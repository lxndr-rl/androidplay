#!/usr/bin/env bash
# InputBridge build. Usage: build.sh test | dex
#   test: compile core + run tests/MapperTests.java on the host JVM.
#   dex : compile the privileged injector against android.jar and dex it (run with app_process as shell/system).
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${ANDROIDPLAY_BUILD_DIR:-$HERE/../../.build}/inputbridge"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
mkdir -p "$OUT"
case "${1:-}" in
test)
  rm -rf "$OUT/core" && mkdir -p "$OUT/core"
  javac -Xlint:all -d "$OUT/core" $(find "$HERE/src/dev/lxndr/input/core" -name '*.java') "$HERE/tests/MapperTests.java" "$HERE/tests/GestureTests.java"
  java -cp "$OUT/core" MapperTests && java -cp "$OUT/core" GestureTests
  ;;
dex)
  BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
  JAR="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar"
  rm -rf "$OUT/classes" "$OUT/dex" && mkdir -p "$OUT/classes" "$OUT/dex"
  javac --release 17 -Xlint:all -cp "$JAR" -d "$OUT/classes" $(find "$HERE/src" -name '*.java')
  "$BT/d8" --min-api 29 --lib "$JAR" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')
  cp "$OUT/dex/classes.dex" "$OUT/inject.dex"
  echo "$OUT/inject.dex"
  ;;
*) echo "usage: $0 test|dex"; exit 2;;
esac
