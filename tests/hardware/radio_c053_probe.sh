#!/system/bin/sh
# OPT-IN experiment (LEGAL-001 decision 2026-11-14: own radio only, never shipped).
# Presents Apple's USB identity (05ac:12a8) with an FFS function composed into g1 and serves ep0 with ffs_probe:
# answers C0 53 with 01 00 00 00, acknowledges 40 51 WITHOUT swapping roles, logs every setup request.
# Run as root on the phone, detached:  APPLE_ID_OK=own_radio_only [PROBE_MODE=noeps|clone] [SOLE=1] [SWAP=1] radio_c053_probe.sh [seconds]
# A watchdog restores the stock ids (04e8:685d samsung/SM-G975F) and adb even if the main flow dies.
[ "$APPLE_ID_OK" = own_radio_only ] || { echo "refused: set APPLE_ID_OK=own_radio_only"; exit 1; }
DUR=${1:-600}
D=/data/local/tmp; G=/config/usb_gadget/g1; L=$D/c053.log
GM="$D/gadget_manager --compose-into g1"
# Restore = the path that already worked for the compose tests: let init/HAL rewrite ids by toggling
# sys.usb.config. Manual id writes only as a fallback if the ids are still Apple's afterwards
# (an extra UDC unbind/rebind before the toggle left the phone off the bus: observed 2026-11-14).
restore_ids() {
  setprop sys.usb.config none; sleep 2; setprop sys.usb.config adb; sleep 4
  if [ "$(cat $G/idVendor)" != 0x04e8 ]; then
    U=$(cat $G/UDC); [ -z "$U" ] && U=$(ls /sys/class/udc | head -1)
    echo "" > $G/UDC; sleep 1
    echo 0x04e8 > $G/idVendor; echo 0x685d > $G/idProduct
    echo samsung > $G/strings/0x409/manufacturer; echo SM-G975F > $G/strings/0x409/product
    echo $U > $G/UDC
  fi
}
( sleep $((DUR+90)); echo "watchdog fired $(date +%T)" >> $L
  pkill ffs_probe; $GM teardown >> $L 2>&1; umount /dev/ffs-carplay 2>/dev/null
  restore_ids; echo "watchdog done" >> $L ) &
{
echo "start $(date +%T) udc=$(cat $G/UDC)"
$D/radio_observe.sh $((DUR+30)) >/dev/null 2>&1 &
sleep 3
$GM setup; echo "setup_rc=$?"
S10_SWAP_HOOK=$([ "$SWAP" = 1 ] && echo $D/swap_to_host_hook.sh) $D/ffs_probe carplay $DUR $PROBE_MODE > $D/c053_events.log 2>&1 &
FP=$!; sleep 2; echo "ffs: $(head -1 $D/c053_events.log)"
$GM bind; echo "bind_rc=$?"
sleep 3
U=$(cat $G/UDC); echo "" > $G/UDC; sleep 1
[ "$SOLE" = 1 ] && rm -f $G/configs/b.1/function0   # radio then sees ONLY our function (adb gone; restore = reboot or HAL toggle)
echo 0x05ac > $G/idVendor; echo 0x12a8 > $G/idProduct
echo "Apple Inc." > $G/strings/0x409/manufacturer; echo iPhone > $G/strings/0x409/product
echo $U > $G/UDC
echo "apple ids set $(date +%T) udc_state=$(cat /sys/class/udc/*/state)"
wait $FP
echo "--- ffs_probe events"; cat $D/c053_events.log
$GM teardown; echo "teardown_rc=$?"
umount /dev/ffs-carplay 2>&1
restore_ids
echo "restored $(date +%T) udc_state=$(cat /sys/class/udc/*/state) ids=$(cat $G/idVendor):$(cat $G/idProduct)"
echo DONE
} >> $L 2>&1
