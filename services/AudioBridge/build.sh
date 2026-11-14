#!/usr/bin/env bash
# AudioBridge probe build. Usage: build.sh dex | tone
#   dex : privileged capture probe (app_process as shell/system) -> .build/audiobridge/capture.dex
#   tone: test-fixture APK that plays a 440 Hz tone -> .build/audiobridge/ToneTest-debug.apk
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${S10CAROS_BUILD_DIR:-$HERE/../../.build}/audiobridge"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
JAR="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar"
mkdir -p "$OUT"
case "${1:-}" in
dex)
  rm -rf "$OUT/classes" "$OUT/dex" && mkdir -p "$OUT/classes" "$OUT/dex"
  javac --release 17 -cp "$JAR" -d "$OUT/classes" $(find "$HERE/src" -name '*.java')
  "$BT/d8" --min-api 31 --lib "$JAR" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')
  cp "$OUT/dex/classes.dex" "$OUT/capture.dex"; echo "$OUT/capture.dex" ;;
tone)
  rm -rf "$OUT/tc" "$OUT/td" && mkdir -p "$OUT/tc" "$OUT/td"
  javac --release 17 -cp "$JAR" -d "$OUT/tc" $(find "$HERE/tonetest/src" -name '*.java')
  "$BT/d8" --min-api 29 --lib "$JAR" --output "$OUT/td" $(find "$OUT/tc" -name '*.class')
  "$BT/aapt2" link --manifest "$HERE/tonetest/AndroidManifest.xml" -I "$JAR" --min-sdk-version 29 --target-sdk-version 35 -o "$OUT/tone-base.apk"
  cp "$OUT/tone-base.apk" "$OUT/tone-unsigned.apk"; (cd "$OUT/td" && zip -q -j "$OUT/tone-unsigned.apk" classes.dex)
  "$BT/zipalign" -f 4 "$OUT/tone-unsigned.apk" "$OUT/tone-aligned.apk"
  KS="$OUT/debug.keystore"
  [ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias s10debug -keyalg RSA -keysize 2048 -validity 3650 -dname "CN=S10CarOS debug" >/dev/null 2>&1
  "$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android --out "$OUT/ToneTest-debug.apk" "$OUT/tone-aligned.apk"
  echo "$OUT/ToneTest-debug.apk" ;;
*) echo "usage: $0 dex|tone"; exit 2;;
esac
