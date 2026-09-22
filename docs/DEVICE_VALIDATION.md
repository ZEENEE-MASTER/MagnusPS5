# Device validation (iPhone 17 Pro Max, iOS 26)

Goal: install the unsigned CI IPA, attach JIT, boot a game folder, and bring
back traces that drive the HLE-trap work. No Mac-side guessing.

## 1. Get the IPA

- Actions → `iOS IPA (iPhone 17 Pro Max)` → latest green run on
  `kyty-sync-2026-09-20` → artifact **`magnus-unsigned-ipa`** →
  `MagnusPS5-unsigned.ipa`. Ad-hoc transport signature only.

## 2. Sign it yourself

- Tool of choice: Sideloadly, AltStore/SideStore, or TrollStore where
  available. Use your Apple ID.
- Free Apple ID: installs and runs; provisioning expires in ~7 days, so
  rebuild/reinstall weekly (the app container, games, and logs survive).
- Paid Apple Developer account: additionally apply `ios/Magnus.entitlements`
  (`extended-virtual-addressing`, `increased-memory-limit`,
  `increased-debugging-memory-limit`, `get-task-allow`) at signing time.
  Without these, complex titles die to jetsam/memory caps even with JIT.
- Do NOT expect App Store distribution: JIT needs a debugger and is
  prohibited there.

## 3. Attach JIT (required — the app cannot execute guest code without it)

- Install StikDebug (StikJIT) on the same device, or debug-serve it from a
  host per its docs.
- Attach StikDebug to MagnusPS5, enable JIT for the process, then launch the
  game from inside Magnus. Re-attach after every force-quit: iOS drops the
  debug session and the JIT pool refuses to initialize without it
  (`magnus_boot_failure()` will say the pool is unavailable).

## 4. Place a game folder

- Files app → On My iPhone → MagnusPS5 → Documents → `Games/<Title>/` with
  `eboot.bin` + `sce_sys/param.json` (extracted PS5 application directory;
  no `.pkg`/`.iso` support, no firmware/keys shipped).
- Or use the in-app Import button. Start with small 2D titles.

## 5. Boot and read the screen

- The game view shows `state`, guest frames, boot result, and counters
  (`compiled`, `shader_us`, `pipelines`). First boot of a title is slowest
  (pipeline/shader caches are cold); second boot should improve.

## 6. Bring back traces (this is the deliverable)

- Files app → MagnusPS5 → **`magnus-boot.log`**: full FEX/boot/thunk-query
  log, mirrored automatically from launch.
- Report per title: game ID, iOS version, free vs paid account, JIT method,
  `magnus_stats()` readout, boot-failure string, and the log file.
- Thunk-query lines and host-call fault RIPs in the log are exactly what the
  per-NID marshaller work consumes next: they name which Sce imports each
  title actually calls.

## 7. Known hard limits (not bugs to re-report)

- Guest→host Sce calls without generated traps fault with a logged RIP;
  that is the tracked HLE-trap phase, not a regression.
- Raw guest syscalls return ENOSYS by design (PS5 code must use Kyty HLE).
- 120Hz ProMotion is capped to 60Hz vblank for thermals (see
  `docs/IPHONE17PROMAX_PERF.md`).
