# FEX licensing notice (MagnusPS5 iOS fork)

## Facts (verified)

- **KytyPS5** (`https://github.com/KytyPS5/KytyPS5`) is licensed **GPL-2.0-only**.
  This fork's `LICENSE` is the same GPL v2 text.
- **willfaust/FEX** (the iOS-capable FEX fork this build uses, pin `053c385`)
  states in its `LICENSE-MADEIRA.md`:
  - all upstream FEX-Emu code stays under its upstream licence (**MIT**),
  - modifications and new files authored for Madeira are **GPL-3.0-or-later**,
  - files mixing both may only be distributed under GPL-3.0-compatible terms.
- This fork does **not** vendor FEX sources. CI clones the pinned FEX fork
  as a sibling directory (`../FEX`) and links its built archives
  (`libFEXCore.a`, …) into the iOS static lib. Our own glue
  (`ios/Stinger/*`, `src/ios/*`) is written for this fork.

## Consequence

Linking GPL-2.0-only Kyty objects with GPL-3.0-covered FEX objects produces
one binary (`MagnusPS5.app/MagnusPS5`) whose two parts impose incompatible
licence terms. Therefore:

- Private builds for personal use on your own device: no distribution takes
  place, so this conflict is not triggered.
- **Do not redistribute IPAs** (GitHub Releases, links, mirrors) until the
  conflict is resolved, via one of:
  1. permission from the KytyPS5 authors to combine with GPL-3.0 code
     (draft: `docs/license-request-kyty.md`), or
  2. permission from Will Faust to use the FEX iOS port bits under MIT
     terms (draft: `docs/license-request-madeira.md`), or
  3. a clean-room iOS port against pure upstream (MIT) FEX with no
     GPL-3.0-covered lines linked in.

Keeping FEX as a pinned external clone (never copied into this repo) keeps
the licensing boundary explicit and auditable.
