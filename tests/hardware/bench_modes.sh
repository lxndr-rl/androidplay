#!/system/bin/sh
# BENCH (phone on the PC): is switching ONLY the usb ids (no function changes, no HAL toggle) a safe way to go between
# ADB mode (Samsung ids) and CARPLAY mode (Apple ids) with adb + our FunctionFS function permanently in one config?
# Each phase is logged with the time; the PC side watches lsusb/adb. Always ends in the stock ids with the function removed.
# usage: bench_modes.sh CYCLES
N=${1:-3}; D=/data/local/tmp; G=/config/usb_gadget/g1; L=$D/bench.log
UDC=$(ls /sys/class/udc | head -1)
t() { cut -d' ' -f1 /proc/uptime; }
log() { echo "$(t) $*" >> $L; }
setids() { echo "" > $G/UDC; sleep 1; echo $1 > $G/idVendor; echo $2 > $G/idProduct; echo "$3" > $G/strings/0x409/manufacturer; echo "$4" > $G/strings/0x409/product; echo $UDC > $G/UDC; }
# Guard: if the gadget is ever left unbound (UDC empty) for 2 reads 2 s apart, bind it again. This is the failure that
# used to need a reboot; the guard makes any experiment self-healing.
( while :; do sleep 2; a=$(cat $G/UDC 2>/dev/null); if [ -z "$a" ]; then sleep 2; b=$(cat $G/UDC 2>/dev/null); [ -z "$b" ] && { echo $UDC > $G/UDC 2>/dev/null; echo "$(t) guard rebound UDC" >> $L; }; fi; [ -f $D/bench.stop ] && exit 0; done ) &
GUARD=$!
: > $L
log "start udc=$(cat $G/UDC) ids=$(cat $G/idVendor):$(cat $G/idProduct) cfg=$(ls $G/configs/b.1 | tr '\n' ' ')"
# 1. compose our function next to adb (same procedure that worked on 2026-11-14)
$D/gadget_manager --compose-into g1 setup >> $L 2>&1
$D/ffs_probe carplay 900 clone > $D/bench_ffs.log 2>&1 &
FP=$!
sleep 2
$D/gadget_manager --compose-into g1 bind >> $L 2>&1
sleep 8
log "composed: udc_state=$(cat /sys/class/udc/*/state) cfg=$(ls $G/configs/b.1 | tr '\n' ' ')"
c=1
while [ $c -le $N ]; do
  log "cycle $c -> APPLE ids"; setids 0x05ac 0x12a8 "Apple Inc." iPhone; sleep 10
  log "cycle $c apple phase: udc_state=$(cat /sys/class/udc/*/state)"
  log "cycle $c -> SAMSUNG ids"; setids 0x04e8 0x685d samsung SM-G975F; sleep 12
  log "cycle $c samsung phase: udc_state=$(cat /sys/class/udc/*/state)"
  c=$((c+1))
done
# 2. leave the stock configuration
kill $FP 2>/dev/null; sleep 1
$D/gadget_manager --compose-into g1 teardown >> $L 2>&1
umount /dev/ffs-carplay 2>/dev/null
log "end: udc=$(cat $G/UDC) ids=$(cat $G/idVendor):$(cat $G/idProduct) state=$(cat /sys/class/udc/*/state) cfg=$(ls $G/configs/b.1 | tr '\n' ' ')"
echo BENCH-DONE >> $L
touch $D/bench.stop; sleep 3; kill $GUARD 2>/dev/null; rm -f $D/bench.stop
