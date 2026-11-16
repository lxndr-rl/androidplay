#!/usr/bin/env bash
# AndroidPlay device diagnostics. Read-only: collects state over adb into a timestamped directory.
# Works with no device (reports it and exits 0). Never flashes, never changes settings.
# Usage: scripts/diagnostics.sh [-s SERIAL] [-o OUTDIR]
set -u
SERIAL=""; OUT=""
while getopts "s:o:" o; do case $o in s) SERIAL="$OPTARG";; o) OUT="$OPTARG";; *) echo "usage: $0 [-s serial] [-o outdir]"; exit 2;; esac; done
ADB="${ADB:-$(command -v adb || echo "$HOME/Android/Sdk/platform-tools/adb")}"
[ -x "$ADB" ] || { echo "adb not found (set ADB=...)"; exit 0; }
A=("$ADB"); [ -n "$SERIAL" ] && A+=(-s "$SERIAL")
devs="$("$ADB" devices | awk 'NR>1 && $2=="device"{print $1}')"
if [ -z "$devs" ]; then
  echo "NO DEVICE: no adb device in 'device' state. Nothing collected."
  "$ADB" devices | tail -n +2 | grep -v '^$' | sed 's/^/  seen: /'
  exit 0
fi
if [ -z "$SERIAL" ] && [ "$(echo "$devs" | wc -l)" -gt 1 ]; then echo "Multiple devices; pass -s SERIAL:"; echo "$devs"; exit 2; fi
TS="$(date +%Y%m%d-%H%M%S)"; OUT="${OUT:-diagnostics-$TS}"; mkdir -p "$OUT"
adbsh() { "${A[@]}" shell "$@" 2>&1; }

# Identity: gate for every device-specific procedure (flashing stays blocked until this is confirmed).
{
  echo "model=$(adbsh getprop ro.product.model | tr -d '\r')"
  echo "device=$(adbsh getprop ro.product.device | tr -d '\r')"
  echo "build=$(adbsh getprop ro.build.display.id | tr -d '\r')"
  echo "lineage=$(adbsh getprop ro.lineage.version | tr -d '\r')"
  echo "sdk=$(adbsh getprop ro.build.version.sdk | tr -d '\r')"
  echo "soc=$(adbsh getprop ro.hardware | tr -d '\r')"
  echo "bootloader=$(adbsh getprop ro.boot.bootloader | tr -d '\r')"
  echo "selinux=$(adbsh getenforce | tr -d '\r')"
  echo "root_uid=$(adbsh id | tr -d '\r')"
} >"$OUT/identity.txt"
model="$(grep '^model=' "$OUT/identity.txt" | cut -d= -f2)"
case "$model" in SM-G975F*|SM-G975N*) echo "model OK for target: $model";; *) echo "WARNING: model '$model' is not SM-G975F/SM-G975N; do NOT use beyond2lte procedures";; esac

adbsh getprop | grep -E 'sys\.usb|persist\.sys\.usb|init\.svc\..*(gadget|usb)|androidplay' >"$OUT/usb-props.txt"
adbsh 'for g in /config/usb_gadget/*; do echo "== $g"; cat $g/UDC 2>/dev/null; ls $g/functions $g/configs/*/ 2>/dev/null; done' >"$OUT/configfs.txt"
adbsh 'ls /sys/class/udc 2>/dev/null; for u in /sys/class/udc/*; do echo "$u state=$(cat $u/state 2>/dev/null) speed=$(cat $u/current_speed 2>/dev/null)"; done' >"$OUT/udc.txt"
adbsh 'for t in /sys/class/typec/*; do echo "== $t"; for f in data_role power_role power_operation_mode vconn_source; do echo "$f=$(cat $t/$f 2>/dev/null)"; done; done' >"$OUT/typec.txt"
adbsh 'dmesg 2>&1 | grep -iE "usb|dwc3|typec|gadget|configfs|s10car" | tail -n 300' >"$OUT/dmesg-usb.txt"
adbsh 'cat /sys/class/power_supply/battery/{status,capacity,temp,current_now,voltage_now,health} 2>&1; dumpsys battery' >"$OUT/battery.txt"
adbsh 'for z in /sys/class/thermal/thermal_zone*; do echo "$(cat $z/type 2>/dev/null) $(cat $z/temp 2>/dev/null)"; done' >"$OUT/thermal.txt"
adbsh 'cat /proc/meminfo | head -5; uptime' >"$OUT/system.txt"
adbsh 'logcat -d -t 2000 -v threadtime' >"$OUT/logcat.txt"
adbsh 'ps -A | grep -iE "s10|carplay|gadget|projection"' >"$OUT/services.txt"
adbsh 'dumpsys display | grep -iE "mState|DisplayDeviceInfo|virtual" | head -30' >"$OUT/display.txt"
adbsh 'getprop persist.sys.usb.config; settings get global adb_enabled; getprop service.adb.tcp.port' >"$OUT/adb-state.txt"
echo "Collected into $OUT"; ls "$OUT"
