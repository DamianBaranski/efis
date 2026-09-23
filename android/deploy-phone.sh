#!/usr/bin/env bash
## Build the debug APK, install it on the phone, push scenery, and launch EFIS.
## Wi-Fi is 192.168.1.7, remembered in android/.phone-wlan.
## If that address does not answer, a USB phone is used instead.
## Override the address with EFIS_PHONE_HOST=192.168.x.x
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT/android"
SDK_DIR="${ANDROID_SDK_ROOT:-$ANDROID_DIR/sdk}"
JDK_DIR="${JAVA_HOME:-$ANDROID_DIR/jdk}"
APK="$ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"
HOST_FILE="$ANDROID_DIR/.phone-wlan"
PKG=com.efis.app
PORT=5555
## Phone on the LAN. Last octet 7, same subnet as the tablet.
DEFAULT_HOST=192.168.1.7

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

HOST="${EFIS_PHONE_HOST:-$DEFAULT_HOST}"
SERIAL="$(pick_target phone "$HOST" "$HOST_FILE" | tr -d '\r')" || true
if [[ -z "$SERIAL" ]]; then
    echo "phone: Wi-Fi $HOST did not answer and no USB phone is online." >&2
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
