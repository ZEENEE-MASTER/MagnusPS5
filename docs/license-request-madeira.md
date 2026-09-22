# Draft: permission request to Will Faust / Madeira (do not send as-is)

Subject: Request to use FEX iOS-port bits under MIT terms in a PS5-emulator iOS port

Hello Will,

I am building an iOS port of the KytyPS5 PlayStation 5 emulator
("MagnusPS5", `https://github.com/ZEENEE-MASTER/MagnusPS5`), which needs an
x86-64 JIT on ARM64 iPhones. Your FEX iOS fork
(`https://github.com/willfaust/FEX`, used at pin `053c385`) is the only
public iOS-capable FEX, and its embedding pattern (FEXBridge, JIT pool,
`FEX_IOS_HOST`) is the reference for our Stinger bridge. Thank you for
publishing it.

The blocker is licensing, not technical: KytyPS5 is GPL-2.0-only, while
your fork's Madeira-authored modifications are GPL-3.0-or-later, so a
distributed binary combining them cannot satisfy both licences. Would you
grant use of the FEX iOS-port bits under MIT terms (matching upstream
FEX-Emu), or another arrangement that permits combination with GPL-2.0-only
code? Our use keeps FEX as a pinned external clone; our own bridge/UI code
is separate.

If a narrower grant is easier (e.g. covering only the iOS-support hunks),
that is enough for this build.

Thank you for Madeira — it proved this class of port is possible.
