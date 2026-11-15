#!/usr/bin/env bash
# Projector build. Usage: build.sh dex  -> .build/projector/projector.dex (run with app_process as the shell uid)
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${S10CAROS_BUILD_DIR:-$HERE/../../.build}/projector"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
mkdir -p "$OUT"
case "${1:-}" in
dex)
  BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
  JAR="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar"
  rm -rf "$OUT/classes" "$OUT/dex" && mkdir -p "$OUT/classes" "$OUT/dex"
  # the encoder is shared with the ProjectionService probe
  javac --release 17 -Xlint:all -cp "$JAR" -d "$OUT/classes" $(find "$HERE/src" -name '*.java') "$HERE/../ProjectionService/src/com/s10caros/projection/H264Encoder.java"
  "$BT/d8" --min-api 29 --lib "$JAR" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')
  cp "$OUT/dex/classes.dex" "$OUT/projector.dex"
  echo "$OUT/projector.dex"
  ;;
*) echo "usage: $0 dex"; exit 2;;
esac
