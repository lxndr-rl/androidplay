# Troubleshooting

First step for any device problem: `scripts/diagnostics.sh` (read-only, adb) and attach the output directory.

## Host tools

| Symptom | Cause / action |
|---|---|
| `scripts/run-tests.sh` reports SKIP | a tool is missing (cmake/javac/make); `scripts/setup.sh --dry-run` lists what it would install; `--strict` makes SKIP fail |
| `verify.sh` exits 1 | host below ROM-build specs (RAM/disk/tools). Expected on this workstation. |
| `build.sh` prints REFUSING | by design: set `S10CAROS_CONFIRM_BUILD=1` after reading what it will download |

## Issues already hit while testing (and their fixes)

| Symptom | Cause | Fix |
|---|---|---|
| Launcher service crash-loops on start, `SecurityException ... connectedDevice` | API 36 needs one of a fixed set of permissions for that FGS type | `CHANGE_NETWORK_STATE` + `CHANGE_WIFI_MULTICAST_STATE` in the manifest |
| Native sender status `failed: timing socket` | app lacks `INTERNET` | permission added |
| Whole app aborts with `JNI CallVoidMethodV called with pending exception` | a Java callback threw and the exception stayed pending | native side now clears pending exceptions after each callback |
| `NullPointerException ... MediaCodec.setParameters` right after connecting | car's first command raced the encoder start | pipeline starts before dialling the car |
| Virtual display encodes 0 frames | nothing is drawn on it; a surface encoder emits only on change | launch an app on that display (`am start --display ID`) |
| `adb` pipes lose stdout/stderr separation | `exec-out` merges stderr into the stream | the capture tool appends its summary line; scripts strip it by length |

## Expected failure modes on real hardware (hypotheses, not observed)

| Symptom | Where to look |
|---|---|
| Radio never reacts | `usb-probe.sh`: UDC state, `dmesg-usb.txt`, gadget bound? ep0 requests `C0 53` / `40 51` received? |
| Radio drops the bus ~4 s after enumeration | iAP2 SYN|ACK late or missing (`research/carplay-protocol.md`; the reference sender saw this) |
| Answers 466 to SETUP | `et` must be 1 (already); otherwise the radio wants pairing (unsupported) |
| Phone gets hot / reboots on USB | check VBUS back-feed (meter) before blaming software; thermal zones in `diagnostics.sh` |
| No audio | MainAudio ownership (`modesChanged` entities); never SETUP audio while the car owns it |
