#!/system/bin/sh
# Hardware experiment, started by ffs_probe right after it acknowledged the radio's `40 51` (role switch request).
# Makes the phone USB host by forcing the Exynos dwc3 OTG FSM (id=0, like an OTG adapter's grounded ID).
# power_role is never written (verified on a PC port 2026-11-14: stays sink, charging continues, no VBUS source).
# Then polls the phone's host side for the radio appearing as a USB device, records its descriptors, restores id=1.
# Single-flight: the radio retries every ~25 s and ffs_probe may start this more than once.
L=/data/local/tmp/swap.log; O=/sys/devices/platform/10c00000.usb/10c00000.dwc3; P=/sys/class/typec/port0
mkdir /data/local/tmp/swap.lock 2>/dev/null || exit 0
t() { cut -d' ' -f1 /proc/uptime; }
snap() { echo "$(t) $1 id=$(cat $O/id) state=$(cat $O/state) udc=$(cat /sys/class/udc/*/state 2>&1 | head -1) dr=$(cat $P/data_role) pr=$(cat $P/power_role) usbdevs=$(ls /sys/bus/usb/devices 2>/dev/null | tr '\n' ',')" >> $L; }
dmesg -c >/dev/null 2>&1
snap before
echo 0 > $O/id; echo "$(t) wrote id=0 rc=$?" >> $L
for i in $(seq 1 40); do sleep 1; snap hold$i
  for d in /sys/bus/usb/devices/*; do
    [ -f $d/idVendor ] && [ "$(cat $d/idVendor)" != 1d6b ] && echo "$(t) DEV $(basename $d) $(cat $d/idVendor):$(cat $d/idProduct) '$(cat $d/manufacturer 2>/dev/null)'/'$(cat $d/product 2>/dev/null)' ifs=$(cat $d/bNumInterfaces 2>/dev/null) speed=$(cat $d/speed 2>/dev/null)" >> $L
  done
done
for d in /sys/bus/usb/devices/[0-9]*-[0-9]*:*; do echo "IF $(basename $d) class=$(cat $d/bInterfaceClass)/$(cat $d/bInterfaceSubClass)/$(cat $d/bInterfaceProtocol) eps=$(cat $d/bNumEndpoints)" >> $L; done
dmesg | grep -iE "usb [0-9]|new .*speed|device descriptor|enumerat|cdc|ncm|iap|otg_on|vbus|set_host|dr_set" | tail -40 >> $L
echo 1 > $O/id; echo "$(t) wrote id=1 rc=$?" >> $L
sleep 3; snap after
rmdir /data/local/tmp/swap.lock
