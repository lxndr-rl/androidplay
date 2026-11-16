#!/system/bin/sh
# Session supervisor, started by ffs_probe right after it acknowledged the radio's `40 51` (role switch request).
#
#  1. phone -> USB host (Exynos dwc3 OTG FSM id=0, like an OTG adapter's grounded ID; power_role is never written)
#  2. wait for the radio to appear as a USB device (iAP interface + CDC NCM)
#  3. start the chain: iAP2 link, eth0 policy route, CarPlay sender, projector (trusted virtual display, audio, keyboard,
#     panel policy), launcher + Back/Home capsule, touch injector
#  4. watch the radio device every 0.5 s: when it disappears (cable pulled, radio off) or the session dies, tear
#     everything down at once and go back to USB device mode so the radio can reconnect immediately
#
# Single-flight (the radio retries every few seconds and ffs_probe may start this more than once).
L=/data/local/tmp/swap.log; O=/sys/devices/platform/10c00000.usb/10c00000.dwc3; P=/sys/class/typec/port0
LOCK=/data/local/tmp/swap.lock
mkdir $LOCK 2>/dev/null || exit 0
t() { cut -d' ' -f1 /proc/uptime; }
log() { echo "$(t) $*" >> $L; }
radio_dev() {   # prints the sysfs path of the first non-root-hub USB device, if any
  for d in /sys/bus/usb/devices/[0-9]*-[0-9]*; do
    case "$d" in *:*) continue;; esac
    [ -f $d/idVendor ] && [ "$(cat $d/idVendor)" != 1d6b ] && { echo $d; return; }
  done
}
cleanup() {
  [ -n "$CLEANED" ] && return; CLEANED=1
  log "cleanup: $1"
  for n in carplay_phone usb_host_iap2 mdns_probe; do for pid in $(pidof $n 2>/dev/null); do kill $pid 2>/dev/null; done; done
  am force-stop dev.lxndr.launcher
  for pid in $(ps -A | grep app_process | awk '{print $2}'); do kill $pid 2>/dev/null; done
  ip -6 rule del pref 17001 2>/dev/null
  echo 1 > $O/id
  svc power stayon true; input keyevent KEYCODE_WAKEUP    # the panel policy is the projector's; make sure it is not left dark
  log "back in device mode (id=1), ready for the next attempt"
  rmdir $LOCK
}
trap 'cleanup "signal"; exit 0' TERM INT

svc power stayon true; locksettings set-disabled true >/dev/null 2>&1; input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard
rm -f /data/local/tmp/touch.fifo; mkfifo /data/local/tmp/touch.fifo; chmod 666 /data/local/tmp/touch.fifo
logcat -c; dmesg -c >/dev/null 2>&1
for f in iap mdns airplay; do echo "=== attempt at uptime $(t) ===" >> /data/local/tmp/$f.log; done
log "attempt start: id=$(cat $O/id) state=$(cat $O/state) dr=$(cat $P/data_role) pr=$(cat $P/power_role)"

echo 0 > $O/id; log "phone is USB host (id=0)"

# 2. wait for the radio's gadget
DEV=""
for k in $(seq 1 80); do
  DEV=$(radio_dev); [ -n "$DEV" ] && break
  sleep 0.5
