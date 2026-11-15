#!/system/bin/sh
# Hardware experiment, started by ffs_probe right after it acknowledged the radio's `40 51` (role switch request).
# Tries to make the phone USB host WITHOUT touching power_role (stays sink: no VBUS source, no boost).
# Steps: drop the gadget (radio sees a disconnect, like an iPhone about to swap), write typec data_role=host,
# poll for a USB device appearing on the phone's host side, then restore device mode. Logs to swap.log.
L=/data/local/tmp/swap.log; G=/config/usb_gadget/g1; P=/sys/class/typec/port0
t() { echo "$(cut -d' ' -f1 /proc/uptime)"; }
snap() { echo "$(t) $1 udc=$(cat $G/UDC) udcstate=$(cat /sys/class/udc/*/state) dr=$(cat $P/data_role) pr=$(cat $P/power_role) partner=$(ls /sys/class/typec | tr '\n' ',') usbdevs=$(ls /sys/bus/usb/devices 2>/dev/null | tr '\n' ',')" >> $L; }
U=$(cat $G/UDC)
snap before
echo "" > $G/UDC; sleep 1; snap gadget-off
for i in 1 2 3; do
  echo host > $P/data_role 2> /data/local/tmp/swap.err; echo "$(t) write data_role=host rc=$? err=$(cat /data/local/tmp/swap.err)" >> $L
  sleep 1; snap try$i
done
for i in $(seq 1 12); do sleep 1; snap poll$i
  for d in /sys/bus/usb/devices/*; do [ -f $d/idVendor ] && echo "$(t) dev $(basename $d) $(cat $d/idVendor):$(cat $d/idProduct) $(cat $d/manufacturer 2>/dev/null)/$(cat $d/product 2>/dev/null)" >> $L; done
done
dmesg 2>/dev/null | tail -40 | grep -iE "usb|typec|max77705|ccic|dwc3|otg|role|vbus" >> $L
echo device > $P/data_role 2>/dev/null
echo $U > $G/UDC; sleep 1; snap restored
