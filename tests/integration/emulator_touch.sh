#!/usr/bin/env bash
# Level-2 integration (emulator or any adb device you OWN and may install to): projection VirtualDisplay + injected touch.
# Starts the ProjectionService probe, launches Settings on its virtual display, injects a tap on "Network & internet"
# through services/InputBridge (app_process as shell), and checks that the foreground activity on THAT display changed.
# Usage: tests/integration/emulator_touch.sh [-s SERIAL]    (refuses real devices unless ANDROIDPLAY_ALLOW_DEVICE=1)
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"; S=""
while getopts "s:" o; do case $o in s) S="$OPTARG";; esac; done
a() { if [ -n "$S" ]; then "$ADB" -s "$S" "$@"; else "$ADB" "$@"; fi; }
[ "$(a get-state 2>/dev/null)" = "device" ] || { echo "no device"; exit 2; }
if [ "$(a shell getprop ro.kernel.qemu)" != "1" ] && [ "${ANDROIDPLAY_ALLOW_DEVICE:-0}" != "1" ]; then echo "not an emulator; set ANDROIDPLAY_ALLOW_DEVICE=1 to run on a real device"; exit 2; fi
W=1200; H=512
OUT="${ANDROIDPLAY_OUT:-$ROOT/.build/emulator_touch}"; mkdir -p "$OUT"
a install -r "$ROOT/.build/projection/ProjectionService-debug.apk" >/dev/null || exit 1
a shell pm grant dev.lxndr.projection android.permission.POST_NOTIFICATIONS
a push "$ROOT/.build/inputbridge/inject.dex" /data/local/tmp/inject.dex >/dev/null
a shell run-as dev.lxndr.projection rm -f files/metrics.json files/out.h264 2>/dev/null
a shell am force-stop com.android.settings
a logcat -c
a shell am start-foreground-service -n dev.lxndr.projection/.ProjectionService --ei w $W --ei h $H --ei seconds 22 >/dev/null
D=""; for i in $(seq 1 30); do D=$(a logcat -d -s AndroidPlay-PROJ | grep -o 'display=[0-9]*' | head -1 | cut -d= -f2); [ -n "$D" ] && break; sleep 0.5; done
[ -n "$D" ] || { echo "FAIL: no virtual display"; exit 1; }
a shell am start --display "$D" -n com.android.settings/.Settings >/dev/null
# Evidence = the ENCODED stream itself: frames before vs after the tap must differ a lot (Settings home -> sub page).
sleep 3
printf 'D 300 215\nS 80\nU 300 215\n' | a shell "CLASSPATH=/data/local/tmp/inject.dex app_process / dev.lxndr.input.shell.InjectMain --display $D --size $W $H" | tee "$OUT/inject.out"
grep -q 'sent=2 dropped=0' "$OUT/inject.out" || { echo "FAIL: injector did not send both events"; exit 1; }
for i in $(seq 1 60); do a shell run-as dev.lxndr.projection ls files/metrics.json >/dev/null 2>&1 && break; sleep 1; done
a exec-out run-as dev.lxndr.projection cat files/out.h264 > "$OUT/out.h264"
a exec-out run-as dev.lxndr.projection cat files/metrics.json > "$OUT/metrics.json"; cat "$OUT/metrics.json"; echo
N=$(ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 -f h264 "$OUT/out.h264")
echo "frames=$N"
[ "${N:-0}" -gt 40 ] || { echo "FAIL: too few frames"; exit 1; }
ffmpeg -v error -y -f h264 -i "$OUT/out.h264" -vf "select=eq(n\,15)" -frames:v 1 "$OUT/before.png"
ffmpeg -v error -y -f h264 -i "$OUT/out.h264" -vf "select=eq(n\,$((N-3)))" -frames:v 1 "$OUT/after.png"
PSNR=$(ffmpeg -v info -i "$OUT/before.png" -i "$OUT/after.png" -lavfi psnr -f null - 2>&1 | grep -o 'average:[0-9.a-z]*' | cut -d: -f2)
echo "PSNR(before,after)=$PSNR dB  (frames in $OUT)"
a shell am force-stop dev.lxndr.projection
if [ "$PSNR" != "inf" ] && awk -v p="$PSNR" 'BEGIN{exit !(p+0 < 25)}'; then echo "PASS: encoded stream of display $D changed after the injected tap"; exit 0; fi
echo "FAIL: stream did not change (PSNR=$PSNR)"; exit 1
