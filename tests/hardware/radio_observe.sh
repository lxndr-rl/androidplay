#!/system/bin/sh
# Passive observer: run as root on the phone, then move the phone to the radio and back.
# Records USB control transfers (dwc3_ctrl_req), dwc3 events, logcat and a 1 Hz state poll.
# Writes to /data/local/tmp/radiolog/. Changes no USB configuration.
D=/data/local/tmp/radiolog
T=/sys/kernel/tracing
DUR=${1:-1800}
rm -rf $D; mkdir -p $D
echo s10radiolog > /sys/power/wake_lock 2>/dev/null
echo 0 > $T/tracing_on
echo > $T/trace
echo 16384 > $T/buffer_size_kb
echo 1 > $T/events/dwc3/dwc3_ctrl_req/enable
echo 1 > $T/events/dwc3/dwc3_event/enable
echo 1 > $T/tracing_on
( timeout $DUR cat $T/trace_pipe | head -c 20000000 > $D/trace.txt ) &
( timeout $DUR logcat -b all -v threadtime | head -c 40000000 > $D/logcat.txt ) &
(
  end=$(( $(date +%s) + DUR ))
  while [ $(date +%s) -lt $end ]; do
    echo "$(cut -d' ' -f1 /proc/uptime) udc=$(cat /sys/class/udc/*/state 2>/dev/null) cfg=$(getprop sys.usb.config) st=$(getprop sys.usb.state) dr=$(cat /sys/class/typec/port0/data_role 2>/dev/null) pr=$(cat /sys/class/typec/port0/power_role 2>/dev/null) partner=$(ls /sys/class/typec 2>/dev/null | tr '\n' ',') bat=$(cat /sys/class/power_supply/battery/capacity 2>/dev/null)"
    sleep 1
  done > $D/state.txt
) &
echo "started $(date) dur=$DUR" > $D/START
wait
echo "ended $(date)" >> $D/START
echo 0 > $T/tracing_on
echo s10radiolog > /sys/power/wake_unlock 2>/dev/null
