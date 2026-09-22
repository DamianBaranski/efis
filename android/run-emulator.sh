#!/usr/bin/env bash
## Create the local EFIS emulator if needed, then install the debug APK.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT/android"
SDK_DIR="${ANDROID_SDK_ROOT:-$ANDROID_DIR/sdk}"
JDK_DIR="${JAVA_HOME:-$ANDROID_DIR/jdk}"
AVD_HOME="${ANDROID_AVD_HOME:-$ANDROID_DIR/avd}"
AVD_NAME="${EFIS_AVD_NAME:-efis}"
API=34
IMAGE="system-images;android-${API};google_apis;x86_64"
APK="$ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"

export JAVA_HOME="$JDK_DIR"
export ANDROID_SDK_ROOT="$SDK_DIR"
export ANDROID_HOME="$SDK_DIR"
export ANDROID_AVD_HOME="$AVD_HOME"
export ANDROID_EMU_ENABLE_CRASH_REPORTING=0
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
export PATH="$JDK_DIR/bin:$SDK_DIR/emulator:$SDK_DIR/platform-tools:$SDK_DIR/cmdline-tools/latest/bin:$PATH"

if [[ ! -x "$SDK_DIR/cmdline-tools/latest/bin/sdkmanager" ]]; then
    echo "SDK missing. Run ./android/setup.sh first." >&2
    exit 1
fi

if [[ ! -x "$SDK_DIR/emulator/emulator" || ! -d "$SDK_DIR/system-images/android-${API}/google_apis/x86_64" ]]; then
    echo "Installing emulator and Android ${API} x86_64 system image"
    set +o pipefail
    yes | sdkmanager --sdk_root="$SDK_DIR" --licenses >/dev/null
    set -o pipefail
    sdkmanager --sdk_root="$SDK_DIR" "emulator" "$IMAGE"
fi

mkdir -p "$AVD_HOME"
if [[ ! -f "$AVD_HOME/${AVD_NAME}.ini" ]]; then
    echo "Creating AVD ${AVD_NAME}"
    device="pixel_6"
    if ! avdmanager list device 2>/dev/null | grep -q "pixel_6"; then
        device="pixel"
    fi
    echo no | avdmanager create avd \
        --force \
        --name "$AVD_NAME" \
        --package "$IMAGE" \
        --device "$device"
    cat >> "$AVD_HOME/${AVD_NAME}.avd/config.ini" <<'EOF'
hw.gpu.enabled=yes
hw.gpu.mode=swiftshader_indirect
hw.ramSize=4096
hw.keyboard=yes
hw.lcd.density=400
EOF
fi

if ! adb devices | awk 'NR>1 && $2=="device"{found=1} END{exit !found}'; then
    echo "Starting emulator ${AVD_NAME}"
    gpu="${EFIS_EMU_GPU:-swiftshader_indirect}"
    : > "$ANDROID_DIR/emulator.log"
    # New session so closing the setup terminal does not kill qemu.
    setsid -f env \
        QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}" \
        ANDROID_SDK_ROOT="$SDK_DIR" \
        ANDROID_HOME="$SDK_DIR" \
        ANDROID_AVD_HOME="$AVD_HOME" \
        emulator -avd "$AVD_NAME" -gpu "$gpu" -no-snapshot -no-boot-anim \
            -netdelay none -netspeed full >> "$ANDROID_DIR/emulator.log" 2>&1
    sleep 1
    pgrep -n -f "qemu-system.*${AVD_NAME}|emulator.*-avd ${AVD_NAME}" > "$ANDROID_DIR/emulator.pid" || true
    echo "Waiting for emulator to boot (gpu=${gpu})"
    adb wait-for-device
    for _ in $(seq 1 120); do
        if [[ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" == "1" ]]; then
            break
        fi
        if ! pgrep -f "qemu-system" >/dev/null; then
            echo "Emulator exited. Last log lines:" >&2
            tail -n 40 "$ANDROID_DIR/emulator.log" >&2
            exit 1
        fi
        sleep 2
    done
fi

if [[ ! -f "$APK" ]]; then
    echo "Building debug APK"
    (cd "$ANDROID_DIR" && ./gradlew assembleDebug)
fi

adb install -r "$APK"
adb shell am start -n com.efis.app/com.efis.app.EfisActivity
echo "EFIS installed and launched on ${AVD_NAME}"
echo "Log: $ANDROID_DIR/emulator.log"
