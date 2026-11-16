#!/usr/bin/env bash
# ProjectionService probe build. Usage: build.sh apk
#   apk : compile the Android shell against android.jar, dex, link manifest, sign with a throwaway debug key.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${ANDROIDPLAY_BUILD_DIR:-$HERE/../../.build}/projection"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
mkdir -p "$OUT"

case "${1:-}" in
apk)
  BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
  JAR="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar"
  [ -f "$JAR" ] || { echo "android.jar not found under $SDK/platforms"; exit 1; }
  rm -rf "$OUT/classes" "$OUT/dex" "$OUT/apk" && mkdir -p "$OUT/classes" "$OUT/dex"
  javac --release 17 -Xlint:all -cp "$JAR" -d "$OUT/classes" $(find "$HERE/src" -name '*.java')
  "$BT/d8" --min-api 29 --lib "$JAR" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')
  "$BT/aapt2" link --manifest "$HERE/AndroidManifest.xml" -I "$JAR" --min-sdk-version 29 --target-sdk-version 35 --debug-mode -o "$OUT/base.apk"
  cp "$OUT/base.apk" "$OUT/unsigned.apk"
  (cd "$OUT/dex" && zip -q -j "$OUT/unsigned.apk" classes.dex)
  # native sender (build first: services/CarPlayService/android/build.sh); stored uncompressed + page-aligned for direct loading
  JNI="$HERE/../../.build/jni"
  for abi in x86_64 arm64-v8a; do
    if [ -f "$JNI/$abi/libs10carplay.so" ]; then mkdir -p "$OUT/apk/lib/$abi" && cp "$JNI/$abi/libs10carplay.so" "$OUT/apk/lib/$abi/"; fi
  done
  [ -d "$OUT/apk/lib" ] && (cd "$OUT/apk" && zip -q -0 -r "$OUT/unsigned.apk" lib)
  "$BT/zipalign" -p -f 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"
  KS="$OUT/debug.keystore"
  [ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias s10debug -keyalg RSA -keysize 2048 -validity 3650 -dname "CN=AndroidPlay debug" >/dev/null 2>&1
  "$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android --out "$OUT/ProjectionService-debug.apk" "$OUT/aligned.apk"
  "$BT/apksigner" verify "$OUT/ProjectionService-debug.apk"
  echo "$OUT/ProjectionService-debug.apk"
  ;;
*) echo "usage: $0 apk"; exit 2;;
esac
