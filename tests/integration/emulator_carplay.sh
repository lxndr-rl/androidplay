#!/usr/bin/env bash
# Level-2 integration: the Android NATIVE sender (JNI libs10carplay.so inside ProjectionService) talks the CarPlay
# AirPlay sequence to a FAKE head unit running on this host. Live H.264 from a VirtualDisplay (Settings app) + a
# synthetic 440 Hz tone. The emulator reaches the host at 10.0.2.2. NOT a USB/NCM link, NOT a real car.
# Prereqs: scripts/run-tests.sh (builds fake_car_server), services/CarPlayService/android/build.sh,
#          services/ProjectionService/build.sh apk, an emulator (or an owned device with ANDROIDPLAY_ALLOW_DEVICE=1).
# The fake car listens on ALL host interfaces for the duration of the run (about 30 s, no secrets, test traffic only).
# Usage: tests/integration/emulator_carplay.sh [-s SERIAL]
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"; S=""
while getopts "s:" o; do case $o in s) S="$OPTARG";; esac; done
a() { if [ -n "$S" ]; then "$ADB" -s "$S" "$@"; else "$ADB" "$@"; fi; }
[ "$(a get-state 2>/dev/null)" = "device" ] || { echo "no device"; exit 2; }
if [ "$(a shell getprop ro.kernel.qemu)" != "1" ] && [ "${ANDROIDPLAY_ALLOW_DEVICE:-0}" != "1" ]; then echo "not an emulator; set ANDROIDPLAY_ALLOW_DEVICE=1"; exit 2; fi
OUT="${ANDROIDPLAY_OUT:-$ROOT/.build/emulator_carplay}"; mkdir -p "$OUT"
CAR="$ROOT/.build/carplay/fake_car_server"; [ -x "$CAR" ] || { echo "build fake_car_server first (scripts/run-tests.sh)"; exit 1; }

"$CAR" 28 > "$OUT/car.out" 2>&1 &
CARPID=$!
for i in $(seq 1 20); do grep -q CONTROL_PORT "$OUT/car.out" 2>/dev/null && break; sleep 0.2; done
PORT=$(grep -o 'CONTROL_PORT=[0-9]*' "$OUT/car.out" | cut -d= -f2)
[ -n "$PORT" ] || { echo "FAIL: fake car did not start"; kill $CARPID 2>/dev/null; exit 1; }
echo "fake car control port: $PORT"

a install -r "$ROOT/.build/projection/ProjectionService-debug.apk" >/dev/null || { kill $CARPID; exit 1; }
a shell pm grant dev.lxndr.projection android.permission.POST_NOTIFICATIONS
a shell am force-stop com.android.settings
a logcat -c
a shell am start-foreground-service -n dev.lxndr.projection/.ProjectionService --ei w 1200 --ei h 512 --ei seconds 22 --es host 10.0.2.2 --ei port "$PORT" >/dev/null
D=""; for i in $(seq 1 30); do D=$(a logcat -d -s AndroidPlay-PROJ | grep -o 'display=[0-9]*' | head -1 | cut -d= -f2); [ -n "$D" ] && break; sleep 0.5; done
[ -n "$D" ] || { echo "FAIL: no virtual display"; kill $CARPID; exit 1; }
a shell am start --display "$D" -n com.android.settings/.Settings >/dev/null
sleep 8
# scroll the Settings list so the encoder keeps producing frames
for i in 1 2 3; do a shell input -d "$D" swipe 300 400 300 100 250; sleep 1.5; a shell input -d "$D" swipe 300 100 300 400 250; sleep 1.5; done
wait $CARPID
a logcat -d -s AndroidPlay-PROJ AndroidPlay-CP AndroidRuntime:E DEBUG:F > "$OUT/logcat.txt"
SUM=$(grep -o 'SUMMARY {.*}' "$OUT/car.out" | sed 's/^SUMMARY //'); echo "car: $SUM"
echo "$SUM" > "$OUT/summary.json"
grep -E "streaming|failed|carplay status|keyframe requested|touch kind" "$OUT/logcat.txt" | sort | uniq -c | sort -rn | head -8
python3 -I - "$OUT/summary.json" "$OUT/logcat.txt" <<'PY'
import json, sys
s = json.load(open(sys.argv[1])); log = open(sys.argv[2]).read()
checks = {
  "frames >= 20": s["frames"] >= 20, "decrypt_ok": s["decrypt_ok"], "size 1200x512": (s["width"], s["height"]) == (1200, 512),
  "config sent (>=1; >=2 after car's forceKeyFrame)": s["configs"] >= 1, "audio packets > 100": s["audio_packets"] > 100,
  "feedback >= 2": s["feedback"] >= 2, "udp keepalives >= 2": s["keepalive_udp"] >= 2,
  "sequence head": s["seq_head"].startswith("SETUP /setup | GET /info | RECORD /"),
  "native reported streaming": "streaming" in log,
  "touch callback reached Java (down+up)": "touch kind=0 x=1199 y=511" in log and "touch kind=2" in log,
  "keyframe request reached Java": "keyframe requested" in log,
}
bad = [k for k, v in checks.items() if not v]
for k, v in checks.items(): print(("PASS " if v else "FAIL ") + k)
print("note: timing_replies=%s (host->emulator UDP is not routable through the emulator NAT; covered by the loopback test)" % s["timing_replies"])
sys.exit(1 if bad else 0)
PY
