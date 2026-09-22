// Magnus iOS JIT pool: debugger-backed RX + vm_remap RW dual-map.
//
// Protocol (technique after public iOS JIT practice): StikDebug attaches as
// debugger (CS_DEBUGGED via csops), allocates RX pages on BRK #0xf00d
// (x16=1 prepare / x16=0 detach), and we mirror them RW with vm_remap so
// FEX CodeBuffers are writable through one view and executable through the
// other. Without a debugger, BRK would trap: a SIGTRAP skip-handler is
// installed only in that case. 16KB iOS pages throughout.
//
// SPDX-FileCopyrightText: Copyright 2026 ZEENEE-MASTER (fork)
// SPDX-License-Identifier: GPL-2.0-only

#ifndef MAGNUS_IOS_STINGER_JIT_POOL_H_
#define MAGNUS_IOS_STINGER_JIT_POOL_H_

#include <cstddef>
#include <cstdint>

namespace MagnusJIT {

// 64MB pool. Returns false when no debugger is attached or mapping fails.
bool PoolInit(size_t size = 64u * 1024u * 1024u);

// Bump-allocate executable bytes; returns the RX (canonical) pointer or
// nullptr when exhausted. Thread-safe.
void* PoolAllocRX(size_t size);

bool InPool(const void* addr);

bool IsReady();

// RX->RW distance for FEXCore::DualMap::WriteOffset.
int64_t WriteOffset();

// Install FEXCore::Allocator mmap/munmap hooks so FEX executable mappings
// land in the pool. Must be called before FEXCore allocates.
void InstallAllocatorHooks();

bool DebuggerAttached();

} // namespace MagnusJIT

#endif
