#!/usr/bin/env bash
# Installs the CarLauncher APK and grants what its cards need (no dialogs on the car screen):
# location (speed, weather position), phone state (signal / data type), notification-listener access (now playing).
# Also pushes the touch injector with the edge gestures. Needs the phone on adb (root only for the dex push).
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APK="$HERE/.build/carlauncher/CarLauncher-debug.apk"
P=com.s10caros.launcher
[ -f "$APK" ] || { echo "build first: packages/CarLauncher/build.sh apk"; exit 1; }
adb install -r "$APK"
for perm in ACCESS_FINE_LOCATION READ_PHONE_STATE POST_NOTIFICATIONS; do adb shell pm grant $P android.permission.$perm || echo "grant $perm failed"; done
adb shell cmd notification allow_listener $P/$P.app.MediaListener || echo "allow_listener failed"
[ -f "$HERE/.build/inputbridge/inject.dex" ] && adb push "$HERE/.build/inputbridge/inject.dex" /data/local/tmp/inject.dex
echo done
