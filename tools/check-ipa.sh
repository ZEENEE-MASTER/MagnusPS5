#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: tools/check-ipa.sh /path/to/Magnus.ipa" >&2
  exit 2
fi

magnus_ipa=$1
if [ ! -f "$magnus_ipa" ]; then
  echo "ERROR: not a file: $magnus_ipa" >&2
  exit 2
fi

for command_name in unzip codesign plutil file strings grep; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "ERROR: required command is unavailable: $command_name" >&2
    exit 2
  fi
done

magnus_tmp=$(mktemp -d "${TMPDIR:-/tmp}/magnus-ipa-check.XXXXXX")
trap 'rm -rf -- "$magnus_tmp"' EXIT HUP INT TERM

unzip -q "$magnus_ipa" -d "$magnus_tmp/unpacked"
magnus_app=$(find "$magnus_tmp/unpacked/Payload" -maxdepth 1 -type d -name '*.app' -print | head -n 1)
if [ -z "$magnus_app" ]; then
  echo "ERROR: IPA has no Payload/*.app" >&2
  exit 1
fi

magnus_info="$magnus_app/Info.plist"
magnus_executable_name=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$magnus_info")
magnus_executable="$magnus_app/$magnus_executable_name"
magnus_entitlements="$magnus_tmp/entitlements.plist"
/usr/bin/codesign -d --entitlements :- "$magnus_app" >"$magnus_entitlements" 2>/dev/null || true

plist_value() {
  /usr/libexec/PlistBuddy -c "Print :$1" "$2" 2>/dev/null || true
}

echo "MagnusPS5 signed-IPA check"
echo "App: $(basename "$magnus_app")"
echo "Bundle ID: $(plist_value CFBundleIdentifier "$magnus_info")"
echo "Minimum iOS: $(plist_value MinimumOSVersion "$magnus_info")"
echo "Executable: $(file -b "$magnus_executable")"

magnus_failed=0

if LC_ALL=C grep -a -q '/Users/' "$magnus_executable"; then
  echo "FAIL privacy: executable contains an absolute /Users path"
  magnus_failed=1
else
  echo "PASS privacy: executable contains no absolute /Users path"
fi

for entitlement_key in \
  com.apple.developer.kernel.extended-virtual-addressing \
  com.apple.developer.kernel.increased-memory-limit \
  com.apple.developer.kernel.increased-debugging-memory-limit \
  get-task-allow
do
  entitlement_value=$(plist_value "$entitlement_key" "$magnus_entitlements")
  if [ "$entitlement_value" = "true" ]; then
    echo "PASS entitlement: $entitlement_key"
  else
    echo "FAIL entitlement: $entitlement_key (value=${entitlement_value:-missing})"
    magnus_failed=1
  fi
done

if [ -f "$magnus_app/jit.js" ]; then
  echo "PASS JIT script: bundled jit.js"
else
  echo "FAIL JIT script: jit.js is missing"
  magnus_failed=1
fi

if strings -a "$magnus_executable" | grep -q 'stikdebug://enable-jit'; then
  echo "PASS StikDebug integration: enable-jit URL request is present"
else
  echo "FAIL StikDebug integration: enable-jit URL request is absent"
  magnus_failed=1
fi

if [ -f "$magnus_app/embedded.mobileprovision" ]; then
  echo "PASS provisioning: embedded.mobileprovision is present"
else
  echo "WARNING provisioning: no embedded.mobileprovision; an ad-hoc upstream IPA must be re-signed"
fi

codesign -dvv "$magnus_app" 2>&1 | grep -E '^(Identifier|TeamIdentifier|Signature)=' || true

if [ "$magnus_failed" -eq 0 ]; then
  echo "RESULT: REQUIRED MAGNUS ENTITLEMENTS AND JIT HOOK ARE PRESENT"
  echo "NOTE: successful installation/runtime authorization still depends on the provisioning profile."
  exit 0
fi

echo "RESULT: IPA IS MISSING A REQUIRED MAGNUS CAPABILITY"
exit 1
