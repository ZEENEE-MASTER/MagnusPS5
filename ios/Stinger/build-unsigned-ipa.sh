#!/bin/sh
# Build unsigned MagnusPS5 IPA on a macOS runner (no paid signing).
# Links the ios-core static lib + 3rdparty archives + MoltenVK iphoneos slice
# with a programmatic UIKit front-end. The user re-signs with their own tool
# (Sideloadly/AltStore/SideStore + Apple ID); JIT/memory entitlements are
# applied at that step, not here. No secrets, certs, or provisioning here.
set -eu

DEPLOYMENT_TARGET="${MAGNUS_DEPLOYMENT_TARGET:-17.4}"
APP_NAME="${MAGNUS_APP_NAME:-MagnusPS5}"
BUNDLE_ID="${MAGNUS_BUNDLE_ID:-Com.Mercury.Magnus}"

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build-unsigned}"
OBJ="$OUT/obj"
STAGE="$OUT/stage/Payload/$APP_NAME.app"
mkdir -p "$OBJ" "$STAGE"

SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
echo "SDK=$SDK"
clang --version | head -n 2

# 1. Compile Stinger UI (ObjC only, ARC, programmatic UI).
for src in "$ROOT/ios/Stinger/main.m"; do
  obj="$OBJ/$(basename "$src" .m).o"
  echo "COMPILE $(basename "$src")"
  clang -target "arm64-apple-ios$DEPLOYMENT_TARGET" -fobjc-arc -O2 \
    -isysroot "$SDK" -I "$ROOT/src" \
    -c "$src" -o "$obj"
done
OBJS="$OBJ/main.o"

# 2. Locate archives: core + everything the core built (SDL2, SPIRV, fmt...).
CORE_LIB="$(find "$ROOT/build-ios" -name 'libkyty_emulator.a' | head -n 1)"
if [ -z "$CORE_LIB" ]; then
  echo "ERROR: libkyty_emulator.a not found under $ROOT/build-ios" >&2
  exit 1
fi
THIRD_LIBS="$(find "$ROOT/build-ios" -name '*.a' ! -name 'libkyty_emulator.a')"
MOLTENVK_LIB="$(find "$ROOT/../MoltenVK" -path '*ios-arm64*libMoltenVK.a' | head -n 1)"
if [ -z "$MOLTENVK_LIB" ]; then
  echo "ERROR: ios-arm64 libMoltenVK.a not found under $ROOT/../MoltenVK" >&2
  exit 1
fi
echo "CORE=$CORE_LIB"
echo "MOLTENVK=$MOLTENVK_LIB"
echo "$THIRD_LIBS" | head -n 20

# 3. Link (undefined symbols are fatal: no dynamic_lookup suppression).
# shellcheck disable=SC2086
clang -target "arm64-apple-ios$DEPLOYMENT_TARGET" -isysroot "$SDK" \
  $OBJS "$CORE_LIB" $THIRD_LIBS "$MOLTENVK_LIB" \
  -framework Foundation -framework UIKit -framework Metal -framework MetalKit \
  -framework AVFoundation -framework AudioToolbox -framework CoreGraphics \
  -framework CoreMotion -framework QuartzCore -framework GameController \
  -framework CoreHaptics \
  -o "$STAGE/$APP_NAME"
cp "$ROOT/ios/Stinger/Info.plist" "$STAGE/Info.plist"

# 4. Ad-hoc sign for transport only (user re-signs; no entitlements embedded).
codesign --force --sign - "$STAGE" 2>&1 | head -n 5 || true

# 5. Package: executable (0755) + Info.plist (0644) only; verify hashes.
TMP_IPA="$OUT/$APP_NAME-unsigned.new.ipa"
FINAL_IPA="$OUT/$APP_NAME-unsigned.ipa"
rm -f "$TMP_IPA"
python3 - "$STAGE" "$TMP_IPA" <<'EOF'
import os, sys, zipfile
stage, tmp = sys.argv[1], sys.argv[2]
app = os.path.join(stage, "MagnusPS5")
plist = os.path.join(stage, "Info.plist")
with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
    for src, arc, mode in ((app, "Payload/MagnusPS5.app/MagnusPS5", 0o755),
                           (plist, "Payload/MagnusPS5.app/Info.plist", 0o644)):
        zi = zipfile.ZipInfo(arc)
        zi.external_attr = (0o100000 | mode) << 16
        with open(src, "rb") as f:
            z.writestr(zi, f.read())
EOF
python3 - "$STAGE" "$TMP_IPA" <<'EOF'
import hashlib, sys, zipfile
stage, tmp = sys.argv[1], sys.argv[2]
def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        h.update(f.read())
    return h.hexdigest()
with zipfile.ZipFile(tmp) as z:
    names = z.namelist()
    assert sorted(names) == ["Payload/MagnusPS5.app/Info.plist",
                             "Payload/MagnusPS5.app/MagnusPS5"], names
    for n in names:
        disk = stage + "/" + n.split("Payload/MagnusPS5.app/")[1]
        assert sha(disk) == hashlib.sha256(z.read(n)).hexdigest(), n
        print("OK", n, sha(disk))
EOF
rm -f "$FINAL_IPA"
mv "$TMP_IPA" "$FINAL_IPA"
shasum -a 256 "$FINAL_IPA"
ls -l "$FINAL_IPA"
echo "Unsigned IPA ready for your signing tool: $FINAL_IPA"
