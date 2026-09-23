#!/usr/bin/env bash
## Shared adb connect for the phone and the tablet.
## Wi-Fi is tried first. If that address does not answer, the script uses a
## USB device. When that device has a Wi-Fi address, wireless debugging is
## turned on so the next run can use the network again.
## This file is sourced. It is not run on its own.
## The caller sets PORT, usually 5555.

## True when adb reports the serial as online.
## \param serial adb serial, USB or ip:port.
device_online() {
    [[ "$(adb -s "$1" get-state 2>/dev/null | tr -d '\r')" == "device" ]]
}

## Connect adb over Wi-Fi.
## Prints ip:port on stdout. Progress goes to stderr.
## \param host Address with or without a port.
connect_host() {
    local host="$1"
    local i
    if [[ "$host" != *:* ]]; then
        host="${host}:${PORT}"
    fi
    echo "Connecting to $host" >&2
    adb connect "$host" >/dev/null 2>&1 || true
    for i in $(seq 1 8); do
        if device_online "$host"; then
            printf '%s\n' "$host"
            return 0
        fi
        sleep 0.4
    done
    return 1
}

## IPv4 address of the device wlan interface.
## Prints the address on stdout.
## \param serial USB adb serial.
wlan_ip_of() {
    adb -s "$1" shell 'ip -4 -o addr show' 2>/dev/null | tr -d '\r' |
        awk '$2 ~ /^(wlan|wifi)/ && $3 == "inet" { split($4, a, "/"); print a[1]; exit }' || true
}

## Online USB serials, one per line. Skips the emulator and ip:port devices.
usb_serials() {
    adb devices 2>/dev/null | awk 'NR > 1 && $2 == "device" && $1 !~ /^emulator/ && $1 !~ /:/ { print $1 }'
}

## First online ip:port, printed on stdout.
wireless_online() {
    adb devices 2>/dev/null | awk 'NR > 1 && $2 == "device" && $1 ~ /:/ { print $1; exit }'
}

## First adb mDNS address, printed on stdout.
mdns_host() {
    adb mdns services 2>/dev/null | awk '
        $2 == "_adb-tls-connect._tcp" || $2 == "_adb._tcp" { print $3; exit }
    ' || true
}

## Store the serial.
## \param path File that remembers ip:port.
## \param host ip:port.
remember_file() {
    printf '%s\n' "$2" >"$1"
}

## Use a USB device. Enables wireless debugging when a Wi-Fi address exists.
## Prints the serial to use (ip:port, or the USB serial if Wi-Fi still does not answer).
## \param usb USB adb serial.
## \param path File that remembers ip:port.
## \param label phone or tablet, used in messages.
use_usb() {
    local usb="$1"
    local path="$2"
    local label="$3"
    local ip host i
    ip="$(wlan_ip_of "$usb")"
    if [[ -n "$ip" ]]; then
        echo "Wi-Fi adb is down. Enabling wireless debugging on USB $usb at ${ip}:${PORT}" >&2
        if adb -s "$usb" tcpip "$PORT" >&2; then
            sleep 1
            if host="$(connect_host "$ip")"; then
                remember_file "$path" "$host"
                printf '%s\n' "$host"
                return 0
            fi
        fi
        echo "$label did not answer on ${ip}:${PORT}. Using USB $usb." >&2
    else
        echo "$label $usb has no Wi-Fi address. Using USB." >&2
    fi
    for i in $(seq 1 15); do
        if device_online "$usb"; then
            printf '%s\n' "$usb"
            return 0
        fi
        sleep 0.4
    done
    echo "$label $usb dropped after wireless debugging. Plug the cable in again." >&2
    return 1
}

## Pick a USB device when Wi-Fi did not answer.
## Prints the serial on stdout.
## \param label phone or tablet.
## \param want IPv4 address to prefer when more than one device is plugged in. May be empty.
## \param path File that remembers ip:port.
failover_usb() {
    local label="$1"
    local want="$2"
    local path="$3"
    local usb ip chosen="" only="" count=0
    want="${want%%:*}"
    if [[ -n "${ANDROID_SERIAL:-}" && "${ANDROID_SERIAL}" != *:* ]] && device_online "${ANDROID_SERIAL}"; then
        use_usb "${ANDROID_SERIAL}" "$path" "$label"
        return
    fi
    while IFS= read -r usb; do
        [[ -z "$usb" ]] && continue
        count=$((count + 1))
        only="$usb"
        ip="$(wlan_ip_of "$usb")"
        if [[ -n "$want" && "$ip" == "$want" ]]; then
            chosen="$usb"
            break
        fi
    done < <(usb_serials)
    if [[ -z "$chosen" && "$count" -eq 1 ]]; then
        chosen="$only"
    fi
    if [[ -z "$chosen" ]]; then
        if [[ "$count" -eq 0 ]]; then
            echo "no $label on Wi-Fi or USB." >&2
        else
            echo "several USB devices are attached. Plug in only the $label." >&2
        fi
        return 1
    fi
    use_usb "$chosen" "$path" "$label"
}

## Connect over Wi-Fi, then USB if that address does not answer.
## Prints the adb serial on stdout and nothing else.
## \param label phone or tablet.
## \param preferred Address to try first. Empty tries a device already on Wi-Fi, then mDNS, then USB.
## \param path File that remembers ip:port.
pick_target() {
    local label="$1"
    local preferred="$2"
    local path="$3"
    local serial="" host=""
    if [[ -n "$preferred" ]]; then
        if serial="$(connect_host "$preferred")"; then
            remember_file "$path" "$serial"
            printf '%s\n' "$serial"
            return 0
        fi
        echo "$label $preferred did not answer on port ${PORT}" >&2
        if [[ -s "$path" ]]; then
            host="$(head -n 1 "$path" | tr -d '\r')"
            if [[ -n "$host" && "${host%%:*}" != "${preferred%%:*}" ]] && serial="$(connect_host "$host")"; then
                printf '%s\n' "$serial"
                return 0
            fi
        fi
    else
        host="$(wireless_online || true)"
        if [[ -n "$host" ]]; then
            remember_file "$path" "$host"
            printf '%s\n' "$host"
            return 0
        fi
        if [[ -s "$path" ]]; then
            host="$(head -n 1 "$path" | tr -d '\r')"
            if [[ -n "$host" ]] && serial="$(connect_host "$host")"; then
                printf '%s\n' "$serial"
                return 0
            fi
        fi
        host="$(mdns_host || true)"
        if [[ -n "$host" ]] && serial="$(connect_host "$host")"; then
            remember_file "$path" "$serial"
            printf '%s\n' "$serial"
            return 0
        fi
    fi
    failover_usb "$label" "$preferred" "$path"
}
