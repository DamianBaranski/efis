#!/usr/bin/env bash
# Download Android SDK/NDK, SDL, and generate the Gradle wrapper.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$ROOT/android"
SDK_DIR="${ANDROID_SDK_ROOT:-$ANDROID_DIR/sdk}"
JDK_DIR="${JAVA_HOME_OVERRIDE:-$ANDROID_DIR/jdk}"
THIRD="$ANDROID_DIR/third_party"
API=34
NDK_VER="26.3.11579264"
CMAKE_VER="3.22.1"
SDL2_VER="2.30.11"
SDL2_IMAGE_VER="2.8.8"
SDL2_TTF_VER="2.24.0"
GLM_VER="1.0.1"
GRADLE_VER="8.7"
CMDLINE_ZIP="commandlinetools-linux-11076708_latest.zip"

mkdir -p "$SDK_DIR" "$THIRD"

download() {
    local url="$1"
    local dest="$2"
    if [[ -f "$dest" ]]; then
        return 0
    fi
    echo "download $url"
    curl -L --fail --retry 3 -o "$dest" "$url"
}

if [[ ! -x "$JDK_DIR/bin/jlink" ]]; then
    echo "download Temurin JDK 21"
    tmp="$ANDROID_DIR/.tmp/jdk"
    mkdir -p "$tmp"
    download "https://api.adoptium.net/v3/binary/latest/21/ga/linux/x64/jdk/hotspot/normal/eclipse?project=jdk" "$tmp/jdk.tgz"
    rm -rf "$JDK_DIR"
    mkdir -p "$JDK_DIR"
    tar -xzf "$tmp/jdk.tgz" -C "$tmp"
    extracted="$(find "$tmp" -maxdepth 1 -type d -name 'jdk-21*' | head -n 1)"
    shopt -s dotglob
    mv "$extracted"/* "$JDK_DIR/"
    shopt -u dotglob
fi
export JAVA_HOME="$JDK_DIR"
export PATH="$JDK_DIR/bin:$PATH"

if [[ ! -x "$SDK_DIR/cmdline-tools/latest/bin/sdkmanager" ]]; then
    tmp="$ANDROID_DIR/.tmp"
    mkdir -p "$tmp"
    download "https://dl.google.com/android/repository/$CMDLINE_ZIP" "$tmp/$CMDLINE_ZIP"
    rm -rf "$tmp/cmdline-tools"
    unzip -q -o "$tmp/$CMDLINE_ZIP" -d "$tmp"
    mkdir -p "$SDK_DIR/cmdline-tools"
    rm -rf "$SDK_DIR/cmdline-tools/latest"
    mv "$tmp/cmdline-tools" "$SDK_DIR/cmdline-tools/latest"
fi

SDKMANAGER="$SDK_DIR/cmdline-tools/latest/bin/sdkmanager"
set +o pipefail
yes | "$SDKMANAGER" --sdk_root="$SDK_DIR" --licenses >/dev/null
set -o pipefail
"$SDKMANAGER" --sdk_root="$SDK_DIR" \
    "platform-tools" \
    "platforms;android-${API}" \
    "build-tools;${API}.0.0" \
    "ndk;${NDK_VER}" \
    "cmake;${CMAKE_VER}" \
    "emulator" \
    "system-images;android-${API};google_apis;x86_64"

unpack_src() {
    local name="$1"
    local url="$2"
    local dir="$THIRD/$name"
    if [[ -f "$dir/CMakeLists.txt" ]]; then
        return 0
    fi
    local zip="$THIRD/${name}.zip"
    download "$url" "$zip"
    rm -rf "$dir"
    unzip -q -o "$zip" -d "$THIRD"
    local extracted
    extracted="$(find "$THIRD" -maxdepth 1 -type d -name "${name}-*" | head -n 1)"
    if [[ -n "$extracted" && "$extracted" != "$dir" ]]; then
        mv "$extracted" "$dir"
    fi
}

unpack_src SDL2 "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VER}/SDL2-${SDL2_VER}.zip"
unpack_src SDL2_image "https://github.com/libsdl-org/SDL_image/releases/download/release-${SDL2_IMAGE_VER}/SDL2_image-${SDL2_IMAGE_VER}.zip"
unpack_src SDL2_ttf "https://github.com/libsdl-org/SDL_ttf/releases/download/release-${SDL2_TTF_VER}/SDL2_ttf-${SDL2_TTF_VER}.zip"
unpack_src glm "https://github.com/g-truc/glm/archive/refs/tags/${GLM_VER}.zip"

JAVA_SRC="$ANDROID_DIR/app/src/main/java/org/libsdl/app"
if [[ ! -f "$JAVA_SRC/SDLActivity.java" ]]; then
    mkdir -p "$JAVA_SRC"
    cp -a "$THIRD/SDL2/android-project/app/src/main/java/org/libsdl/app/." "$JAVA_SRC/"
fi

{
    printf 'sdk.dir=%s\n' "$SDK_DIR"
    printf 'org.gradle.java.home=%s\n' "$JDK_DIR"
} > "$ANDROID_DIR/local.properties"

python3 "$ROOT/scripts/android-pack-assets.py"

if [[ ! -x "$ANDROID_DIR/gradlew" ]]; then
    gradle_zip="$THIRD/gradle-${GRADLE_VER}-bin.zip"
    download "https://services.gradle.org/distributions/gradle-${GRADLE_VER}-bin.zip" "$gradle_zip"
    rm -rf "$THIRD/gradle-${GRADLE_VER}"
    unzip -q -o "$gradle_zip" -d "$THIRD"
    "$THIRD/gradle-${GRADLE_VER}/bin/gradle" -p "$ANDROID_DIR" wrapper --gradle-version "$GRADLE_VER"
fi

echo
echo "Android SDK: $SDK_DIR"
echo "Build with:"
echo "  cd android && ./gradlew assembleDebug"
echo "APK: android/app/build/outputs/apk/debug/app-debug.apk"