done
[ -z "$DEV" ] && { cleanup "the radio never appeared as a USB device"; exit 0; }
DEVNUM=$(cat $DEV/devnum); BUS=$(cat $DEV/busnum)
sleep 2
log "radio device $(basename $DEV) $(cat $DEV/idVendor):$(cat $DEV/idProduct) '$(cat $DEV/manufacturer 2>/dev/null)' ifs=$(cat $DEV/bNumInterfaces 2>/dev/null) speed=$(cat $DEV/speed 2>/dev/null)"
log "DESC $(od -An -tx1 -v $DEV/descriptors 2>/dev/null | tr -d '\n' | cut -c1-1200)"
for i in $DEV/*:*; do log "IFACE $(basename $i) class=$(cat $i/bInterfaceClass)/$(cat $i/bInterfaceSubClass)/$(cat $i/bInterfaceProtocol) eps=$(cat $i/bNumEndpoints) str='$(cat $i/interface 2>/dev/null)' drv=$(basename $(readlink $i/driver 2>/dev/null) 2>/dev/null)"; done

# 3. the chain (everything runs "forever"; this supervisor stops it)
AL0=$(wc -l < /data/local/tmp/airplay.log 2>/dev/null); AL0=${AL0:-0}   # lines before this attempt, to recognise OUR "streaming" line later
up() { cut -d. -f1 /proc/uptime; }
[ -x /data/local/tmp/usb_host_iap2 ] && /data/local/tmp/usb_host_iap2 $BUS $DEVNUM 86400 >> /data/local/tmp/iap.log 2>&1 &
( for k in 1 2 3 4 5 6 7 8 9 10; do ip link show eth0 >/dev/null 2>&1 && break; sleep 1; done
  ip -6 route replace fe80::/64 dev eth0 table 4242 2>>$L
  ip -6 rule del pref 17001 2>/dev/null; ip -6 rule add pref 17001 iif lo oif eth0 lookup 4242 2>>$L
  log "eth0 policy route installed"
  [ -x /data/local/tmp/mdns_probe ] && /data/local/tmp/mdns_probe eth0 20 >> /data/local/tmp/mdns.log 2>&1 ) &
( sleep 6; [ -x /data/local/tmp/carplay_phone ] && /data/local/tmp/carplay_phone eth0 86400 /data/local/tmp/fix.h264 /data/local/tmp/fix.pcm 47500 /data/local/tmp/touch.fifo 47501 >> /data/local/tmp/airplay.log 2>&1 ) &
rm -f /data/local/tmp/projector.log
nohup sh -c "CLASSPATH=/data/local/tmp/projector.dex app_process / dev.lxndr.projector.Projector 1280 720 160 30 4000 47500 86400 47501 > /data/local/tmp/projector.log 2>&1" >/dev/null 2>&1 &
sleep 4
PD=$(grep -o "display=[0-9]*" /data/local/tmp/projector.log | tail -1 | cut -d= -f2); log "projection display=$PD"
if [ -n "$PD" ]; then
  if pm path dev.lxndr.launcher >/dev/null 2>&1; then
    appops set dev.lxndr.launcher SYSTEM_ALERT_WINDOW allow
    am start --display $PD -n dev.lxndr.launcher/.app.SplashActivity >> /data/local/tmp/proj.log 2>&1   # branded splash until the session streams
  else
    am start --display $PD -n com.android.settings/.Settings >> /data/local/tmp/proj.log 2>&1
  fi
  nohup sh -c "CLASSPATH=/data/local/tmp/inject.dex app_process / dev.lxndr.input.shell.InjectMain --display $PD --size 1280 720 --listen 47600 < /data/local/tmp/touch.fifo >> /data/local/tmp/inject.log 2>&1" >/dev/null 2>&1 &
fi
SPLASH_T0=$(up); HOME_SHOWN=0

# 4. supervise: the radio device must stay (same device number) and the sender must stay alive
GONE=0; DEAD=0
while :; do
  sleep 0.5
  D2=$(radio_dev)
  if [ -z "$D2" ] || [ "$(cat $D2/devnum 2>/dev/null)" != "$DEVNUM" ]; then GONE=$((GONE+1)); else GONE=0; fi
  [ $GONE -ge 2 ] && { cleanup "radio USB device gone"; exit 0; }
  if [ -z "$(pidof carplay_phone)" ] && [ -z "$(pidof usb_host_iap2)" ]; then DEAD=$((DEAD+1)); else DEAD=0; fi
  [ $DEAD -ge 20 ] && { cleanup "sender and iAP2 link both ended"; exit 0; }
  # splash -> launcher: once the session streams (and the splash was up for at least 3 s), or after 45 s regardless
  if [ $HOME_SHOWN = 0 ] && [ -n "$PD" ]; then
    AGE=$(( $(up) - SPLASH_T0 ))
    STREAMING=0; tail -n +$((AL0+1)) /data/local/tmp/airplay.log 2>/dev/null | grep -q "client state=2" && STREAMING=1
    if { [ $STREAMING = 1 ] && [ $AGE -ge 3 ]; } || [ $AGE -ge 45 ]; then
      am start --display $PD -n dev.lxndr.launcher/.app.LauncherActivity >> /data/local/tmp/proj.log 2>&1
      log "launcher shown (streaming=$STREAMING, splash age ${AGE}s)"; HOME_SHOWN=1
    fi
  fi
done
