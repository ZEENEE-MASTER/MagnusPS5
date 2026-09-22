# Stinger (in-repo iOS front-end)

Programmatic UIKit, Objective-C only. No Swift, storyboards, XIBs, or asset
catalogs. Links `libkyty_emulator.a` via `src/ios/embed.h`.

- `main.m` — library (Documents/Games, import via Files, eboot check),
  Metal game view (`magnus_set_surface`), A19 Pro defaults (60Hz, spatial
  MetalFX, res 0, shader cache, network off), status via `magnus_stats()`.
- `Bridge.cpp` — stub for the out-of-repo production bridge. Game boot is
  refused with "FEX JIT runtime is not bundled in this unsigned build" until
  the real FEX-backed Stinger lands. Replace by setting
  `MAGNUS_STINGER_BRIDGE` to `../../Source/Stinger/Bridge.cpp`.
- `build-unsigned-ipa.sh` — compiles, links (undefined = fatal), ad-hoc
  signs for transport only, packages executable + Info.plist, verifies
  SHA-256. No entitlements embedded; your signing tool
  (Sideloadly/AltStore/SideStore) applies `ios/Magnus.entitlements` with your
  Apple ID (paid account needed for JIT + memory limits).
