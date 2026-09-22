#!/usr/bin/env bash
# Install scenery into the app-private files dir (internal storage).
# Honor/scoped storage blocks adb-pushed trees under Android/data.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_DIR="${ANDROID_SDK_ROOT:-$ROOT/android/sdk}"
export PATH="$SDK_DIR/platform-tools:$PATH"
PKG=com.efis.app

if ! adb get-state >/dev/null 2>&1; then
    echo "no device; start the emulator first (./android/run-emulator.sh)" >&2
    exit 1
fi

SERIAL="${ANDROID_SERIAL:-}"
if [[ -z "$SERIAL" ]]; then
    # Prefer a real phone when the emulator is also attached.
    SERIAL=$(adb devices | awk 'NR>1 && $2=="device" && $1 !~ /^emulator/{print $1; exit}')
    SERIAL="${SERIAL:-$(adb devices | awk 'NR>1 && $2=="device"{print $1; exit}')}"
fi
if [[ -z "$SERIAL" ]]; then
    echo "no adb device" >&2
    exit 1
fi
ADB=(adb -s "$SERIAL")

TAR=$(mktemp /tmp/efis-scenery.XXXXXX.tar)
trap 'rm -f "$TAR"' EXIT
pack=(terrain textures/btg textures/unknown.png)
if [[ -d "$ROOT/resources/openaip/cache" ]]; then
    pack+=(openaip/cache)
fi
if [[ -f "$ROOT/resources/openaip/api.key" ]]; then
    pack+=(openaip/api.key)
fi
if [[ -f "$ROOT/resources/openaip/carto.key" ]]; then
    pack+=(openaip/carto.key)
fi
tar -C "$ROOT/resources" -cf "$TAR" "${pack[@]}"
"${ADB[@]}" push "$TAR" /data/local/tmp/efis-scenery.tar
"${ADB[@]}" shell "run-as $PKG mkdir -p files/resources"
"${ADB[@]}" shell "cat /data/local/tmp/efis-scenery.tar | run-as $PKG tar xf - -C files/resources"
"${ADB[@]}" shell rm -f /data/local/tmp/efis-scenery.tar
echo "scenery extracted into $PKG internal files/ on $SERIAL"
