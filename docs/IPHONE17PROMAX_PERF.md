# iPhone 17 Pro Max (A19 Pro) Performance Guide — Magnus + Kyty Sep-20

Target device: iPhone 17 Pro Max, A19 Pro (2x P @4.26GHz + 4x E @2.6GHz, 32MB SLC),
6-core GPU @~1.6GHz with Neural Accelerators + HW ray tracing, 12GB RAM, iOS 26,
6.9" 120Hz ProMotion. This fork tracks KytyPS5 `ba55ba5` (2026-09-20) + Magnus iOS glue
(FEX JIT + MoltenVK + Stinger Bridge).

> Expectation reset: PS5 is x86-64 + RDNA2. iPhone is ARM64 + Apple GPU.
> Every frame goes through FEX x86->ARM JIT *and* Vulkan->MoltenVK->Metal.
> 2D / lightweight Unity / UE4 titles can boot. AAA 3D will be slow or crash.
> No firmware, keys, games, or `.pkg`/`.iso` handling are included — use only
> legally extracted `eboot.bin + sce_sys/param.json` folders (see `docs/iphone-setup.md`,
> `tools/check-game.rb`).

## What the Sep-20 Kyty update fixed (relevant to iOS)

- SaveDataMemory2 persisted to disk (Astro Bot progress, #659)
- ATRAC9 sampler blocks + NGS2 PCM buffers (less silent audio)
- AvPlayer EOF + startup state (fewer video hangs)
- Tessellation optional + GUI toggle (keep OFF on iOS — MoltenVK tessellation is slow)
- Video aspect + 3-vertex rect rendering (fewer stretched/missing frames)
- `S_BITCMP0/1_B64`, partial `V_NOT_B32` SDWA, signed sampler compares
- Cubemap face storage access, zero-valued indirect CX skip
- Unicode paths, game-list rescan avoidance
- FaultManager + descriptorHeap + pipelineCache work (keep shader cache ON)

## Recommended defaults for A19 Pro

Via `src/ios/embed.h`:

```c
magnus_set_vblank_frequency(60);   // not 120 — halves GPU + thermal cost
magnus_set_metal_fx(true,false,false); // spatial ON, temporal OFF, interpolation OFF
magnus_set_screen_size(0);         // lowest internal res, upscale via MetalFX
magnus_set_volume(100);
magnus_set_shader_cache_dir("<Application Support>/MagnusPS5/shader_cache");
magnus_set_network_enabled(false); // enable only if game needs it
```

Entitlements (`ios/Magnus.entitlements`, already in repo):
- `extended-virtual-addressing`, `increased-memory-limit`,
  `increased-debugging-memory-limit`, `get-task-allow`
- Requires **paid** Apple Developer Program for full JIT + memory limits.
  Free accounts install UI but cannot boot games reliably.
- Re-attach JIT (StikDebug / SideStore `jit.js`) after every force-quit.

## FEX JIT tuning (A19 Pro)

CMake wires `../FEX` static libs (`libFEXCore.a`, etc.). In your Stinger/Xcode
scheme or `FEXConfig`:

- JIT enabled, interpreter fallback OFF for hot blocks
- Block size: default; increase `MaxInstPerBlock` only if stability holds
- Threads: pin emulation threads to P-cores (`qos_class_user_interactive`)
- `HostFeatures`: enable AFP, LSE, RDM if FEX build supports A19
- Disable FEX debug logging in Release (`MAGNUS_VERBOSE` unset)
- `magnus_stats()` exposes `compiled_blocks`, `frontend/backend_compile_us` —
  if backend >> frontend, lower in-game resolution first.

## MoltenVK / Metal tuning

- `KYTY_VULKAN_TARGET=vulkan1.0` on iOS (set in CMakeLists). Do NOT force 1.3 —
  MoltenVK on iOS 26 translates 1.0 most reliably.
- `MVK_CONFIG_*` (set via `MTL` env or MoltenVK plist):
  - `MVK_ALLOW_METAL_FENCES=1`, `MVK_ALLOW_METAL_EVENTS=1`
  - `MVK_CONFIG_USE_MTLHEAP=1` (12GB helps heap reuse)
  - `MVK_CONFIG_SHADER_FAST_MATH=1`
  - Async queue + dedicated transfer queue ON
- Keep tessellation OFF, MSAA 1x, anisotropy 1x on first boot.
- Prefer 60Hz vblank; 120Hz ProMotion doubles MoltenVK present cost + thermals.

## Thermal + battery (A19 Pro throttles under sustained JIT)

- Cap to 30/60fps in-game where possible; avoid 120fps targets.
- Play plugged in with Low Power OFF, but remove case if hot.
- If `segv/bus` spikes in `magnus_stats()`, stop — jetsam or guard-page fault,
  not a game bug. Relaunch, lower res, clear shader cache once.
- Shader cache is persistent — first boot of a title is slowest (pipeline
  creation). Second boot should be faster (`pipelines`, `pipeline_create_us`).

## Debugging checklist

1. `tools/check-game.rb /path/to/GameFolder` — must contain `eboot.bin`, `sce_sys/param.json`
2. `tools/check-ipa.sh` — validates Payload, entitlements, no `/Users` paths
3. `tools/prepare-sidestore-ipa.sh in.ipa out.ipa` — strips profile, ad-hoc signs,
   SideStore does final signing.
4. Console: `MAGNUS_VERBOSE=1` only for repro; silent otherwise.
5. File issues with: device (17 Pro Max, iOS 26.x), game ID, `magnus_stats()` dump,
   full log,paid vs free dev account, JIT method.

## Build (GitHub Actions, no local Mac required)

`.github/workflows/ios-ipa.yml` builds `kyty_emulator` static lib for
`iphoneos arm64` with FEX + MoltenVK. Full `.ipa` still needs Stinger Xcode
project (`../../Source/Stinger/Bridge.cpp`, outside this repo) + paid signing.
Artifacts upload `libkyty_emulator.a` for Stinger linking.
