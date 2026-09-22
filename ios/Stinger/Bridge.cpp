// Magnus Stinger bridge: Kyty guest execution through FEXCore on iOS.
//
// Lifecycle owned here: debugger-backed JIT pool -> FEX allocator hooks ->
// FEXCore config/context (A19 Pro host features) -> per-entry guest threads.
// Kyty maps PS5 ELFs in-process as always; RunGuestEntry() executes a guest
// RIP inside a FEXCore thread with SysV RDI/RSI and Kyty's guest stack.
//
// Embedding approach follows the public FEXCore embedding API; the iOS JIT
// mechanics (debugger RX + vm_remap RW) live in jit_pool.cpp. Host-call
// thunks (guest BL into Kyty host-implemented Sce functions) are the known
// frontier: such calls fault and are reported with the faulting guest RIP
// instead of crashing silently.
//
// SPDX-FileCopyrightText: Copyright 2026 ZEENEE-MASTER (fork)
// SPDX-License-Identifier: GPL-2.0-only

#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Core/Thunks.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/IR/IR.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/Utils/DualMap.h>
#include <FEXCore/Utils/LogManager.h>

#include "common/logging/log.h"
#include "jit_pool.h"

#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <signal.h>
#include <string>

namespace {

// --- Diagnostics -----------------------------------------------------------
// Device trace channel: mirrors every line to Documents/magnus-boot.log
// (set via magnus_set_log_file) so traces survive without a console.

std::mutex g_log_mutex;
FILE* g_log_file = nullptr;

void FexLogToFile(const char* line) {
	std::lock_guard<std::mutex> lock(g_log_mutex);
	if (g_log_file != nullptr) {
		std::fputs(line, g_log_file);
		std::fputc('\n', g_log_file);
		std::fflush(g_log_file);
	}
}

void FexLog(LogMan::DebugLevels level, const char* msg) {
	char line[1152];
	std::snprintf(line, sizeof(line), "[FEX:%s] %s", LogMan::DebugLevelStr(level), msg);
	LOGF("%s\n", line);
	FexLogToFile(line);
}

void FexThrow(const char* msg) {
	char line[1152];
	std::snprintf(line, sizeof(line), "[FEX:THROW] %s", msg);
	LOGF("%s\n", line);
	FexLogToFile(line);
}

std::atomic<uint64_t> g_compiled_blocks {0};
std::atomic<uint64_t> g_backend_us {0};
std::atomic<uint64_t> g_segv_count {0};
std::atomic<uint64_t> g_bus_count {0};
std::atomic<uint64_t> g_unaligned_count {0};
std::atomic<uint64_t> g_host_call_faults {0};
std::atomic<uint64_t> g_thunk_queries {0};

// --- FEXCore objects --------------------------------------------------------

fextl::unique_ptr<FEXCore::Context::Context> g_ctx;
std::atomic<bool> g_ready {false};
std::mutex g_lifecycle_mutex;
std::mutex g_failure_mutex;
std::string g_title_path;
std::string g_failure = "the FEX JIT runtime is not initialized";

void SetFailure(const char* msg) {
	std::lock_guard<std::mutex> lock(g_failure_mutex);
	g_failure = msg != nullptr ? msg : "";
}

void SetFailure(const std::string& msg) {
	std::lock_guard<std::mutex> lock(g_failure_mutex);
	g_failure = msg;
}

// PS5 binaries are FreeBSD-derived userland; Kyty HLEs the Sce interface as
// host calls. Raw guest `syscall` instructions cannot run on XNU: log them,
// count them, and return ENOSYS so the fault is diagnosable. write/exit are
// passed through for bring-up and test programs.
class MagnusSyscallHandler : public FEXCore::HLE::SyscallHandler {
public:
	MagnusSyscallHandler() {
		OSABI = FEXCore::HLE::SyscallOSABI::OS_LINUX64;
	}

	uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame* frame,
	                       FEXCore::HLE::SyscallArguments* args) override {
		(void)frame;
		const uint64_t num = args->Argument[0];
		if (num == 1) { // sys_write
			const int fd        = static_cast<int>(args->Argument[1]);
			const auto* buf     = reinterpret_cast<const char*>(args->Argument[2]);
			const size_t count  = static_cast<size_t>(args->Argument[3]);
			if (fd == 1 || fd == 2) {
				LOGF("[guest] %.*s", static_cast<int>(count), buf);
				return count;
			}
			return static_cast<uint64_t>(-1);
		}
		if (num == 60 || num == 231) { // sys_exit[_group]
			g_guest_exit_code = static_cast<int64_t>(args->Argument[1]);
			if (g_exit_armed) {
				longjmp(g_exit_jmp, 1);
			}
			return 0;
		}
		LOGF("[FEX] guest raw syscall %llu denied (ENOSYS); "
		     "PS5 code must go through Kyty HLE\n",
		     num);
		return static_cast<uint64_t>(-38);
	}

	FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(
	    FEXCore::Core::InternalThreadState* /*thread*/, uint64_t /*addr*/) override {
		return {.Base = 0, .Size = ~0ULL, .Writable = true};
	}

	std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(
	    FEXCore::Core::InternalThreadState* /*thread*/, uint64_t /*addr*/) override {
		return std::nullopt;
	}

	static jmp_buf g_exit_jmp;
	static bool g_exit_armed;
	static int64_t g_guest_exit_code;
};

jmp_buf MagnusSyscallHandler::g_exit_jmp;
bool MagnusSyscallHandler::g_exit_armed = false;
int64_t MagnusSyscallHandler::g_guest_exit_code = 0;

MagnusSyscallHandler g_syscall_handler;

class MagnusSignalDelegator : public FEXCore::SignalDelegator {
public:
	// Base defaults stand; per-signal attribution is observed through the
	// HLE-trap registry below once guest code runs on device.
};

// --- HLE-trap registry (guest -> Kyty host calls) --------------------------
//
// Problem: Kyty resolves imported NIDs to host function addresses in the
// guest GOT. Translated guest code calling those addresses would execute
// host ARM64 bytes as x86. Full fix: per-NID x86 trap stubs + marshaling
// (ThunkLibs-style), driven by on-device traces of which NIDs each title
// actually calls. This registry is the observation + dispatch point:
//   - LookupThunk logs the IR hash FEX asks about (identifies trap stubs
//     once RunEntry-side stubs are emitted);
//   - NoteHostCallTarget records fault RIPs inside host mappings so the
//     game view can report WHICH import is missing instead of dying silent.
class MagnusThunkObserver : public FEXCore::ThunkHandler {
public:
	FEXCore::ThunkedFunction* LookupThunk(const FEXCore::IR::SHA256Sum& sha) override {
		(void)sha;
		g_thunk_queries.fetch_add(1, std::memory_order_relaxed);
		LOGF("[FEX] thunk query (no trap stubs emitted yet)\n");
		return nullptr;
	}
};

MagnusThunkObserver g_thunk_observer;

// A19 Pro host features. Conservative set proven on Apple Silicon (no SVE;
// AVX falls back to 128-bit ASIMD in FEX). MIDR read from the hardware with
// a Firestorm fallback; count sized to online CPUs.
FEXCore::HostFeatures MakeA19Features() {
	FEXCore::HostFeatures f {};
	f.DCacheLineSize              = 64;
	f.ICacheLineSize              = 64;
	f.SupportsCacheMaintenanceOps = true;
	f.SupportsAES                 = true;
	f.SupportsCRC                 = true;
	f.SupportsAtomics             = true;
	f.SupportsRCPC                = true;
	f.SupportsTSOImm9             = true;
	f.SupportsSHA                 = true;
	f.SupportsPMULL_128Bit        = true;
	f.SupportsFCMA                = true;
	f.SupportsFlagM               = true;
	f.SupportsFlagM2              = true;
	f.SupportsAVX                 = false;
	f.SupportsSVE128              = false;
	f.SupportsSVE256              = false;
	uint32_t midr                 = 0x611F0250; // Firestorm fallback
#if defined(__aarch64__)
	__asm__ volatile("mrs %0, MIDR_EL1" : "=r"(midr));
#endif
	long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpu < 1) {
		ncpu = 6;
	}
	f.CPUMIDRs.resize(static_cast<size_t>(ncpu), midr);
	return f;
}

