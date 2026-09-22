#!/usr/bin/env bash
# Build the debug APK, install it on a connected tablet, push scenery, and launch EFIS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT/android"
SDK_DIR="${ANDROID_SDK_ROOT:-$ANDROID_DIR/sdk}"
JDK_DIR="${JAVA_HOME:-$ANDROID_DIR/jdk}"
APK="$ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"
PKG=com.efis.app

export JAVA_HOME="$JDK_DIR"
export ANDROID_SDK_ROOT="$SDK_DIR"
export ANDROID_HOME="$SDK_DIR"
export PATH="$JDK_DIR/bin:$SDK_DIR/platform-tools:$PATH"

if [[ ! -x "$ANDROID_DIR/gradlew" || ! -x "$SDK_DIR/platform-tools/adb" ]]; then
    echo "Android SDK or Gradle wrapper missing. Run ./android/setup.sh first." >&2
    exit 1
fi

pick_tablet() {
    adb devices | awk 'NR>1 && $2=="device" && $1 !~ /^emulator/{print $1}'
}

if [[ -n "${ANDROID_SERIAL:-}" ]]; then
    SERIAL="$ANDROID_SERIAL"
    if ! adb -s "$SERIAL" get-state >/dev/null 2>&1; then
        echo "device $SERIAL is not connected" >&2
        exit 1
    fi
else
    mapfile -t TABLETS < <(pick_tablet)
    if [[ ${#TABLETS[@]} -eq 0 ]]; then
        echo "no tablet found. Plug it in, enable USB debugging, and accept the computer prompt." >&2
        adb devices >&2 || true
        exit 1
    fi
    SERIAL="${TABLETS[0]}"
    if [[ ${#TABLETS[@]} -gt 1 ]]; then
        echo "several tablets connected; using $SERIAL"
        echo "set ANDROID_SERIAL to choose another"
    fi
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
