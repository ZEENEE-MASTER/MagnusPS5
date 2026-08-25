# iPhone setup and troubleshooting

This guide describes the behavior of the Magnus 0.0.1 release as published on August 25, 2026. It
does not assume that a structurally valid game is compatible with the emulator.

## What the official IPA contains

The published `Magnus.ipa` is an arm64 app with a minimum iOS version of 17.4. Its binary is ad-hoc
signed (no Team ID and no embedded provisioning profile), so it must be signed for the destination
device before installation. The release binary declares:

```xml
<key>com.apple.developer.kernel.extended-virtual-addressing</key><true/>
<key>com.apple.developer.kernel.increased-memory-limit</key><true/>
<key>com.apple.developer.kernel.increased-debugging-memory-limit</key><true/>
<key>get-task-allow</key><true/>
```

Entitlements written into the original binary are not sufficient by themselves. The final code
signature and provisioning profile must authorize them. Re-signing can remove an entitlement or
produce a package that installs but lacks the capability at runtime.

## Why Magnus needs both address space and RAM allowances

The emulator models 13,824 MiB of guest physical memory with sparse file-backed mappings. This does
not mean it immediately consumes 13.5 GiB of iPhone RAM, but it does require a large virtual address
layout. Magnus also reads `kernel.flexibleMemorySize` from the game's `sce_sys/param.json`; if it is
absent, the emulator defaults to 1 GiB of flexible guest memory.

- **Extended Virtual Addressing** permits the large address layout. Add the capability to the
  Magnus App ID/target before generating the provisioning profile. GetMoreRam does not do this.
- **Increased Memory Limit** asks iOS for a higher resident-memory ceiling on supported devices.
  GetMoreRam toggles this capability for an existing App ID, after which Magnus must be signed and
  installed again with a regenerated profile.
- **Increased Debugging Memory Limit** is present in the official release and is useful while the
  process is attached to a debugger. Preserve it when the signing account/profile supports it.
- iOS can still terminate Magnus under memory or thermal pressure. An entitlement is not a RAM
  reservation and cannot make unavailable physical memory appear.

## Recommended signing order

SideStore 0.6.3 requests Extended Virtual Addressing, Increased Memory Limit, and Increased
Debugging Memory Limit as additional entitlements during installation, updates the App ID features,
and then fetches the provisioning profile. A direct IPA import therefore lets SideStore perform the
final developer-account signing on the phone. GetMoreRam remains useful for explicitly enabling or
checking Increased Memory Limit, but it is not a replacement for the other capabilities.

1. Choose one stable, unique Magnus bundle identifier. Do not change it between GetMoreRam,
   signing, and StikDebug.
2. Register that App ID with the Apple account used by the signing tool.
3. Enable **Extended Virtual Addressing** for the App ID/target. With source and Xcode this is the
   target's Signing & Capabilities setting; with an IPA signer, the generated provisioning profile
   still has to authorize the entitlement.
4. In GetMoreRam, sign in with the same account, refresh App IDs, select the Magnus App ID, and use
   **Add Increased Memory Limit**.
5. Re-sign/reinstall Magnus so a fresh profile is embedded. Preserve all four entitlements listed
   above. A free account or a particular signer may not support every capability.
6. Run `tools/check-ipa.sh` on the final IPA—not the upstream IPA or an intermediate copy.

If Extended Virtual Addressing is missing, Magnus reports that no title can boot because it cannot
reserve the guest address space. Adding only Increased Memory Limit does not fix that failure.

## StikDebug setup and Magnus's custom JIT flow

StikDebug supports on-device JIT on iOS 17.4 and later. Its documented setup requires StikDebug, a
pairing file for the device, a loopback VPN such as LocalDevVPN, and a sideloaded target app carrying
`get-task-allow`.

1. Install and open StikDebug once.
2. Import a current pairing file created while the iPhone is unlocked and trusted.
3. Enable the loopback VPN and confirm StikDebug can connect/mount its developer support files.
4. Launch Magnus and select Play.
5. Magnus opens a request equivalent to:

   ```text
   stikdebug://enable-jit?bundle-id=<Magnus bundle>&pid=<running pid>&script-name=jit.js&script-data=<encoded script>
   ```

6. In StikDebug, approve **Enable and Run Script**. The bundled script attaches to that exact Magnus
   process and prepares executable regions requested by Magnus. Return to Magnus after StikDebug
   reports success.

This custom script matters. Selecting Magnus manually and running only StikDebug's generic
`universal.js` is not equivalent to accepting Magnus's request. The Magnus script watches its own
breakpoint protocol and prepares requested RX regions.

## Importing a game

Place or select a complete extracted application folder. The directory selected in Files must be
the directory that directly contains both `eboot.bin` and `sce_sys/param.json`; do not select its
parent and do not add an extra nesting level.

Before copying a large title to the phone, run:

```bash
ruby tools/check-game.rb /path/to/PPSAxxxxx-app
```

Magnus copies imported content into its own Games library. Leave enough storage for that additional
copy plus shader caches, screenshots, and temporary data.

## Symptom guide

| Symptom | Most likely cause | Check |
| --- | --- | --- |
| `No title can boot` / no extended virtual addressing | Final provisioning profile did not grant Extended Virtual Addressing, or re-signing stripped it. | Run `tools/check-ipa.sh` on the final IPA and recreate the App ID/profile. |
| `StikDebug is not installed` | The `stikdebug` URL scheme is unavailable. | Install/open current StikDebug. |
| `StikDebug did not attach` | Pairing, VPN, developer disk image, bundle ID, PID, or `get-task-allow` failure. | Keep Magnus running, refresh pairing, enable VPN, and verify the final IPA. |
| `this game folder has no eboot.bin` | Wrong directory level or incomplete import. | Select the folder that directly contains `eboot.bin`. |
| Missing `sce_sys/param.json` | Wrong format, wrong directory level, or PS4-style metadata. | Magnus expects PS5 JSON metadata, not `param.sfo`. |
| `elf is not valid` / recompiler could not load | Encrypted, compressed, corrupt, unsupported SELF/ELF, or unsupported module. | Run `tools/check-game.rb`; redump your own content if the file is incomplete. |
| Game returns/stops immediately | Input parsed, but the guest hit an unsupported library/CPU path. | Capture the first fatal or unresolved-import line. |
| Black screen with guest frames increasing | Renderer/shader/presentation problem. | Record guest-frame and pipeline counters plus the first shader/Vulkan error. |
| iOS closes Magnus | Memory pressure, thermal pressure, invalid memory mapping, or crash. | Preserve the crash log; check entitlements, free storage, temperature, and first fault line. |
| Second game will not launch | Intentional one-game-per-process rule. | Fully close Magnus and relaunch it. |

## Reporting a useful emulation failure

Report the earliest failure, not the last cascade of errors. Include device/iOS, game title ID and
version, validation summaries, signed-IPA entitlement summary, and the first relevant log block.
Never upload a game executable, dump, pairing file, provisioning profile, Apple credentials, or
certificate.

## References

- [Apple: Extended Virtual Addressing Entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.extended-virtual-addressing)
- [Apple: Increased Memory Limit entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.increased-memory-limit)
- [GetMoreRam](https://github.com/hugeBlack/GetMoreRam)
- [StikDebug](https://github.com/StikDebug/StikDebug)
