#!/system/bin/sh
# Hardware experiment, started by ffs_probe right after it acknowledged the radio's `40 51` (role switch request).
# Makes the phone USB host by forcing the Exynos dwc3 OTG FSM (id=0, like an OTG adapter's grounded ID).
# power_role is never written (verified on a PC port 2026-11-14: stays sink, charging continues, no VBUS source).
# Then polls (120 s) the phone's host side for the radio appearing as a USB device, records its descriptors, restores id=1.
# Single-flight: the radio retries every ~25 s and ffs_probe may start this more than once.
L=/data/local/tmp/swap.log; O=/sys/devices/platform/10c00000.usb/10c00000.dwc3; P=/sys/class/typec/port0
mkdir /data/local/tmp/swap.lock 2>/dev/null || exit 0
t() { cut -d' ' -f1 /proc/uptime; }
snap() { echo "$(t) $1 id=$(cat $O/id) state=$(cat $O/state) udc=$(cat /sys/class/udc/*/state 2>&1 | head -1) dr=$(cat $P/data_role) pr=$(cat $P/power_role) usbdevs=$(ls /sys/bus/usb/devices 2>/dev/null | tr '\n' ',')" >> $L; }
dmesg -c >/dev/null 2>&1
snap before
echo 0 > $O/id; echo "$(t) wrote id=0 rc=$?" >> $L
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null
portsc() { for f in $(find /sys/kernel/debug/usb -name "portsc*" 2>/dev/null | head -4); do echo "$(t) PORTSC $f $(cat $f 2>&1 | head -2 | tr "\n" " ")" >> $L; done; }
DUMPED=0
dump_dev() { d=$1
  echo "$(t) DESC $(basename $d) $(od -An -tx1 -v $d/descriptors 2>/dev/null | tr -d '\n' | cut -c1-1200)" >> $L
  for i in $d/*:*; do echo "$(t) IFACE $(basename $i) class=$(cat $i/bInterfaceClass)/$(cat $i/bInterfaceSubClass)/$(cat $i/bInterfaceProtocol) eps=$(cat $i/bNumEndpoints) str='$(cat $i/interface 2>/dev/null)' drv=$(basename $(readlink $i/driver 2>/dev/null) 2>/dev/null)" >> $L; done
  echo "$(t) SERIAL '$(cat $d/serial 2>/dev/null)' cfgs=$(cat $d/bNumConfigurations) maxpower=$(cat $d/bMaxPower) class=$(cat $d/bDeviceClass)" >> $L
  dmesg | grep -E "usb 1-1|1f3a|cdc_ncm|usbcore|ncm|iap|rndis|xhci-hcd.*(error|fail)" | tail -30 | cut -c1-220 >> $L
}
for i in $(seq 1 120); do sleep 1; snap hold$i; case $i in 1|10|40|80|120) portsc;; esac
  for d in /sys/bus/usb/devices/*; do
    [ -f $d/idVendor ] && [ "$(cat $d/idVendor)" != 1d6b ] && { echo "$(t) DEV $(basename $d) $(cat $d/idVendor):$(cat $d/idProduct) '$(cat $d/manufacturer 2>/dev/null)'/'$(cat $d/product 2>/dev/null)' ifs=$(cat $d/bNumInterfaces 2>/dev/null) speed=$(cat $d/speed 2>/dev/null)" >> $L; [ $DUMPED = 0 ] && { sleep 2; dump_dev $d; DUMPED=1; if [ -x /data/local/tmp/usb_host_iap2 ]; then /data/local/tmp/usb_host_iap2 $(cat $d/busnum) $(cat $d/devnum) 100 > /data/local/tmp/iap.log 2>&1 & fi; ( sleep 4; [ -x /data/local/tmp/mdns_probe ] && /data/local/tmp/mdns_probe eth0 90 > /data/local/tmp/mdns.log 2>&1; ) & ( sleep 5; ping -6 -c 4 -I eth0 ff02::1 > /data/local/tmp/ping6.log 2>&1 ) & sleep 6; ( echo "--- ip addr"; ip addr; echo "--- ip -6 route"; ip -6 route; echo "--- neigh"; ip -6 neigh ) >> /data/local/tmp/ip.log 2>&1; }; }
  done
done
for d in /sys/bus/usb/devices/[0-9]*-[0-9]*:*; do echo "IF $(basename $d) class=$(cat $d/bInterfaceClass)/$(cat $d/bInterfaceSubClass)/$(cat $d/bInterfaceProtocol) eps=$(cat $d/bNumEndpoints)" >> $L; done
dmesg | grep -iE "usb [0-9]|new .*speed|device descriptor|enumerat|cdc|ncm|iap|otg_on|vbus|set_host|dr_set" | tail -40 >> $L
echo 1 > $O/id; echo "$(t) wrote id=1 rc=$?" >> $L
sleep 3; snap after
rmdir /data/local/tmp/swap.lock
