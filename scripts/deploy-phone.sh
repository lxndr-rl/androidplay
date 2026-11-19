#!/usr/bin/env bash
# Pushes the build (scripts/build-phone.sh) to the phone, installs the apps and grants what the launcher cards need.
# Phone: on adb, rooted adb (`adb root` works), USB debugging on. Does NOT start the USB mode manager unless asked.
#   scripts/deploy-phone.sh [--start]     --start also launches the manager (needs APPLE_ID_OK=own_radio_only, see docs/installation.md)
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PH="${ANDROIDPLAY_BUILD_DIR:-$ROOT/.build}/phone"
[ -f "$PH/gadget_manager" ] || { echo "run scripts/build-phone.sh first"; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "no phone on adb"; exit 1; }
adb root >/dev/null 2>&1 || true; sleep 2
[ "$(adb shell id -u | tr -d '\r')" = 0 ] || { echo "adb root is not available on this build"; exit 1; }
D=/data/local/tmp
for f in gadget_manager ffs_probe mdns_probe carplay_phone usb_host_iap2 projector.dex inject.dex; do adb push "$PH/$f" $D/ >/dev/null; done
adb push "$ROOT/scripts/device/androidplayd.sh" $D/ >/dev/null
adb push "$ROOT/scripts/device/learn.sh" $D/ >/dev/null
adb push "$ROOT/scripts/device/archive_logs.sh" $D/ >/dev/null
adb push "$ROOT/tests/hardware/swap_to_host_hook.sh" $D/ >/dev/null
adb shell "chmod 755 $D/gadget_manager $D/ffs_probe $D/mdns_probe $D/carplay_phone $D/usb_host_iap2 $D/androidplayd.sh $D/learn.sh $D/archive_logs.sh $D/swap_to_host_hook.sh"
adb install -r "$PH/CarLauncher-debug.apk" >/dev/null
adb install -r "$PH/ProjectionService-debug.apk" >/dev/null
P=dev.lxndr.launcher
for perm in ACCESS_FINE_LOCATION READ_PHONE_STATE POST_NOTIFICATIONS; do adb shell pm grant $P android.permission.$perm || true; done
adb shell cmd notification allow_listener $P/$P.app.MediaListener || true
echo "deployed. Wi-Fi to treat as home (phone stays a normal ADB phone there):"
echo "  adb shell 'echo -n \"YOUR_SSID\" > $D/androidplay.home_ssid'"
if [ "${1:-}" = --start ]; then
  adb shell "APPLE_ID_OK=${APPLE_ID_OK:-} SOLE=1 nohup $D/androidplayd.sh start >/dev/null 2>&1 &"
  sleep 6; adb shell "$D/androidplayd.sh status | head -1" || echo "(adb may reconnect: replug the cable once if it does not return)"
else
  echo "start the manager when ready:  APPLE_ID_OK=own_radio_only scripts/deploy-phone.sh --start"
fi
