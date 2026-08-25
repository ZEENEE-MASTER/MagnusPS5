# MagnusPS5 iPhone analysis fork

For continuation on a Mac with Xcode 26.6, see [HANDOFF-macOS26.md](HANDOFF-macOS26.md).

This fork tracks [BaconMakin/MagnusPS5](https://github.com/BaconMakin/MagnusPS5) and adds
repeatable checks for game folders and signed iOS packages. Magnus is experimental. Upstream's
`0.0.1` release says that only a limited number of games work and that it was built and tested for
an iPhone 17 Pro Max.

## Game format

Magnus does **not** open a PS5 `.iso`, `.pkg`, archive, or disc image. Its iOS front end imports an
**extracted PS5 application directory** whose root contains:

```text
PPSAxxxxx-app/
├── eboot.bin
├── sce_sys/
│   └── param.json
├── sce_module/       # optional game modules
└── ...               # all other game assets, with their original paths preserved
```

The executable must be a compatible x86-64 Prospero ELF or SELF container. The current loader only
handles supported SELF signatures whose mapped segments are already uncompressed. A retail `.pkg`
or an encrypted/incomplete copy is not a valid input.

Use only content dumped from a console and game you own. This fork does not contain games, keys,
firmware, or DRM-circumvention tools.

Validate a folder without changing it:

```bash
ruby tools/check-game.rb /path/to/PPSAxxxxx-app
```

The result `STRUCTURALLY COMPATIBLE` means Magnus can recognize and parse the input. It does not
promise that the game is playable; library, CPU, shader, memory, or renderer emulation can still be
missing.

## iPhone signing, memory, and JIT

Read [docs/iphone-setup.md](docs/iphone-setup.md) before re-signing or installing the IPA. The short
version is that four entitlements have distinct jobs:

| Entitlement | Purpose |
| --- | --- |
| `com.apple.developer.kernel.extended-virtual-addressing` | Lets Magnus reserve the large sparse guest address map. This is address space, not physical RAM. |
| `com.apple.developer.kernel.increased-memory-limit` | Requests a higher resident-memory limit on supported devices. Extra memory is not guaranteed. |
| `com.apple.developer.kernel.increased-debugging-memory-limit` | Requests the debugging memory allowance used by the release build. |
| `get-task-allow` | Allows StikDebug to attach to the sideloaded Magnus process. |

[GetMoreRam](https://github.com/hugeBlack/GetMoreRam) enables only the **Increased Memory Limit**
capability for an App ID. It does not add Extended Virtual Addressing or enable JIT.
[StikDebug](https://github.com/StikDebug/StikDebug) performs the debugger/JIT attach. Magnus 0.0.1
already contains a custom `jit.js` script and asks StikDebug through its `stikdebug://enable-jit`
URL scheme.

Check an IPA after the final signing step:

```bash
tools/check-ipa.sh /path/to/Magnus-resigned.ipa
```

Prepare the upstream ad-hoc release for direct import into SideStore:

```bash
tools/prepare-sidestore-ipa.sh /path/to/Magnus.ipa /path/to/Magnus-SideStore.ipa
```

This preserves the requested entitlement declarations. SideStore still performs the final signing
with the Apple account configured on the device and embeds the resulting provisioning profile.

## Reproducibility status

The public repository is not currently enough to reproduce the released app. It contains the
emulator core but no Magnus Swift/Xcode application project, and its CMake configuration refers to
unpublished or separately supplied paths for:

- `Source/Stinger/Bridge.cpp`
- a sibling FEX source and iPhoneOS static build
- MoltenVK iPhoneOS library and private headers
- the final app target, assets, signing configuration, and entitlements

Until those pieces or build instructions are published, this fork can audit inputs and the release
IPA, but it cannot honestly claim a source-reproducible Magnus build.

## Troubleshooting

Start with [docs/iphone-setup.md](docs/iphone-setup.md), keep the exact signed IPA that was installed,
and include these items in a report:

- iPhone model and iOS version
- output from `tools/check-ipa.sh`
- output from `tools/check-game.rb`
- the first Magnus error shown and the relevant `Magnus:` log lines
- whether StikDebug confirmed the script attach

Do not attach the game, `eboot.bin`, account credentials, pairing files, or signing certificates.