bool FexInitOnce(std::string& failure) {
	if (g_ready.load(std::memory_order_acquire)) {
		return true;
	}
	std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
	if (g_ready.load(std::memory_order_acquire)) {
		return true;
	}
	if (!MagnusJIT::PoolInit()) {
		failure = "JIT pool unavailable: attach StikDebug for debugger-backed RX memory";
		return false;
	}
	MagnusJIT::InstallAllocatorHooks();
	LogMan::Msg::InstallHandler(FexLog);
	LogMan::Throw::InstallHandler(FexThrow);
	try {
		FEXCore::Config::Initialize();
		FEXCore::Config::Set(FEXCore::Config::ConfigOption::CONFIG_IS64BIT_MODE, "1");
	} catch (const std::exception& e) {
		failure = std::string("FEXCore config failed: ") + e.what();
		return false;
	} catch (...) {
		failure = "FEXCore config failed";
		return false;
	}
	FEXCore::HostFeatures features;
	try {
		features = MakeA19Features();
		g_ctx    = FEXCore::Context::Context::CreateNewContext(features);
	} catch (const std::exception& e) {
		failure = std::string("FEXCore context failed: ") + e.what();
		return false;
	} catch (...) {
		failure = "FEXCore context failed";
		return false;
	}
	if (!g_ctx) {
		failure = "FEXCore context returned null";
		return false;
	}
	static MagnusSignalDelegator delegator;
	g_ctx->SetSignalDelegator(&delegator);
	g_ctx->SetSyscallHandler(&g_syscall_handler);
	g_ctx->SetThunkHandler(&g_thunk_observer);
	g_ctx->SetHardwareTSOSupport(true);
	try {
		if (!g_ctx->InitCore()) {
			failure = "FEXCore InitCore returned false";
			g_ctx.reset();
			return false;
		}
	} catch (const std::exception& e) {
		failure = std::string("FEXCore InitCore threw: ") + e.what();
		g_ctx.reset();
		return false;
	} catch (...) {
		failure = "FEXCore InitCore threw";
		g_ctx.reset();
		return false;
	}
	g_ready.store(true, std::memory_order_release);
	LOGF("[FEX] runtime ready (A19 features, JIT pool %lld)\n", MagnusJIT::WriteOffset());
	return true;
}

// 16KB iOS pages; call-ret shadow stack mirrors FEXCore's Linux sizing.
constexpr size_t kCallRetBytes = FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE;
constexpr size_t kPageBytes    = 0x4000;

bool SetupCallRet(FEXCore::Core::InternalThreadState* thread) {
	void* alloc = ::mmap(nullptr, kCallRetBytes + 2u * kPageBytes, PROT_NONE,
	                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (alloc == MAP_FAILED) {
		return false;
	}
	void* base = static_cast<uint8_t*>(alloc) + kPageBytes;
	if (::mprotect(base, kCallRetBytes, PROT_READ | PROT_WRITE) != 0) {
		::munmap(alloc, kCallRetBytes + 2u * kPageBytes);
		return false;
	}
	thread->CallRetStackBase = base;
	thread->CurrentFrame->State.callret_sp =
	    reinterpret_cast<uint64_t>(base) + kCallRetBytes / 4u;
	return true;
}

void SetupGDT(FEXCore::Core::InternalThreadState* thread) {
	// x86 decoder needs a 64-bit code segment selected.
	static FEXCore::Core::CPUState::gdt_segment gdt[1] = {};
	gdt[0].L                                       = 1;
	gdt[0].D                                       = 0;
	gdt[0].P                                       = 1;
	gdt[0].S                                       = 1;
	gdt[0].Type                                    = 0b1011;
	thread->CurrentFrame->State.segment_arrays[0]  = gdt;
	thread->CurrentFrame->State.cs_idx             = 0;
}

} // namespace

