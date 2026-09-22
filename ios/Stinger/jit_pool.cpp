// See jit_pool.h. Original implementation of the public debugger-JIT
// protocol (csops/CS_DEBUGGED, BRK #0xf00d x16=1/0, vm_remap dual-map).
//
// SPDX-FileCopyrightText: Copyright 2026 ZEENEE-MASTER (fork)
// SPDX-License-Identifier: GPL-2.0-only

#include "jit_pool.h"

#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/Utils/DualMap.h>

#include <mach/mach.h>
#include <mach/vm_map.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <signal.h>
#include <ucontext.h>

#ifndef CS_DEBUGGED
#define CS_DEBUGGED 0x10000000
#endif
#ifndef CS_OPS_STATUS
#define CS_OPS_STATUS 0
#endif
extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);

namespace MagnusJIT {
namespace {

constexpr size_t kPage = 0x4000; // 16KB iOS pages

void* g_rx_base   = nullptr;
void* g_rw_base   = nullptr;
size_t g_pool_size = 0;
std::atomic<size_t> g_pool_used {0};
std::mutex g_pool_mutex;

size_t AlignUp(size_t v, size_t a) {
	return (v + a - 1u) & ~(a - 1u);
}

void SigtrapSkip(int, siginfo_t*, void* ctx) {
	auto* uc = static_cast<ucontext_t*>(ctx);
#if defined(__aarch64__)
	uc->uc_mcontext->__ss.__pc += 4;
	uc->uc_mcontext->__ss.__x[0] = 0;
#endif
}

} // namespace

bool DebuggerAttached() {
	uint32_t flags = 0;
	if (csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) != 0) {
		return false;
	}
	return (flags & CS_DEBUGGED) != 0;
}

void InstallTrapHandlerIfUndbugged() {
	if (DebuggerAttached()) {
		return; // Debugger owns BRK/SIGTRAP; do not steal them.
	}
	struct sigaction sa {};
	sa.sa_flags     = SA_SIGINFO;
	sa.sa_sigaction = SigtrapSkip;
	sigaction(SIGTRAP, &sa, nullptr);
}

// BRK #0xf00d protocol: x16=1 prepare(x0=hint, x1=len)->x0, x16=0 detach.
__attribute__((noinline, optnone)) static void* DebuggerPrepare(void* addr, size_t len) {
	register void* x0 __asm__("x0") = addr;
	register size_t x1 __asm__("x1") = len;
	__asm__ volatile("mov x16, #1\n"
	                 "brk #0xf00d\n"
	                 : "+r"(x0)
	                 : "r"(x1)
	                 : "x16", "memory");
	return x0;
}

bool PoolInit(size_t size) {
	if (g_rx_base != nullptr) {
		return true;
	}
	if (!DebuggerAttached()) {
		std::fprintf(stderr, "[MagnusJIT] no debugger attached, RX pool unavailable\n");
		return false;
	}
	size             = AlignUp(size, kPage);
	mach_port_t task = mach_task_self();
	void* rx         = DebuggerPrepare(nullptr, size);
	if (rx == nullptr) {
		std::fprintf(stderr, "[MagnusJIT] debugger RX allocation failed\n");
		return false;
	}
	vm_address_t rw   = 0;
	vm_prot_t cur = 0, mx = 0;
	kern_return_t kr = vm_remap(task, &rw, size, 0, VM_FLAGS_ANYWHERE, task,
	                            reinterpret_cast<vm_address_t>(rx), FALSE, &cur, &mx,
	                            VM_INHERIT_NONE);
	if (kr != KERN_SUCCESS) {
		std::fprintf(stderr, "[MagnusJIT] vm_remap RW mirror failed: %d\n", kr);
		return false;
	}
	kr = vm_protect(task, rw, size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
	if (kr != KERN_SUCCESS) {
		std::fprintf(stderr, "[MagnusJIT] vm_protect RW failed: %d\n", kr);
		vm_deallocate(task, rw, size);
		return false;
	}
	g_rx_base   = rx;
	g_rw_base   = reinterpret_cast<void*>(rw);
	g_pool_size = size;
	FEXCore::DualMap::WriteOffset =
	    static_cast<int64_t>(reinterpret_cast<intptr_t>(g_rw_base) -
	                         reinterpret_cast<intptr_t>(g_rx_base));
	// Coherence check.
	const uint32_t probe = 0xC0DEFACEu;
	std::memcpy(g_rw_base, &probe, sizeof(probe));
	uint32_t back = 0;
	std::memcpy(&back, g_rx_base, sizeof(back));
	if (back != probe) {
		std::fprintf(stderr, "[MagnusJIT] dual-map coherence FAILED\n");
		return false;
	}
	return true;
}

void* PoolAllocRX(size_t size) {
	if (g_rx_base == nullptr) {
		return nullptr;
	}
	size          = AlignUp(size, kPage);
	const size_t off = g_pool_used.fetch_add(size, std::memory_order_relaxed);
	if (off + size > g_pool_size) {
		return nullptr;
	}
	return static_cast<uint8_t*>(g_rx_base) + off;
}

bool InPool(const void* addr) {
	if (g_rx_base == nullptr) {
		return false;
	}
	const auto a = reinterpret_cast<uintptr_t>(addr);
	const auto b = reinterpret_cast<uintptr_t>(g_rx_base);
	return a >= b && a < b + g_pool_size;
}

bool IsReady() {
	return g_rx_base != nullptr;
}

int64_t WriteOffset() {
	if (g_rx_base == nullptr) {
		return 0;
	}
	return static_cast<int64_t>(reinterpret_cast<intptr_t>(g_rw_base) -
	                            reinterpret_cast<intptr_t>(g_rx_base));
}

namespace {
void* PoolMmapHook(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
	if ((prot & PROT_EXEC) != 0 && g_rx_base != nullptr) {
		if (void* p = PoolAllocRX(length)) {
			return p;
		}
		return MAP_FAILED;
	}
	return ::mmap(addr, length, prot, flags, fd, offset);
}

int PoolMunmapHook(void* addr, size_t length) {
	if (InPool(addr)) {
		return 0; // Bump allocator: no free.
	}
	return ::munmap(addr, length);
}
} // namespace

void InstallAllocatorHooks() {
	InstallTrapHandlerIfUndbugged();
	FEXCore::Allocator::mmap   = PoolMmapHook;
	FEXCore::Allocator::munmap = PoolMunmapHook;
}

} // namespace MagnusJIT
