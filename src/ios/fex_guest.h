// Kyty -> FEX guest-execution hook (iOS only).
//
// RunEntry() in loader/runtimeLinker.cpp calls this instead of invoking the
// guest address as a native function pointer when the host cannot run x86-64
// code directly (iPhone ARM64). Implemented in ios/Stinger/Bridge.cpp.
//
// SPDX-FileCopyrightText: Copyright 2026 ZEENEE-MASTER (fork)
// SPDX-License-Identifier: GPL-2.0-only

#ifndef EMULATOR_SRC_IOS_FEX_GUEST_H_
#define EMULATOR_SRC_IOS_FEX_GUEST_H_

#include <cstdint>

namespace Magnus {

// Execute guest RIP through the FEX JIT with SysV RDI/RSI and the given
// guest RSP. Returns false when the entry could not be dispatched; details
// via StingerRuntimeFailure().
bool RunGuestEntry(uint64_t rip, uint64_t rdi, uint64_t rsi, uint64_t rsp);

} // namespace Magnus

#endif