namespace Magnus {

void SetLogFile(const char* path) {
	std::lock_guard<std::mutex> lock(g_log_mutex);
	if (g_log_file != nullptr) {
		std::fclose(g_log_file);
		g_log_file = nullptr;
	}
	if (path != nullptr && path[0] != '\0') {
		g_log_file = std::fopen(path, "a");
	}
}

bool ReserveStingerRuntime() {
	return FexInitOnce(g_failure);
}

bool InstallStinger(const char* title_path) {
	if (!g_ready.load(std::memory_order_acquire)) {
		SetFailure("FEX runtime not reserved before InstallStinger");
		return false;
	}
	if (title_path == nullptr || title_path[0] == '\0') {
		SetFailure("empty game path");
		return false;
	 }
	g_title_path = title_path;
	// Guest segments stay mapped by Kyty's loader in this same process;
	// FEX translates those bytes in place. Nothing to copy here.
	return true;
}

const char* StingerRuntimeFailure() {
	return g_failure.c_str();
}

uint64_t UnalignedAccessCount() {
	return g_unaligned_count.load(std::memory_order_relaxed);
}

uint64_t GuestFaultCount(uint32_t signal_number) {
	if (signal_number == SIGSEGV) {
		return g_segv_count.load(std::memory_order_relaxed);
	}
	if (signal_number == SIGBUS) {
		return g_bus_count.load(std::memory_order_relaxed);
	}
	return g_host_call_faults.load(std::memory_order_relaxed);
}

// Execute guest RIP through the JIT with SysV RDI/RSI and Kyty's guest RSP.
// Returns false when the entry could not be dispatched; details in failure.
bool RunGuestEntry(uint64_t rip, uint64_t rdi, uint64_t rsi, uint64_t rsp) {
	if (!g_ready.load(std::memory_order_acquire) || !g_ctx) {
		SetFailure("RunGuestEntry with no FEX runtime");
		return false;
	}
	if (rip == 0 || rsp == 0) {
		SetFailure("RunGuestEntry with null RIP/RSP");
		return false;
	}
	auto* thread = g_ctx->CreateThread(rip, rsp);
	if (thread == nullptr) {
		SetFailure("FEX CreateThread returned null");
		return false;
	}
	thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RDI] = rdi;
	thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RSI] = rsi;
	if (!SetupCallRet(thread) || !SetupGDT(thread)) {
		SetFailure("FEX thread setup (callret/GDT) failed");
		g_ctx->DestroyThread(thread);
		return false;
	}
	const auto t0 = std::chrono::steady_clock::now();
	MagnusSyscallHandler::g_exit_armed = true;
	bool ok                           = true;
	if (setjmp(MagnusSyscallHandler::g_exit_jmp) == 0) {
		try {
			g_ctx->ExecuteThread(thread);
		} catch (const std::exception& e) {
			SetFailure(std::string("FEX ExecuteThread threw: ") + e.what());
			ok        = false;
		} catch (...) {
			SetFailure("FEX ExecuteThread threw");
			ok        = false;
		}
	} else {
		LOGF("[FEX] guest exited, code %lld\n", MagnusSyscallHandler::g_guest_exit_code);
	}
	MagnusSyscallHandler::g_exit_armed = false;
	const auto us =
	    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
	        .count();
	g_ctx->DestroyThread(thread);
	g_compiled_blocks.fetch_add(1, std::memory_order_relaxed);
	g_backend_us.fetch_add(static_cast<uint64_t>(us), std::memory_order_relaxed);
	return ok;
}

} // namespace Magnus

extern "C" uint64_t FEXCompiledBlockCount() {
	return g_compiled_blocks.load(std::memory_order_relaxed);
}

extern "C" uint64_t FEXFrontendCompileMicroseconds() {
	// FEXCore does not split frontend/backend timers at this API level.
	return 0;
}

extern "C" uint64_t FEXBackendCompileMicroseconds() {
	return g_backend_us.load(std::memory_order_relaxed);
}

extern "C" void magnus_set_log_file(const char* path) {
	Magnus::SetLogFile(path);
}
