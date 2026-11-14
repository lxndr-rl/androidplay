#!/usr/bin/env bash
# CarLauncher build. Usage: build.sh test | apk
#   test: compile core (pure Java) + run tests/CoreTests.java on the host JVM.
#   apk : compile the Android shell against android.jar, dex, link manifest, sign with a throwaway debug key.
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${S10CAROS_BUILD_DIR:-$HERE/../../.build}/carlauncher"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
mkdir -p "$OUT"

case "${1:-}" in
test)
  rm -rf "$OUT/core" && mkdir -p "$OUT/core"
  javac -Xlint:all -d "$OUT/core" $(find "$HERE/src/com/s10caros/launcher/core" -name '*.java') "$HERE/tests/CoreTests.java"
  java -cp "$OUT/core" CoreTests
  ;;
apk)
  BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
  JAR="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar"
  [ -f "$JAR" ] || { echo "android.jar not found under $SDK/platforms"; exit 1; }
  rm -rf "$OUT/classes" "$OUT/dex" && mkdir -p "$OUT/classes" "$OUT/dex"
  javac --release 17 -Xlint:all -cp "$JAR" -d "$OUT/classes" $(find "$HERE/src" -name '*.java')
  "$BT/d8" --min-api 29 --lib "$JAR" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')
  "$BT/aapt2" link --manifest "$HERE/AndroidManifest.xml" -I "$JAR" --min-sdk-version 29 --target-sdk-version 35 -o "$OUT/base.apk"
  cp "$OUT/base.apk" "$OUT/unsigned.apk"
  (cd "$OUT/dex" && zip -q -j "$OUT/unsigned.apk" classes.dex)
  "$BT/zipalign" -f 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"
  KS="$OUT/debug.keystore"
  [ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias s10debug -keyalg RSA -keysize 2048 -validity 3650 -dname "CN=S10CarOS debug" >/dev/null 2>&1
  "$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android --out "$OUT/CarLauncher-debug.apk" "$OUT/aligned.apk"
  "$BT/apksigner" verify "$OUT/CarLauncher-debug.apk"
  echo "$OUT/CarLauncher-debug.apk"
  ;;
*) echo "usage: $0 test|apk"; exit 2;;
esac
