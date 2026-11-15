#!/system/bin/sh
# s10carosd: USB mode manager (runs as root on the phone). See docs/mode-manager.md.
#
#   ADB state   (default): Samsung ids, adb, our FunctionFS function composed in but idle. A PC never sees Apple ids.
#   CARPLAY     : Apple ids. Entered only when a head unit probes the phone (Android Open Accessory probe `c0 33`,
#                 or an Apple probe `c0 53` if ids were already Apple). The session itself is swap_to_host_hook.sh.
#
# Rules that keep adb reachable:
#   * ADB is the state at start and the state after every session or timeout.
#   * CARPLAY never lasts more than CARPLAY_GRACE seconds without a radio request and never outlives a session.
#   * Only ids/strings change between states (no function add/remove, no HAL toggle).
#   * A guard rebinds the UDC if it stays empty. Kill switch: touch /data/local/tmp/s10caros.off
# usage: [NOSWITCH=1] [SOLE=1] [SWEEP=1] s10carosd.sh [start|stop|status]     (NOSWITCH=1: only log what it would do)
D=/data/local/tmp; G=/config/usb_gadget/g1; T=/sys/kernel/tracing
OFF=$D/s10caros.off; LOG=$D/s10carosd.log; PIDF=$D/s10carosd.pid; STATE=$D/s10carosd.state
CARPLAY_GRACE=${CARPLAY_GRACE:-25}
GENERIC=${GENERIC:-1}          # unknown-host trigger (hosts that never send the AOA probe, e.g. the Chevrolet radio)
GENERIC_WAIT=${GENERIC_WAIT:-8}  # seconds the USB must stay configured with no adb connection before trying CARPLAY
GENERIC_COOLDOWN=${GENERIC_COOLDOWN:-120}
SWEEP=${SWEEP:-1}              # every other c0 53 answered with a different value (data for hosts that stop after c0 53)
UDC=$(ls /sys/class/udc | head -1)
t() { cut -d' ' -f1 /proc/uptime; }
log() { echo "$(t) $*" >> $LOG; }
setids() {   # vid pid manufacturer product serial (UDC cycle only)
  echo "" > $G/UDC; sleep 1
  # SOLE=1 (experiment): hide the adb interface from the radio while in CARPLAY; put it back when returning to ADB.
  if [ "$1" = 0x05ac ] && [ "$SOLE" = 1 ]; then rm -f $G/configs/b.1/function0; fi
  if [ "$1" = 0x04e8 ] && [ ! -e $G/configs/b.1/function0 ]; then ln -s $G/functions/ffs.adb $G/configs/b.1/function0; fi
  echo $1 > $G/idVendor; echo $2 > $G/idProduct
  echo "$3" > $G/strings/0x409/manufacturer; echo "$4" > $G/strings/0x409/product
  [ -n "$5" ] && echo "$5" > $G/strings/0x409/serialnumber
  echo $UDC > $G/UDC
}
# Android's USB service rebuilds the gadget configuration whenever it changes mode (e.g. when Zlink's AOA `40 35` makes it enter
# accessory mode); that drops our link. Put it back (and rebuild the FunctionFS side if the daemon or the function is gone).
ensure_composed() {   # $1 = force: also strip links Android added (accessory function etc.) so the radio sees only adb + ours
  if [ "$1" != force ] && [ -e $G/configs/b.1/s10_carplay ] && [ -d $G/functions/ffs.carplay ] && [ -n "$(pidof ffs_probe)" ]; then return 0; fi
  log "recompose: link=$([ -e $G/configs/b.1/s10_carplay ] && echo yes || echo NO) fn=$([ -d $G/functions/ffs.carplay ] && echo yes || echo NO) ffs_probe=$([ -n "$(pidof ffs_probe)" ] && echo yes || echo NO)"
  echo "" > $G/UDC; sleep 1
  for p in $(pidof ffs_probe 2>/dev/null); do kill $p; done; umount /dev/ffs-carplay 2>/dev/null
  for l in $G/configs/b.1/*; do [ -L $l ] || continue; case "$(basename $l)" in function0) ;; *) log "removing foreign link $(basename $l)"; rm -f $l;; esac; done
  rm -f $G/configs/b.1/s10_carplay; rmdir $G/functions/ffs.carplay 2>/dev/null
  mkdir $G/functions/ffs.carplay
  S10_REPLY_SWEEP=$SWEEP S10_SWAP_HOOK=$D/swap_to_host_hook.sh nohup $D/ffs_probe carplay 2592000 > $D/ffs_events.log 2>&1 &
  sleep 2
  ln -s $G/functions/ffs.carplay $G/configs/b.1/s10_carplay
  [ -e $G/configs/b.1/function0 ] || ln -s $G/functions/ffs.adb $G/configs/b.1/function0
  echo $UDC > $G/UDC; sleep 1
  log "recomposed (cfg=$(ls $G/configs/b.1 | tr '\n' ' '))"
}
to_adb()     { setids 0x04e8 0x685d samsung SM-G975F "$SER_ORIG"; echo adb > $STATE; log "state ADB"; }
to_carplay() { setids 0x05ac 0x12a8 "Apple Inc." iPhone "$SER_APPLE"; echo carplay > $STATE; CP_SINCE=$(cut -d. -f1 /proc/uptime); log "state CARPLAY ($1)"; }

kill_tracers() { for p in $(ps -A -o PID,ARGS | grep "[c]at /sys/kernel/tracing/trace_pipe" | awk '{print $1}'); do kill $p 2>/dev/null; done; }

stop_all() {
  kill_tracers
  touch $D/s10carosd.stop
  [ -f $PIDF ] && kill $(cat $PIDF) 2>/dev/null
  for n in ffs_probe; do for p in $(pidof $n 2>/dev/null); do kill $p 2>/dev/null; done; done
  [ -d $D/swap.lock ] && rmdir $D/swap.lock
  sleep 1
  $D/gadget_manager --compose-into g1 teardown >> $LOG 2>&1
  umount /dev/ffs-carplay 2>/dev/null
  setids 0x04e8 0x685d samsung SM-G975F "$SER_ORIG" 2>/dev/null
  rm -f $PIDF
  log "stopped, stock config restored (udc=$(cat $G/UDC))"
}

# The real device serial, never the value currently on the gadget (that may already be the Apple one, which is how a wrong
# "original" got saved once).
real_serial() { s=$(getprop ro.boot.serialno); [ -z "$s" ] && s=$(getprop ro.serialno); echo "$s"; }
case "$1" in
stop) SER_ORIG=$(real_serial); stop_all; exit 0;;
status) echo "state=$(cat $STATE 2>/dev/null) pid=$(cat $PIDF 2>/dev/null) udc=$(cat $G/UDC) ids=$(cat $G/idVendor):$(cat $G/idProduct) cfg=$(ls $G/configs/b.1 | tr '\n' ' ')"; tail -5 $LOG 2>/dev/null; exit 0;;
start) ;;
*) echo "usage: $0 start|stop|status"; exit 2;;
esac

[ -f $PIDF ] && kill -0 $(cat $PIDF) 2>/dev/null && { echo "already running"; exit 0; }
[ -f $OFF ] && { echo "kill switch present ($OFF)"; exit 1; }
rm -f $D/s10carosd.stop
echo $$ > $PIDF
SER_ORIG=$(real_serial)
SER_APPLE=00008030-001A2B3C4D5E6F78    # iPhone-style 25-char serial (made-up value)
log "start: ids=$(cat $G/idVendor):$(cat $G/idProduct) udc=$(cat $G/UDC) serial_orig=$SER_ORIG"

# compose our function next to adb. Its interface class is ff/00/00 (NOT adb's ff/42/01): a PC's adb picks the first interface
# matching ff/42/01, and when ours came first adb stopped seeing the phone (seen 2026-11-15, order flips after a cable cycle)
# compose our function next to adb (FunctionFS must have its descriptors before the UDC is bound again)
$D/gadget_manager --compose-into g1 setup >> $LOG 2>&1
S10_REPLY_SWEEP=$SWEEP S10_SWAP_HOOK=$D/swap_to_host_hook.sh nohup $D/ffs_probe carplay 2592000 > $D/ffs_events.log 2>&1 &
sleep 2
$D/gadget_manager --compose-into g1 bind >> $LOG 2>&1
echo adb > $STATE
log "composed; state ADB (cfg=$(ls $G/configs/b.1 | tr '\n' ' '))"

# trace: remember the head unit's probes (AOA getProtocol c0 33 / Apple c0 53) in flag files
kill_tracers
echo 0 > $T/tracing_on 2>/dev/null; echo > $T/trace
echo 1 > $T/events/dwc3/dwc3_ctrl_req/enable; echo 1 > $T/tracing_on
rm -f $D/s10.aoa $D/s10.apple; : > $D/s10carosd.ctrl.log; CN=0
( cat $T/trace_pipe | while read -r line; do
    case "$line" in
      *"c0 33 00 00 00 00 02 00"*) touch $D/s10.aoa; echo "$(cut -d' ' -f1 /proc/uptime) saw AOA probe c0 33" >> $LOG;;
      *"c0 53 00 00 00 00 04 00"*) touch $D/s10.apple; echo "$(cut -d' ' -f1 /proc/uptime) saw Apple probe c0 53" >> $LOG;;
    esac
    # any non-standard request (printed as raw bytes, not as a name): keep a short record of what each host asks us
    case "$line" in
      *"dwc3_ctrl_req: "[0-9a-f][0-9a-f]" "[0-9a-f][0-9a-f]" "*) CN=$((CN+1)); [ $CN -le 400 ] && echo "$(cut -d' ' -f1 /proc/uptime) state=$(cat $STATE 2>/dev/null) ${line##*dwc3_ctrl_req: }" >> $D/s10carosd.ctrl.log;;
    esac
  done ) &
TRACER=$!

# guard: gadget unbound for 2 reads 2 s apart (and not in a deliberate switch) -> bind it again
( while :; do sleep 2; [ -f $D/s10carosd.stop ] && exit 0
    a=$(cat $G/UDC 2>/dev/null)
    if [ -z "$a" ] && [ ! -f $D/s10.switching ]; then sleep 2; b=$(cat $G/UDC 2>/dev/null); [ -z "$b" ] && { echo $UDC > $G/UDC 2>/dev/null; log "guard rebound the UDC"; }; fi
  done ) &
GUARD=$!

trap 'stop_all; kill $TRACER $GUARD 2>/dev/null; exit 0' TERM INT
SERVED=0; NA=0; TICK=0; MISS=0; CFG=0; LAST_GENERIC=-9999
while :; do
  sleep 1
  [ -f $OFF ] && { log "kill switch: stopping"; stop_all; kill $TRACER $GUARD 2>/dev/null; exit 0; }
  [ -f $D/s10carosd.stop ] && exit 0
  S=$(cat $STATE)
  TICK=$((TICK+1))
  if [ $((TICK % 30)) = 0 ]; then log "alive: state=$S udc=$(cat $G/UDC) udc_state=$(cat /sys/class/udc/$UDC/state 2>/dev/null) ids=$(cat $G/idVendor):$(cat $G/idProduct) link=$([ -e $G/configs/b.1/s10_carplay ] && echo yes || echo NO) trace_on=$(cat $T/tracing_on)"; fi
  if [ "$S" = adb ]; then
    # Android entered accessory mode (ids 18d1:xxxx) because a head unit asked for it: that is an AOA probe even if the
    # trace missed it. Note it once per episode.
    if [ "$(cat $G/idVendor)" = 0x18d1 ] && [ ! -f $D/s10.switching ]; then
      [ -f $D/s10.aoa ] || { log "ids are $(cat $G/idVendor):$(cat $G/idProduct) (accessory mode): treating as an AOA probe"; touch $D/s10.aoa; }
    fi
    # keep our function linked in ADB state too (the HAL drops it on every mode change); only when nothing else is going on
    if [ ! -e $G/configs/b.1/s10_carplay ] && [ ! -f $D/s10.aoa ] && [ ! -f $D/s10.switching ]; then MISS=$((MISS+1)); else MISS=0; fi
    if [ $MISS -ge 8 ]; then MISS=0; touch $D/s10.switching; ensure_composed; rm -f $D/s10.switching; fi
    # unknown host: configured for GENERIC_WAIT s with nobody using adb -> not a dev PC, try CARPLAY (once per cooldown)
    if [ "$GENERIC" = 1 ] && [ ! -f $D/s10.aoa ]; then
      if [ "$(cat /sys/class/udc/$UDC/state 2>/dev/null)" = configured ]; then CFG=$((CFG+1)); else CFG=0; fi
      if [ $CFG -ge $GENERIC_WAIT ]; then
        NOW=$(cut -d. -f1 /proc/uptime)
        if [ $((NOW - LAST_GENERIC)) -ge $GENERIC_COOLDOWN ] && ! dumpsys adb 2>/dev/null | grep -q "connected_to_adb=true"; then
          LAST_GENERIC=$NOW; CFG=0
          if [ "$NOSWITCH" = 1 ]; then log "NOSWITCH: would try CARPLAY (unknown host, no adb)"; else
            log "unknown host (configured ${GENERIC_WAIT}s, no adb connection): trying CARPLAY"
            touch $D/s10.switching; ensure_composed; to_carplay "unknown host"; rm -f $D/s10.switching
          fi
        else CFG=0; fi
      fi
    fi
    if [ -f $D/s10.aoa ]; then
      rm -f $D/s10.aoa $D/s10.apple
      if [ "$NOSWITCH" = 1 ]; then log "NOSWITCH: would switch to CARPLAY now"; else
        touch $D/s10.switching
        sleep 3                       # let Android finish its accessory-mode attempt and settle back to adb
        ensure_composed force
        to_carplay "head unit probed us (AOA)"; rm -f $D/s10.switching
      fi
    fi
  else   # carplay
    if [ -d $D/swap.lock ]; then SERVED=1; continue; fi          # a session is running
    if [ $SERVED = 1 ]; then                                      # it just ended (cleanup removed the lock)
      SERVED=0; rm -f $D/s10.aoa $D/s10.apple
      touch $D/s10.switching; to_adb; rm -f $D/s10.switching; continue
    fi
    # cable pulled while waiting for a radio: back to ADB at once, before a PC can be plugged in and see Apple ids
    # (not during the first 6 s: the radio re-enumerates after our id switch and the UDC is briefly not attached then)
    if [ "$(cat /sys/class/udc/$UDC/state 2>/dev/null)" = "not attached" ] && [ $(( $(cut -d. -f1 /proc/uptime) - CP_SINCE )) -ge 6 ]; then NA=$((NA+1)); else NA=0; fi
    if [ $NA -ge 2 ]; then
      log "cable gone while in CARPLAY: back to ADB"
      NA=0; touch $D/s10.switching; to_adb; rm -f $D/s10.switching; continue
    fi
    NOW=$(cut -d. -f1 /proc/uptime)
    if [ -f $D/s10.apple ]; then rm -f $D/s10.apple; CP_SINCE=$NOW; fi   # radio is talking Apple: keep waiting
    if [ $((NOW - CP_SINCE)) -ge $CARPLAY_GRACE ]; then
      log "no radio request for ${CARPLAY_GRACE}s: back to ADB"
      touch $D/s10.switching; to_adb; rm -f $D/s10.switching
    fi
  fi
done
