#!/usr/bin/env bash
# Counts how many times the PC saw the phone disconnect while it was armed (Apple ids), in a fixed window after the Apple device
# first appeared. A stable armed phone shows 0-1; a radio that probes twice 3 s apart needs 0.
#   tests/hardware/flap_count.sh [WINDOW_S=15]       phone on adb; it is rebooted; flag files steer the variant under test
set -u
W=${1:-15}
adb reboot; sleep 8
t0=$(date +%s)
for _ in $(seq 1 1500); do
  for d in /sys/bus/usb/devices/[0-9]*-[0-9]*; do
    case $d in *:*) continue;; esac
    [ "$(cat $d/idVendor 2>/dev/null):$(cat $d/idProduct 2>/dev/null)" = "05ac:12a8" ] && found=1
  done
  [ "${found:-0}" = 1 ] && break; sleep 0.1
done
[ "${found:-0}" = 1 ] || { echo "apple device never appeared"; exit 3; }
a=$(date '+%Y-%m-%d %H:%M:%S'); sleep "$W"; b=$(date '+%Y-%m-%d %H:%M:%S')
n=$(journalctl -k --since "$a" --until "$b" --no-pager 2>/dev/null | grep -c "USB disconnect")
e=$(journalctl -k --since "$a" --until "$b" --no-pager 2>/dev/null | grep -c "new high-speed USB device")
echo "window ${W}s: disconnects=$n enumerations=$e"
