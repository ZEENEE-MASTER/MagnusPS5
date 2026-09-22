// In-repo Stinger stub bridge (unsigned-IPA builds).
//
// The production bridge lives outside this repo at
// ../../Source/Stinger/Bridge.cpp and backs these symbols with the real
// FEX-Emu x86-64 -> ARM64 JIT. Until that lands (see workflow FEX iphoneos
// build), this stub lets the iOS static lib + unsigned IPA link and run the
// UI. Game boot is refused with a clear message via magnus_boot_failure().
//
// SPDX-FileCopyrightText: Copyright 2026 ZEENEE-MASTER (fork)
// SPDX-License-Identifier: GPL-2.0-only

#include <cstddef>
#include <cstdint>

namespace Magnus {

bool InstallStinger(const char* /*title_path*/) {
	return false;
}

bool ReserveStingerRuntime() {
	return false;
}

const char* StingerRuntimeFailure() {
	return "the FEX JIT runtime is not bundled in this unsigned build";
}

uint64_t UnalignedAccessCount() {
	return 0;
}

uint64_t GuestFaultCount(uint32_t /*signal_number*/) {
	return 0;
}

} // namespace Magnus

extern "C" uint64_t FEXCompiledBlockCount() {
	return 0;
}

extern "C" uint64_t FEXFrontendCompileMicroseconds() {
	return 0;
}

extern "C" uint64_t FEXBackendCompileMicroseconds() {
	return 0;
}
