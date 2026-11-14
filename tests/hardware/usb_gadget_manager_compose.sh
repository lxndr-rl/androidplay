#!/system/bin/sh
# Run as root ON the phone (survives adb loss). Composes ffs.carplay into g1 with the real
# gadget_manager CLI, serves ep0 with ffs_probe for N seconds, then tears down and restores adb.
D=/data/local/tmp; L=$D/compose.log; G=/config/usb_gadget/g1
GM="$D/gadget_manager --compose-into g1"
{
echo "start $(date +%T) udc=$(cat $G/UDC)"
$GM setup; echo "setup_rc=$?"
$D/ffs_probe carplay 40 echo > $D/compose_events.log 2>&1 &
FP=$!; sleep 2; echo "ffs: $(head -1 $D/compose_events.log)"
$GM bind; echo "bind_rc=$?"
sleep 6; echo "t+6 udc_state=$(cat /sys/class/udc/*/state)"
wait $FP; cat $D/compose_events.log
$GM teardown; echo "teardown_rc=$?"
umount /dev/ffs-carplay 2>&1
sleep 2; setprop sys.usb.config none; sleep 1; setprop sys.usb.config adb
echo "restored $(date +%T) udc_state=$(cat /sys/class/udc/*/state) funcs=$(ls $G/functions | tr '\n' ' ')"
echo DONE
} > $L 2>&1
