#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: tools/prepare-sidestore-ipa.sh input.ipa output.ipa" >&2
  exit 2
fi

magnus_input=$1
magnus_output=$2
magnus_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
magnus_entitlements="$magnus_script_dir/../ios/Magnus.entitlements"

if [ ! -f "$magnus_input" ]; then
  echo "ERROR: input IPA not found: $magnus_input" >&2
  exit 2
fi
if [ ! -f "$magnus_entitlements" ]; then
  echo "ERROR: entitlement template not found: $magnus_entitlements" >&2
  exit 2
fi

for command_name in unzip zip codesign plutil perl grep; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "ERROR: required command is unavailable: $command_name" >&2
    exit 2
  fi
done

magnus_tmp=$(mktemp -d "${TMPDIR:-/tmp}/magnus-sidestore.XXXXXX")
trap 'rm -rf -- "$magnus_tmp"' EXIT HUP INT TERM

unzip -q "$magnus_input" -d "$magnus_tmp/package"
magnus_app=$(find "$magnus_tmp/package/Payload" -maxdepth 1 -type d -name '*.app' -print | head -n 1)
if [ -z "$magnus_app" ]; then
  echo "ERROR: input IPA has no Payload/*.app" >&2
  exit 1
fi

# A stale profile belongs to a different account. SideStore creates and embeds the final profile.
if [ -f "$magnus_app/embedded.mobileprovision" ]; then
  rm -f -- "$magnus_app/embedded.mobileprovision"
fi

# Remove compiler-embedded build-machine source paths before public redistribution. Each replacement
# has exactly the same byte length, so this does not move Mach-O data or offsets.
magnus_executable=$(/usr/bin/plutil -extract CFBundleExecutable raw "$magnus_app/Info.plist")
magnus_binary="$magnus_app/$magnus_executable"
if [ ! -f "$magnus_binary" ]; then
  echo "ERROR: app executable not found: $magnus_binary" >&2
  exit 1
fi
LC_ALL=C /usr/bin/perl -0pi -e \
  's{(/Users/[A-Za-z0-9._-]+/Documents/[A-Za-z0-9._-]+)}{"/build/" . ("x" x (length($1) - 7))}ge' \
  "$magnus_binary"
if LC_ALL=C /usr/bin/grep -a -q '/Users/' "$magnus_binary"; then
  echo "ERROR: executable still contains an absolute /Users path" >&2
  exit 1
fi

/usr/bin/codesign --force --sign - --timestamp=none --entitlements "$magnus_entitlements" "$magnus_app"

magnus_output_dir=$(dirname -- "$magnus_output")
mkdir -p -- "$magnus_output_dir"
rm -f -- "$magnus_output"
(
  cd "$magnus_tmp/package"
  /usr/bin/zip -qry "$magnus_output" Payload
)

echo "Created: $magnus_output"
echo "SideStore must perform the final developer-account signing and provisioning step."
