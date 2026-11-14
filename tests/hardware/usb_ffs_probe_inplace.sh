#!/system/bin/sh
D=/data/local/tmp; L=$D/probe2.log; G=/config/usb_gadget/g1
{
UDC=$(cat $G/UDC); echo "start $(date +%T) udc=$UDC"
mkdir $G/functions/ffs.carplay; echo "mkdir_func rc=$?"
$D/ffs_probe carplay 40 > $D/ffs_events2.log 2>&1 &
FP=$!
sleep 2; echo "ffs: $(head -2 $D/ffs_events2.log | tr '\n' ' ')"
echo "" > $G/UDC; echo "unbind rc=$?"
ln -s $G/functions/ffs.carplay $G/configs/b.1/function1; echo "link rc=$?"
echo $UDC > $G/UDC; echo "bind rc=$?"
sleep 6; echo "t+6 udc_state=$(cat /sys/class/udc/*/state) speed=$(cat /sys/class/udc/*/current_speed)"
sleep 28; echo "t+34 udc_state=$(cat /sys/class/udc/*/state)"
wait $FP; echo "probe exited"
cat $D/ffs_events2.log
echo "" > $G/UDC; rm -f $G/configs/b.1/function1; umount /dev/ffs-carplay 2>&1; rmdir $G/functions/ffs.carplay; echo "cleanup rc=$?"
echo $UDC > $G/UDC; echo "rebind rc=$?"
sleep 2; setprop sys.usb.config none; sleep 1; setprop sys.usb.config adb
echo "restored $(date +%T) udc_state=$(cat /sys/class/udc/*/state) funcs=$(ls $G/functions | tr '\n' ' ')"
echo DONE
} > $L 2>&1
