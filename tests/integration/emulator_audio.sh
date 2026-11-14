#!/usr/bin/env bash
# Level-2 integration (emulator, or a device you OWN with S10CAROS_ALLOW_DEVICE=1): play a 440 Hz tone in an app and
# capture system audio (REMOTE_SUBMIX) with services/AudioBridge as the shell uid; verify the dominant frequency.
# Usage: tests/integration/emulator_audio.sh [-s SERIAL]
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ADB="${ADB:-$HOME/Android/Sdk/platform-tools/adb}"; S=""
while getopts "s:" o; do case $o in s) S="$OPTARG";; esac; done
a() { if [ -n "$S" ]; then "$ADB" -s "$S" "$@"; else "$ADB" "$@"; fi; }
[ "$(a get-state 2>/dev/null)" = "device" ] || { echo "no device"; exit 2; }
if [ "$(a shell getprop ro.kernel.qemu)" != "1" ] && [ "${S10CAROS_ALLOW_DEVICE:-0}" != "1" ]; then echo "not an emulator; set S10CAROS_ALLOW_DEVICE=1"; exit 2; fi
OUT="${S10CAROS_OUT:-$ROOT/.build/emulator_audio}"; mkdir -p "$OUT"
a install -r "$ROOT/.build/audiobridge/ToneTest-debug.apk" >/dev/null || exit 1
a push "$ROOT/.build/audiobridge/capture.dex" /data/local/tmp/capture.dex >/dev/null
a shell am start -n com.s10caros.tonetest/.ToneActivity --ei seconds 9 >/dev/null; sleep 2
a exec-out "CLASSPATH=/data/local/tmp/capture.dex app_process / com.s10caros.audio.shell.CaptureMain 5" > "$OUT/capture.raw"
a shell am force-stop com.s10caros.tonetest
python3 -I - "$OUT/capture.raw" <<'PY'
import sys, struct, math
raw = open(sys.argv[1], 'rb').read()
want = 5 * 48000 * 4
if len(raw) < want or b"CAPTURED" not in raw[want:]:
    print("FAIL: capture incomplete (%d bytes): %r" % (len(raw), raw[want:want+120])); sys.exit(1)
pcm = raw[:want]
L = [struct.unpack_from('<h', pcm, 4 * i)[0] for i in range(48000, 96000)]
def g(x, f):
    k = 2 * math.cos(2 * math.pi * f / 48000); s1 = s2 = 0
    for v in x: s0 = v + k * s1 - s2; s2 = s1; s1 = s0
    return math.sqrt(s1 * s1 + s2 * s2 - k * s1 * s2) / len(x)
a440, other = g(L, 440), g(L, 300) + g(L, 1000) + g(L, 600)
print("440 Hz=%.1f others=%.3f" % (a440, other))
sys.exit(0 if a440 > 20 and a440 > 50 * (other + 0.001) else 1)
PY
rc=$?; [ $rc -eq 0 ] && echo "PASS: 440 Hz tone captured from system audio" || echo "FAIL"; exit $rc
