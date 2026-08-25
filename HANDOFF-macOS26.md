# MagnusPS5 macOS 26 handoff

Prepared August 25, 2026 for continuation on a Mac running macOS Tahoe 26.2 or later.

## Resume point

- Fork: <https://github.com/hernan0078/MagnusPS5>
- Working branch: `codex/iphone-setup-analysis`
- Upstream: <https://github.com/BaconMakin/MagnusPS5>
- All analysis, validation, and handoff changes are contained in this branch.
- Previous analysis environment: Apple silicon Mac running macOS 15

Clone the exact working branch:

```sh
git clone --branch codex/iphone-setup-analysis \
  https://github.com/hernan0078/MagnusPS5.git
cd MagnusPS5
git remote add upstream https://github.com/BaconMakin/MagnusPS5.git
```

## Required development environment

The published Magnus 0.0.1 IPA reports this embedded build metadata:

- Xcode 26.6 (`DTXcode=2660`, build `17F113`)
- iPhoneOS 26.5 SDK (build `23F81a`)
- minimum deployment target iOS 17.4

Apple requires macOS Tahoe 26.2 or later for Xcode 26.6. Install Xcode 26.6, launch it once,
accept the license, install the iOS platform components, and sign in with the developer account.

Verify the destination Mac:

```sh
sw_vers
xcodebuild -version
xcrun --sdk iphoneos --show-sdk-version
security find-identity -v -p codesigning
```

The iPhone must have Developer Mode enabled. Connect it by USB for the initial Xcode pairing and
trust prompts.

## Transfer artifact

The handoff bundle includes:

- `Magnus-0.0.1-SideStore-Public.ipa`
- this handoff document
- `SHA256SUMS`

Expected IPA SHA-256:

```text
1f5045875dd3658e20a7d84c0f13be587111ab841d2307771a7921ae4cfcdf0f  Magnus-0.0.1-SideStore-Public.ipa
```

The IPA is deliberately prepared for SideStore import. It is ad-hoc signed and has no embedded
provisioning profile; SideStore must perform final developer-account signing and provisioning.

Verify it after transfer:

```sh
shasum -a 256 Magnus-0.0.1-SideStore-Public.ipa
tools/check-ipa.sh /absolute/path/to/Magnus-0.0.1-SideStore-Public.ipa
```

## Required entitlements and JIT flow

The final installed Magnus build must preserve:

```text
com.apple.developer.kernel.extended-virtual-addressing = true
com.apple.developer.kernel.increased-memory-limit = true
com.apple.developer.kernel.increased-debugging-memory-limit = true
get-task-allow = true
```

Extended Virtual Addressing increases usable address space, not physical RAM. The memory-limit
entitlements request a larger memory allowance but do not guarantee it.

Magnus bundles `jit.js` and requests StikDebug through:

```text
stikdebug://enable-jit?bundle-id=...&pid=...&script-name=jit.js&script-data=...
```

StikDebug must have a valid pairing file and its loopback VPN active. Approve Magnus's own
**Enable and Run Script** request. A JIT attachment belongs to one process ID, so it must be repeated
after every force-quit/relaunch.

## Game input

Magnus expects an extracted PS5 application directory, not an ISO, PKG, archive, or a lone EBOOT.
The imported folder must contain at least:

```text
eboot.bin
sce_sys/param.json
```

The previously inspected game was Minecraft `PPSA17221`, content version `01.008.000`. Its root
`eboot.bin` and six adjacent PRX/SPRX modules were structurally compatible with the current Magnus
SELF loader. That does not prove the game is playable.

The original folder was previously located at:

```text
$HOME/Downloads/PPSA17221-app
```

That path no longer exists at handoff time, so the game data is not included in the bundle. Copy a
lawfully obtained extracted folder separately, then validate it before import:

```sh
ruby tools/check-game.rb /absolute/path/to/PPSA17221-app
```

## Known boot behavior

Magnus intentionally permits only one boot attempt per app process. It sets `g_booted_once` before
the boot is confirmed. A failed first attempt therefore causes every subsequent attempt to show:

```text
Magnus runs one game per launch. Close Magnus and open it again to play another.
```

That message is a retry guard, not a JIT or RAM failure. Fully swipe Magnus away, relaunch it,
reattach JIT to the new PID, and press Play only once. Preserve the first error; retrying masks it.

If the message appears on the very first Play after a confirmed force-quit, investigate whether the
unpublished Swift frontend is calling `magnus_boot_game` twice.

## Controller support

The released binary links Apple's GameController framework and contains GCController, DualSense,
DualShock, Xbox, and MFi support. The core maps sticks, triggers, D-pad, face buttons, shoulders,
L3/R3, Options/Share, and touchpad click. Advanced DualSense adaptive triggers, motion, speaker,
light-bar control, and high-definition haptics are not confirmed.

Pair the controller before launching Magnus. Force-quit and reopen Magnus if a newly paired
controller is not detected.

## Performance troubleshooting capture

Run performance tests on the iPhone and capture them from the Mac. Use a fresh Magnus process for
each test and keep the same game area, camera position, duration, and device temperature.

1. Connect the iPhone by USB and select it in macOS Console.
2. Filter for process `Magnus` and save all lines beginning with `Magnus:`.
3. Force-quit Magnus, relaunch, attach StikDebug JIT, and press Play once.
4. Reproduce the slow section for 60-90 seconds.
5. Record the exact wall-clock time of the slowdown and export the log.
6. Collect any crash or jetsam report from Xcode's Devices and Simulators window.
7. When attachment works, capture Xcode Instruments traces with Time Profiler, Metal System Trace,
   Allocations, and VM Tracker. Capture one instrument at a time before combining them.

Magnus exposes counters for:

- guest and host frame activity
- compiled FEX blocks and frontend compilation time
- shader compilation time
- graphics pipeline count and pipeline creation time
- unaligned accesses, SIGSEGV guest faults, and SIGBUS guest faults

Useful log filters include:

```text
Magnus:JIT
Magnus:Stinger
Magnus:Boot
Magnus:Core
Magnus:Fault
Unresolved import
Shader
Pipeline
```

Do not share pairing files, provisioning profiles, Apple-account credentials, or copyrighted game
binaries with logs.

## Source-rebuild blocker

The public repository is not a complete source-reproducible iOS application. It contains the C++
emulator core and iOS bridge but not the Swift/Xcode frontend project. Its build also references
components that are not fully supplied, including FEX integration, `Source/Stinger/Bridge.cpp`, and
MoltenVK static/private-header inputs.

Consequences:

- The released IPA can be inspected, re-signed, installed, logged, and profiled.
- The published C++ core can be analyzed and patched.
- Rebuilding the original IPA and debugging the Swift interface requires the missing project and
  dependencies from the upstream developer, or a deliberate reconstruction of the frontend.

Do not report a source-reproducible IPA until those pieces are present and the full build succeeds.

## Recommended next actions on macOS 26

1. Verify Xcode 26.6 and iOS 26.5 device support.
2. Pair the iPhone and confirm Instruments can see the SideStore-signed Magnus process.
3. Validate the final installed entitlements and provisioning profile.
4. Capture the first real Minecraft boot failure before retrying.
5. Capture a reproducible performance trace only after the game reaches gameplay.
6. Classify the bottleneck as CPU/JIT, shader/pipeline, memory/jetsam, guest fault, unresolved import,
   or thermal throttling before changing code.
7. Ask upstream for the missing Swift/Xcode project and build dependencies.

## Existing project documentation and tools

- `README.md`: format, signing, and scope overview
- `docs/iphone-setup.md`: detailed entitlement, StikDebug, and troubleshooting notes
- `ios/Magnus.entitlements`: required entitlement template
- `tools/check-game.rb`: validates an extracted game folder
- `tools/check-ipa.sh`: validates a prepared/signed IPA
- `tools/prepare-sidestore-ipa.sh`: prepares the upstream IPA for SideStore final signing
