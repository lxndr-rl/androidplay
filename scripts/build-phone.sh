#!/usr/bin/env bash
# Builds everything that runs on the phone into .build/phone/ (no device needed):
#   native arm64 tools (static libstdc++): gadget_manager ffs_probe mdns_probe carplay_phone usb_host_iap2
#   dex: projector.dex inject.dex      apks: CarLauncher-debug.apk ProjectionService-debug.apk
# Needs: Android SDK (ANDROID_SDK_ROOT, build-tools, a platform) and NDK (ANDROID_NDK_ROOT or $SDK/ndk/*), JDK 17+.
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
NDK="${ANDROID_NDK_ROOT:-$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)}"
CXX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++"
[ -x "$CXX" ] || { echo "NDK clang not found ($CXX): set ANDROID_NDK_ROOT"; exit 1; }
OUT="${ANDROIDPLAY_BUILD_DIR:-$ROOT/.build}"; PH="$OUT/phone"; mkdir -p "$PH"
cc() { "$CXX" -std=c++17 -O1 -static-libstdc++ "$@"; }
G="$ROOT/usb/GadgetManager"; C="$ROOT/services/CarPlayService"
echo "== native tools"
cc -I"$G/include" "$G/src/main.cpp" "$G/src/gadget_manager.cpp" -o "$PH/gadget_manager"
cc -I"$G/include" "$G/src/ffs_probe.cpp" "$G/src/gadget_manager.cpp" -o "$PH/ffs_probe"
cc "$G/src/mdns_probe.cpp" -o "$PH/mdns_probe"
cc -I"$C/include" "$C/src/carplay_phone.cpp" "$C/src/session_runner.cpp" "$C/src/airplay_client.cpp" "$C/src/airplay.cpp" \
   "$C/src/airplay_audio.cpp" "$C/src/bplist.cpp" "$C/src/crypto_portable.cpp" -o "$PH/carplay_phone"
cc -I"$C/include" "$C/src/usb_host_iap2.cpp" "$C/src/iap2_driver.cpp" "$C/src/iap2_session.cpp" "$C/src/iap2_link.cpp" \
   "$C/src/iap2_control.cpp" -o "$PH/usb_host_iap2"
echo "== dex and apks"
"$ROOT/services/InputBridge/build.sh" dex >/dev/null && cp "$OUT/inputbridge/inject.dex" "$PH/"
"$ROOT/services/Projector/build.sh" dex >/dev/null && cp "$OUT/projector/projector.dex" "$PH/"
# Control screen (TEST AID): one random token per checkout, packaged in the app's assets and pushed to the phone for the control server
mkdir -p "$OUT/ctl/assets"
[ -s "$OUT/ctl/token" ] || head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n' > "$OUT/ctl/token"
cp "$OUT/ctl/token" "$OUT/ctl/assets/ctl.token"
"$ROOT/services/Control/build.sh" dex >/dev/null && cp "$OUT/control/ctl.dex" "$PH/" && cp "$OUT/ctl/token" "$PH/androidplay.token"
CTL_ASSETS="$OUT/ctl/assets" "$ROOT/packages/CarLauncher/build.sh" apk >/dev/null && cp "$OUT/carlauncher/CarLauncher-debug.apk" "$PH/"
"$ROOT/services/ProjectionService/build.sh" apk >/dev/null && cp "$OUT/projection/ProjectionService-debug.apk" "$PH/"
ls -l "$PH"
