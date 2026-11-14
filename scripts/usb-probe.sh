#!/usr/bin/env bash
# usb-probe.sh: collect USB gadget/Type-C state from a phone on adb. Read-only.
# Usage: scripts/usb-probe.sh [-s SERIAL] [-o OUTDIR]
# Exit 0 even with no device (prints a notice, writes nothing); exit 3 if adb is missing.
set -u
SERIAL_ARGS=()
OUT=""
while getopts "s:o:" o; do
    case $o in
        s) SERIAL_ARGS=(-s "$OPTARG");;
        o) OUT="$OPTARG";;
        *) echo "usage: $0 [-s SERIAL] [-o OUTDIR]" >&2; exit 2;;
    esac
done

if ! command -v adb >/dev/null 2>&1; then
    echo "usb-probe: adb not found in PATH" >&2; exit 3
fi

devs=$(adb devices 2>/dev/null | awk 'NR>1 && $2=="device"{print $1}')
if [ -z "$devs" ]; then
    echo "usb-probe: no adb device attached (adb devices is empty or unauthorized). Nothing collected."
    echo "usb-probe: connect the phone, enable USB debugging, accept the key, re-run."
    exit 0
fi

A=(adb "${SERIAL_ARGS[@]}")
sh() { "${A[@]}" shell "$@" 2>&1; }
# root is needed for configfs/dmesg on user builds; try, but never require it
IS_ROOT=$(sh id -u | tr -d '\r')
section() { printf '\n===== %s =====\n' "$1"; }

report() {
    section "device"
    sh getprop ro.product.device; sh getprop ro.build.fingerprint; sh uname -a
    echo "shell uid: $IS_ROOT (0 needed for configfs/dmesg)"
    section "UDC list (/sys/class/udc)"
    sh 'ls /sys/class/udc 2>&1'
    section "configfs mount"
    sh 'mount | grep -E "configfs|functionfs" 2>&1'
    section "configfs gadgets"
    sh 'for g in /config/usb_gadget/*; do [ -d "$g" ] || continue; echo "# $g"; for f in UDC idVendor idProduct bcdUSB max_speed; do printf "%s=" $f; cat $g/$f 2>&1; done; echo "configs:"; ls $g/configs/*/ 2>&1; echo "functions:"; ls $g/functions 2>&1; done; ls /config/usb_gadget 2>&1 | head -1 >/dev/null'
    section "UDC state / speed"
    sh 'for u in /sys/class/udc/*; do [ -e "$u" ] || continue; echo "# $u"; for f in state current_speed maximum_speed is_a_peripheral; do printf "%s=" $f; cat $u/$f 2>&1; done; done'
    section "typec / dual_role"
    sh 'for p in /sys/class/typec/* /sys/class/dual_role_usb/*; do [ -e "$p" ] || continue; echo "# $p"; for f in data_role power_role preferred_role mode supported_modes; do [ -e $p/$f ] && { printf "%s=" $f; cat $p/$f; }; done; done 2>&1'
    section "samsung usb/ccic sysfs"
    sh 'ls /sys/class/sec/ccic /sys/class/sec/switch /sys/class/android_usb/android0 2>&1 | head -40'
    section "getprop sys.usb.* / vendor.usb.* / persist.sys.usb.*"
    sh 'getprop | grep -E "sys\.usb|vendor\.usb|persist\.sys\.usb|init\.svc\..*usb|ro\.boot\.usb"'
    section "SELinux"
    sh getenforce
    section "dmesg usb lines"
    sh 'dmesg 2>&1 | grep -iE "usb|dwc3|ccic|max77705|typec|configfs|gadget|dual_role" | tail -80'
    section "logcat usb (last 80)"
    sh 'logcat -d -b all 2>&1 | grep -iE "usb" | tail -80'
}

if [ -n "$OUT" ]; then
    mkdir -p "$OUT"
    f="$OUT/usb-probe-$(date +%Y%m%d-%H%M%S).txt"
    report | tee "$f"
    echo "usb-probe: saved $f"
else
    report
fi
