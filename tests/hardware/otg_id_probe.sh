#!/system/bin/sh
# Experiment (root, detached): force the Exynos dwc3 OTG FSM to host by writing id=0 (what an OTG adapter's grounded ID
# does), WITHOUT touching typec power_role. Logs state, usb devices and charger/otg messages, then restores id=1.
# Usage: otg_id_probe.sh [hold_seconds]
H=${1:-15}; O=/sys/devices/platform/10c00000.usb/10c00000.dwc3; L=/data/local/tmp/otgid.log; T=/sys/class/typec/port0
t() { cut -d' ' -f1 /proc/uptime; }
snap() { echo "$(t) $1 id=$(cat $O/id) b_sess=$(cat $O/b_sess) state=$(cat $O/state) udc=$(cat /sys/class/udc/*/state 2>&1 | head -1) dr=$(cat $T/data_role) pr=$(cat $T/power_role) usbdevs=$(ls /sys/bus/usb/devices 2>/dev/null | tr '\n' ',')" >> $L; }
: > $L; dmesg -c >/dev/null 2>&1
snap before
echo 0 > $O/id; echo "$(t) wrote id=0 rc=$?" >> $L
for i in $(seq 1 $H); do sleep 1; snap hold$i; done
dmesg | grep -iE "otg|host|vbus|id|xhci|usb|ccic|typec|dr_|role" | grep -viE "wdt|fg_|chg_monitor" | tail -60 >> $L
echo 1 > $O/id; echo "$(t) wrote id=1 rc=$?" >> $L
sleep 3; snap after
