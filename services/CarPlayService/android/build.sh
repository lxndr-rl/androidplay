#!/usr/bin/env bash
# Build libs10carplay.so with the Android NDK for the emulator (x86_64) and the S10+ (arm64-v8a).
# Output: .build/jni/<abi>/libs10carplay.so
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
NDK="${ANDROID_NDK_ROOT:-$(ls -d "$SDK"/ndk/* | sort -V | tail -1)}"
CMAKE="$(ls "$SDK"/cmake/*/bin/cmake | tail -1)"; NINJA="$(dirname "$CMAKE")/ninja"
OUT="${S10CAROS_BUILD_DIR:-$HERE/../../../.build}/jni"
for ABI in ${ABIS:-x86_64 arm64-v8a}; do
  "$CMAKE" -S "$HERE" -B "$OUT/$ABI-build" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-29 \
    -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-Wall -Wextra" >/dev/null
  "$CMAKE" --build "$OUT/$ABI-build" >/dev/null
  mkdir -p "$OUT/$ABI" && cp "$OUT/$ABI-build/libs10carplay.so" "$OUT/$ABI/"
  echo "$OUT/$ABI/libs10carplay.so"
done
