# Draft: permission request to KytyPS5 authors (do not send as-is)

Subject: Request to combine KytyPS5 (GPL-2.0) with GPL-3.0 iOS JIT code in an iOS port

Hello KytyPS5 team,

I maintain an iOS port of KytyPS5 ("MagnusPS5", fork of the
BaconMakin/hernan0078 iOS work, tracking your `main`). Running PS5 guest
code on an ARM64 iPhone requires an x86-64 JIT, for which the port links
the iOS-capable FEX-Emu fork from the Madeira project
(`https://github.com/willfaust/FEX`). That fork's iOS modifications are
licensed GPL-3.0-or-later, while KytyPS5 is GPL-2.0-only.

The combination cannot satisfy both licences at once for a distributed
binary. May I have your permission to distribute iOS build artefacts of
this port that combine KytyPS5 with GPL-3.0-covered FEX code (with full
source disclosure of the fork as required)? If a narrower grant is easier
(e.g. “GPL-2.0-or-later for the files used by the iOS port”), that also
resolves it.

Repo/branch: `https://github.com/ZEENEE-MASTER/MagnusPS5`
(`kyty-sync-2026-09-20`, tracks your `ba55ba5`). FEX use is a pinned
external clone, never copied into the fork; see
`LICENSES/FEX-LICENSE-NOTICE.md`.

Thank you for KytyPS5.
