#!/usr/bin/env bash
# Makes the USB mode manager start by itself at every boot (no PC, no manual command).
#   scripts/install-autostart.sh            install
#   scripts/install-autostart.sh --remove   remove it again
# How: `adb remount` puts an overlayfs over /vendor (the real partitions are not written; the changes live in a scratch area
# that `--remove` empties), then an init .rc is added to /vendor/etc/init. Needs: userdebug build, unlocked bootloader,
# `adb root`. The first install makes adb remount enable overlayfs and asks for one reboot (done here with `adb reboot`).
# Owner opt-in: the rc sets APPLE_ID_OK=own_radio_only (see docs/installation.md, LEGAL-001).
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RC=/vendor/etc/init/androidplay.rc
adb get-state >/dev/null 2>&1 || { echo "no phone on adb"; exit 1; }
adb root >/dev/null 2>&1 || true; sleep 2
[ "$(adb shell id -u | tr -d '\r')" = 0 ] || { echo "adb root is not available on this build"; exit 1; }
adb remount 2>&1 | tail -3
if [ "${1:-}" = --remove ]; then
  adb shell "rm -f $RC /vendor/etc/init/androidplay_test.rc"
  adb shell "/data/local/tmp/androidplayd.sh stop" >/dev/null 2>&1 || true
  echo "removed. Reboot (adb reboot) for it to take effect; the manager stays running until then."
  exit 0
fi
adb push "$ROOT/scripts/device/androidplay.rc" $RC >/dev/null
adb shell "chmod 644 $RC; chcon u:object_r:vendor_file:s0 $RC; ls -lZ $RC"
adb shell 'ls /data/local/tmp/androidplayd.sh >/dev/null' || { echo "run scripts/deploy-phone.sh first"; exit 1; }
echo "installed. Rebooting once to verify it starts by itself..."
adb reboot
