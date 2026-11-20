#!/usr/bin/env bash
# Control server build. Usage: build.sh dex | test
#   dex : .build/control/ctl.dex (run with app_process as root; a TEST AID, see CtlServer.java)
#   test: host test of the protocol (token, flags, unknown commands) against a temp /data/local/tmp replacement
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${ANDROIDPLAY_BUILD_DIR:-$HERE/../../.build}/control"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
mkdir -p "$OUT"
case "${1:-}" in
dex)
  BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
  JAR="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar"
  rm -rf "$OUT/classes" "$OUT/dex" && mkdir -p "$OUT/classes" "$OUT/dex"
  javac --release 17 -Xlint:all -cp "$JAR" -d "$OUT/classes" $(find "$HERE/src" -name '*.java')
  "$BT/d8" --min-api 29 --lib "$JAR" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')
  cp "$OUT/dex/classes.dex" "$OUT/ctl.dex"; echo "$OUT/ctl.dex" ;;
test)
  rm -rf "$OUT/host" && mkdir -p "$OUT/host"
  javac -d "$OUT/host" $(find "$HERE/src" -name '*.java') "$HERE/tests/CtlTests.java"
  java -cp "$OUT/host" CtlTests ;;
*) echo "usage: $0 dex|test"; exit 2;;
esac
