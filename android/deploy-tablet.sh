#!/usr/bin/env bash
## Build the debug APK, install it on the tablet, push scenery, and launch EFIS.
## The tablet address is remembered in android/.tablet-wlan.
## Wi-Fi is tried first. If it does not answer, a USB tablet is used.
## Override the address with EFIS_TABLET_HOST=192.168.x.x
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT/android"
SDK_DIR="${ANDROID_SDK_ROOT:-$ANDROID_DIR/sdk}"
JDK_DIR="${JAVA_HOME:-$ANDROID_DIR/jdk}"
APK="$ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"
HOST_FILE="$ANDROID_DIR/.tablet-wlan"
PKG=com.efis.app
PORT=5555

export JAVA_HOME="$JDK_DIR"
export ANDROID_SDK_ROOT="$SDK_DIR"
export ANDROID_HOME="$SDK_DIR"
export PATH="$JDK_DIR/bin:$SDK_DIR/platform-tools:$PATH"

if [[ ! -x "$ANDROID_DIR/gradlew" || ! -x "$SDK_DIR/platform-tools/adb" ]]; then
    echo "Android SDK or Gradle wrapper missing. Run ./android/setup.sh first." >&2
    exit 1
fi

# shellcheck source=deploy-adb.sh
source "$ANDROID_DIR/deploy-adb.sh"

HOST="${EFIS_TABLET_HOST:-}"
if [[ -z "$HOST" && -n "${ANDROID_SERIAL:-}" && "${ANDROID_SERIAL}" == *:* ]]; then
    HOST="$ANDROID_SERIAL"
fi
if [[ -z "$HOST" && -s "$HOST_FILE" ]]; then
    HOST="$(head -n 1 "$HOST_FILE" | tr -d '\r')"
fi

SERIAL="$(pick_target tablet "$HOST" "$HOST_FILE" | tr -d '\r')" || true
if [[ -z "$SERIAL" ]]; then
    echo "tablet: Wi-Fi did not answer and no USB tablet is online." >&2
    echo "Use the same network, or plug the tablet in:" >&2
    echo "  EFIS_TABLET_HOST=192.168.x.x ./android/deploy-tablet.sh" >&2
    adb devices >&2 || true
    exit 1
fi

export ANDROID_SERIAL="$SERIAL"
ADB=(adb -s "$SERIAL")

echo "Building debug APK for $SERIAL"
(cd "$ANDROID_DIR" && ./gradlew assembleDebug)

echo "Installing $APK"
"${ADB[@]}" install -r "$APK"

echo "Pushing scenery"
"$ANDROID_DIR/push-scenery.sh"

echo "Launching $PKG"
"${ADB[@]}" shell am start -n "$PKG/$PKG.EfisActivity"
echo "EFIS installed and launched on $SERIAL"
