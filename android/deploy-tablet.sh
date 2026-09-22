#!/usr/bin/env bash
## Build the debug APK, install it on the tablet over Wi-Fi, push scenery, and launch EFIS.
## The tablet address is remembered in android/.tablet-wlan.
## First contact can be a USB cable: the script switches that tablet to port 5555.
## Later runs use Wi-Fi only. Override the address with EFIS_TABLET_HOST=192.168.x.x
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

## True when the serial is an ip:port pair.
## \param serial adb serial.
is_wireless() {
    [[ "$1" == *:* ]]
}

## True when adb reports the serial as online.
## \param serial adb serial, USB or ip:port.
device_online() {
    [[ "$(adb -s "$1" get-state 2>/dev/null | tr -d '\r')" == "device" ]]
}

## Connect adb to the tablet.
## Prints ip:port on stdout. Progress goes to stderr.
## \param host Address with or without a port.
connect_host() {
    local host="$1"
    if [[ "$host" != *:* ]]; then
        host="${host}:${PORT}"
    fi
    echo "Connecting to $host" >&2
    adb connect "$host" >/dev/null || true
    local i
    for i in $(seq 1 20); do
        if device_online "$host"; then
            printf '%s\n' "$host"
            return 0
        fi
        sleep 0.4
    done
    return 1
}

## IPv4 address of the tablet wlan interface.
## Prints the address on stdout.
## \param serial USB adb serial.
wlan_ip_of() {
    adb -s "$1" shell 'ip -4 -o addr show' 2>/dev/null | tr -d '\r' |
        awk '$2 ~ /^wlan/ && $3 == "inet" { split($4, a, "/"); print a[1]; exit }'
}

## First online ip:port from adb devices, printed on stdout.
wireless_online() {
    adb devices | awk 'NR > 1 && $2 == "device" && $1 ~ /:/ { print $1; exit }'
}

## First online USB serial that is not the emulator, printed on stdout.
usb_online() {
    adb devices | awk 'NR > 1 && $2 == "device" && $1 !~ /^emulator/ && $1 !~ /:/ { print $1; exit }'
}

## First adb mDNS address, printed on stdout.
mdns_host() {
    adb mdns services 2>/dev/null | awk '
        $2 == "_adb-tls-connect._tcp" || $2 == "_adb._tcp" { print $3; exit }
    '
}

## Store the serial in android/.tablet-wlan.
## \param host ip:port.
remember() {
    printf '%s\n' "$1" >"$HOST_FILE"
}

## Switch a USB tablet to port 5555 and print the ip:port.
## \param usb USB adb serial.
promote_usb() {
    local usb="$1"
    local ip host
    ip="$(wlan_ip_of "$usb")"
    if [[ -z "$ip" ]]; then
        echo "tablet $usb has no Wi-Fi address. Join the same network as this computer." >&2
        return 1
    fi
    echo "Switching $usb to Wi-Fi at ${ip}:${PORT}" >&2
    adb -s "$usb" tcpip "$PORT" >&2
    sleep 1
    host="$(connect_host "$ip")" || return 1
    remember "$host"
    printf '%s\n' "$host"
}

## Choose the tablet.
## Prints ip:port on stdout and nothing else. Status goes to stderr.
pick_tablet() {
    local host="" serial="" usb=""

    if [[ -n "${EFIS_TABLET_HOST:-}" ]]; then
        serial="$(connect_host "$EFIS_TABLET_HOST")" || return 1
        remember "$serial"
        printf '%s\n' "$serial"
        return 0
    fi

    if [[ -n "${ANDROID_SERIAL:-}" ]] && is_wireless "$ANDROID_SERIAL"; then
        serial="$(connect_host "$ANDROID_SERIAL")" || return 1
        remember "$serial"
        printf '%s\n' "$serial"
        return 0
    fi

    serial="$(wireless_online || true)"
    if [[ -n "$serial" ]]; then
        printf '%s\n' "$serial"
        return 0
    fi

    if [[ -s "$HOST_FILE" ]]; then
        host="$(head -n 1 "$HOST_FILE")"
        if serial="$(connect_host "$host")"; then
            printf '%s\n' "$serial"
            return 0
        fi
        echo "saved tablet $host did not answer" >&2
    fi

    host="$(mdns_host || true)"
    if [[ -n "$host" ]] && serial="$(connect_host "$host")"; then
        remember "$serial"
        printf '%s\n' "$serial"
        return 0
    fi

    usb="${ANDROID_SERIAL:-}"
    if [[ -z "$usb" ]] || is_wireless "$usb"; then
        usb="$(usb_online || true)"
    fi
    if [[ -n "$usb" ]] && device_online "$usb"; then
        promote_usb "$usb"
        return
    fi
    return 1
}

SERIAL="$(pick_tablet | tr -d '\r' | awk '/^[0-9.]+:[0-9]+$/ { host = $0 } END { if (host != "") print host }')" || true
if [[ -z "$SERIAL" ]]; then
    echo "no tablet on Wi-Fi." >&2
    echo "Use the same network, then either:" >&2
    echo "  EFIS_TABLET_HOST=192.168.x.x ./android/deploy-tablet.sh" >&2
    echo "or plug the tablet in once so adb can switch to port ${PORT}." >&2
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
