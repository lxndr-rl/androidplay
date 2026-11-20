#!/system/bin/sh
# androidplayd: USB mode manager (runs as root on the phone). See docs/mode-manager.md.
#
#   ADB state   (default): Samsung ids, adb, our FunctionFS function composed in but idle. A PC never sees Apple ids.
#   CARPLAY     : Apple ids. Entered only when a head unit probes the phone (Android Open Accessory probe `c0 33`,
#                 or an Apple probe `c0 53` if ids were already Apple). The session itself is swap_to_host_hook.sh.
#
# v2 ("armed"): the radio must see Apple ids the instant it is plugged in (Zlink ignores a new device while it is busy with
# its own Android Auto attempt, so reacting to its probe is too late). Therefore the idle state depends on where the phone is:
#   connected to the home Wi-Fi (SSID in /data/local/tmp/androidplay.home_ssid)  -> ADB   (a PC never sees Apple ids)
#   anywhere else (or SSID file missing => ADB, the safe default is NOT armed)  -> CARPLAY armed (Apple ids, waiting for a radio)
# Overrides: touch /data/local/tmp/androidplay.home (force ADB) or androidplay.car (force armed). While armed, a host that turns out to
# be a PC (it closes the configuration right after opening it) or that sends no Apple request for CARPLAY_GRACE s makes the
# phone drop to ADB until the cable is pulled. The idle state is only changed while no cable is attached.
#
# Rules that keep adb reachable:
#   * ADB is the state at start and the state after every session or timeout.
#   * CARPLAY never lasts more than CARPLAY_GRACE seconds without a radio request and never outlives a session.
#   * Only ids/strings change between states (no function add/remove, no HAL toggle).
#   * A guard rebinds the UDC if it stays empty. Kill switch: touch /data/local/tmp/androidplay.off
# env: FFS_MODE=clone|noeps (ffs_probe mode; default: ff/00/00 with two bulk endpoints)
# usage: APPLE_ID_OK=own_radio_only [NOSWITCH=1] [SOLE=1] [SWEEP=1] androidplayd.sh [start|stop|status]     (NOSWITCH=1: only log what it would do)
D=/data/local/tmp; G=/config/usb_gadget/g1; T=/sys/kernel/tracing; O=/sys/devices/platform/10c00000.usb/10c00000.dwc3
OFF=$D/androidplay.off; LOG=$D/androidplayd.log; PIDF=$D/androidplayd.pid; STATE=$D/androidplayd.state
CARPLAY_GRACE=${CARPLAY_GRACE:-25}
GENERIC=${GENERIC:-1}          # unknown-host trigger (hosts that never send the AOA probe, e.g. the Chevrolet radio)
GENERIC_WAIT=${GENERIC_WAIT:-8}  # seconds the USB must stay configured with no adb connection before trying CARPLAY
GENERIC_COOLDOWN=${GENERIC_COOLDOWN:-120}
SWEEP=${SWEEP:-0}              # OFF by default: every other c0 53 would get a different value, which can sabotage a radio that asks only twice
                               # (Captiva 2026-11-17). Per-radio answers are chosen by learning mode / profiles instead (learn.sh).
UDC=$(ls /sys/class/udc | head -1)
t() { cut -d' ' -f1 /proc/uptime; }
log() { echo "$(t) $*" >> $LOG; }
# Pre-flight indicator: a notification on the phone and a status file say what the manager is doing right now, so nobody has to guess
# before driving ("Armed: waiting for the radio" is the only state worth leaving the house with).
notify() { echo "$(date +%H:%M:%S 2>/dev/null) $*" > $D/androidplayd.status; cmd notification post -S bigtext -t AndroidPlay androidplay "$*" >/dev/null 2>&1; }
# per-radio learning and evidence archive (optional files: without them the manager behaves as before)
[ -f $D/learn.sh ] && . $D/learn.sh
type learn_active >/dev/null 2>&1 || { learn_active() { return 1; }; learn_fail() { :; }; learn_success() { :; }; profile_apply() { :; }; }
archive() { [ -x $D/archive_logs.sh ] && $D/archive_logs.sh "$1" >/dev/null 2>&1; }
want_state() {
  [ -f $D/androidplay.home ] && { echo adb; return; }
  [ -f $D/androidplay.car ] && { echo carplay; return; }
  h=$(cat $D/androidplay.home_ssid 2>/dev/null)
  [ -z "$h" ] && { echo adb; return; }                                   # not configured: stay a normal phone
  if cmd wifi status 2>/dev/null | grep -q "connected to \"$h\""; then echo adb; else echo carplay; fi
}
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
  S10_REPLY_SWEEP=$SWEEP S10_SWAP_HOOK=$D/swap_to_host_hook.sh nohup $D/ffs_probe carplay 2592000 $FFS_MODE > $D/ffs_events.log 2>&1 &
  sleep 2
  ln -s $G/functions/ffs.carplay $G/configs/b.1/s10_carplay
  [ -e $G/configs/b.1/function0 ] || ln -s $G/functions/ffs.adb $G/configs/b.1/function0
  echo $UDC > $G/UDC; sleep 1
  log "recomposed (cfg=$(ls $G/configs/b.1 | tr '\n' ' '))"
}
to_adb()     { setids 0x04e8 0x685d samsung SM-G975F "$SER_ORIG"; echo adb > $STATE; log "state ADB"; notify "NOT armed: plain ADB phone (PC or home). Unplug and wait 10 s to arm."; }
to_carplay() { setids 0x05ac 0x12a8 "Apple Inc." iPhone "$SER_APPLE"; echo carplay > $STATE; CP_SINCE=$(cut -d. -f1 /proc/uptime); CP_ENTER=$CP_SINCE; rm -f $D/s10.cfg0; log "state CARPLAY ($1)"; notify "ARMED: waiting for the radio ($1)"; }

kill_tracers() { for p in $(ps -A -o PID,ARGS | grep "[c]at /sys/kernel/tracing/trace_pipe" | awk '{print $1}'); do kill $p 2>/dev/null; done; }

kill_ctl() { for p in $(ps -A -o PID,ARGS | grep "[d]ev.lxndr.ctl.CtlServer" | awk '{print $1}'); do kill $p 2>/dev/null; done; }

stop_all() {
  kill_tracers; kill_ctl
  touch $D/androidplayd.stop
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

# a PID file from before a reboot can name an unrelated live process: check the command line too
OLDPID=$(cat $PIDF 2>/dev/null)
[ -n "$OLDPID" ] && [ "$OLDPID" != "$$" ] && grep -q androidplayd /proc/$OLDPID/cmdline 2>/dev/null && { echo "already running"; exit 0; }
# Presenting Apple USB ids is an opt-in experiment for the owner's own radio: refuse to start without it.
[ "$NOSWITCH" = 1 ] || [ "$APPLE_ID_OK" = own_radio_only ] || { echo "refused: set APPLE_ID_OK=own_radio_only (see docs/installation.md)"; exit 1; }
[ -f $OFF ] && { echo "kill switch present ($OFF)"; exit 1; }
# At boot the USB service may not have built the gadget yet: wait for it (up to 120 s) before touching anything.
i=0; while [ $i -lt 120 ]; do [ -d $G ] && [ -n "$(ls /sys/class/udc 2>/dev/null)" ] && [ -n "$(cat $G/UDC 2>/dev/null)" ] && break; i=$((i+1)); sleep 1; done
UDC=$(ls /sys/class/udc | head -1)
rm -f $D/androidplayd.stop
echo $$ > $PIDF
SER_ORIG=$(real_serial)
[ -f $D/androidplay.nosole ] && SOLE=0     # experiment switch: keep the adb interface linked while armed
SER_APPLE=00008030-001A2B3C4D5E6F78    # iPhone-style 25-char serial (made-up value)
log "start: ids=$(cat $G/idVendor):$(cat $G/idProduct) udc=$(cat $G/UDC) serial_orig=$SER_ORIG"

# keep what the previous run left behind (the next lines truncate/overwrite the per-session logs), then pick this radio's answer
archive boot
profile_apply
# compose our function next to adb. Its interface class is ff/00/00 (NOT adb's ff/42/01): a PC's adb picks the first interface
# matching ff/42/01, and when ours came first adb stopped seeing the phone (seen 2026-11-15, order flips after a cable cycle)
# compose our function next to adb (FunctionFS must have its descriptors before the UDC is bound again)
$D/gadget_manager --compose-into g1 setup >> $LOG 2>&1
S10_REPLY_SWEEP=$SWEEP S10_SWAP_HOOK=$D/swap_to_host_hook.sh nohup $D/ffs_probe carplay 2592000 $FFS_MODE > $D/ffs_events.log 2>&1 &
sleep 2
# Every UDC unbind/bind is a disconnect+connect the host has to enumerate again, and on this phone repeated cycles make the host give up
# ("device descriptor read error -71", "unable to enumerate": 1 good enumeration in 10 cycles on the bench, 2026-11-18). So when the phone is to
# be armed anyway, bind ONCE, straight into the Apple identity, instead of binding as ADB first and switching a second later.
START_ARMED=0
if [ "$NOSWITCH" != 1 ] && [ "$(want_state)" = carplay ]; then
  START_ARMED=1
  # same steps as setids, except that the single bind is done by `gadget_manager bind`, which ALSO links the s10_carplay function into the
  # configuration (binding the UDC without that link leaves a configuration with no functions: no USB at all)
  [ "$SOLE" = 1 ] && rm -f $G/configs/b.1/function0
  echo 0x05ac > $G/idVendor; echo 0x12a8 > $G/idProduct
  echo "Apple Inc." > $G/strings/0x409/manufacturer; echo iPhone > $G/strings/0x409/product; echo "$SER_APPLE" > $G/strings/0x409/serialnumber
  $D/gadget_manager --compose-into g1 bind >> $LOG 2>&1
  echo carplay > $STATE; CP_SINCE=$(cut -d. -f1 /proc/uptime); CP_ENTER=$CP_SINCE; rm -f $D/s10.cfg0
  log "state CARPLAY (armed at start; one bind, cfg=$(ls $G/configs/b.1 | tr '\n' ' '))"; notify "ARMED: waiting for the radio (armed at start)"
else
  $D/gadget_manager --compose-into g1 bind >> $LOG 2>&1
  echo adb > $STATE
  log "composed; state ADB (cfg=$(ls $G/configs/b.1 | tr '\n' ' '))"
fi

# trace: remember the head unit's probes (AOA getProtocol c0 33 / Apple c0 53) in flag files
kill_tracers
echo 0 > $T/tracing_on 2>/dev/null; echo > $T/trace
echo 1 > $T/events/dwc3/dwc3_ctrl_req/enable; echo 1 > $T/tracing_on
rm -f $D/s10.aoa $D/s10.apple; : > $D/androidplayd.ctrl.log; CN=0
( while :; do
    [ -f $D/androidplayd.stop ] && exit 0
    echo 1 > $T/events/dwc3/dwc3_ctrl_req/enable 2>/dev/null; echo 1 > $T/tracing_on 2>/dev/null
    cat $T/trace_pipe | while read -r line; do
    case "$line" in
      *"c0 33 00 00 00 00 02 00"*) touch $D/s10.aoa; echo "$(cut -d' ' -f1 /proc/uptime) saw AOA probe c0 33" >> $LOG;;
      *"Set Configuration(Config = 0)"*) touch $D/s10.cfg0;;
      *"c0 53 00 00 00 00 04 00"*) touch $D/s10.apple; echo "$(cut -d' ' -f1 /proc/uptime) saw Apple probe c0 53" >> $LOG;;
    esac
    # any non-standard request (printed as raw bytes, not as a name): keep a short record of what each host asks us
    case "$line" in
      *"dwc3_ctrl_req: "[0-9a-f][0-9a-f]" "[0-9a-f][0-9a-f]" "*) CN=$((CN+1)); [ $CN -le 400 ] && echo "$(cut -d' ' -f1 /proc/uptime) state=$(cat $STATE 2>/dev/null) ${line##*dwc3_ctrl_req: }" >> $D/androidplayd.ctrl.log;;
    esac
  done
    echo "$(cut -d' ' -f1 /proc/uptime) trace reader ended (EOF); restarting" >> $LOG
    sleep 0.2
  done ) &
TRACER=$!

# guard: gadget unbound for 2 reads 2 s apart (and not in a deliberate switch) -> bind it again
( while :; do sleep 2; [ -f $D/androidplayd.stop ] && exit 0
    a=$(cat $G/UDC 2>/dev/null)
    # only when a cable partner is present: with no cable Android leaves the UDC unbound on purpose
    if [ -z "$a" ] && [ ! -f $D/s10.switching ] && [ ! -f $D/androidplay.noguard ] && [ -e /sys/class/typec/port0-partner ]; then sleep 2; b=$(cat $G/UDC 2>/dev/null); [ -z "$b" ] && { echo $UDC > $G/UDC 2>/dev/null; log "guard rebound the UDC"; }; fi
  done ) &
GUARD=$!

# TEST AID (off unless androidplay.ctl exists): lets the AndroidPlay Control screen on the phone read state and switch modes without adb.
# Loopback only and token protected, see services/Control. Not part of a release image.
kill_ctl
if [ -f $D/androidplay.ctl ] && [ -f $D/ctl.dex ] && [ -f $D/androidplay.token ]; then
  CLASSPATH=$D/ctl.dex nohup app_process / dev.lxndr.ctl.CtlServer 47650 $D/androidplay.token > $D/ctl.log 2>&1 &
  log "control server on 127.0.0.1:47650 (test aid)"
fi

trap 'stop_all; kill $TRACER $GUARD 2>/dev/null; exit 0' TERM INT
SERVED=0; NA=0; TICK=0; MISS=0; CFG=0; LAST_GENERIC=-9999; ARMED=0; NAI=0; CP_ENTER=0; CP_SINCE=$(cut -d. -f1 /proc/uptime); WANT_AT=0; TEMP_ADB=0
EP_APPLE=0; EP_OK=0; EP_AOA=0   # what the radio did in this plug episode (learning mode and the archive use it)
LAST_LS=""   # link-state log
# Armed from the first second. Booting (or starting) with the cable already in a radio used to leave the phone as plain ADB until the cable was
# pulled: the radio then probed AOA, saw an Android and gave up before the slow AOA->CARPLAY switch finished (Captiva, 2026-11-17). A PC
# plugged at that moment is not a problem: its SET_CONFIGURATION 0 (or CARPLAY_GRACE s without an Apple request) returns us to ADB.
[ $START_ARMED = 1 ] && { ARMED=1; TEMP_ADB=0; CP_ENTER=$(cut -d. -f1 /proc/uptime); CP_SINCE=$CP_ENTER; rm -f $D/s10.cfg0 $D/s10.apple $D/s10.aoa; }
while :; do
  sleep 1
  [ -f $OFF ] && { log "kill switch: stopping"; stop_all; kill $TRACER $GUARD 2>/dev/null; exit 0; }
  [ -f $D/androidplayd.stop ] && exit 0
  S=$(cat $STATE)
  TICK=$((TICK+1))
  # commands from the AndroidPlay Control screen (test aid): arm now / ADB now
  if [ -f $D/androidplay.cmd ]; then
    UICMD=$(cat $D/androidplay.cmd 2>/dev/null); rm -f $D/androidplay.cmd
    case "$UICMD" in
      arm) if [ "$S" = adb ] && [ "$NOSWITCH" != 1 ]; then log "ui: arm now"; touch $D/s10.switching; ensure_composed; to_carplay "armed (from the UI)"; ARMED=1; TEMP_ADB=0; rm -f $D/s10.switching; fi; continue;;
      adb) if [ "$S" = carplay ] && [ "$NOSWITCH" != 1 ]; then log "ui: ADB now"; touch $D/s10.switching; to_adb; ARMED=0; TEMP_ADB=1; rm -f $D/s10.switching; fi; continue;;
    esac
  fi
  if [ $((TICK % 30)) = 0 ]; then log "alive: state=$S udc=$(cat $G/UDC) udc_state=$(cat /sys/class/udc/$UDC/state 2>/dev/null) ids=$(cat $G/idVendor):$(cat $G/idProduct) link=$([ -e $G/configs/b.1/s10_carplay ] && echo yes || echo NO) trace_on=$(cat $T/tracing_on)"; fi
  UST=$(cat /sys/class/udc/$UDC/state 2>/dev/null)
  # Link log (only on change): was the cable even seen? (The Chevrolet trip of 2026-11-18 showed 5 minutes armed with nothing, and with the
  # control-request log gone there was no way to tell a silent radio from an unseen cable.)
  DR=$(cat /sys/class/typec/port0/data_role 2>/dev/null); DR=${DR#*[}; DR=${DR%%]*}
  PR=$(cat /sys/class/typec/port0/power_role 2>/dev/null); PR=${PR#*[}; PR=${PR%%]*}
  LS="partner=$([ -e /sys/class/typec/port0-partner ] && echo y || echo n) data=$DR power=$PR udc=$UST bsess=$(cat $O/b_sess 2>/dev/null) usb_online=$(cat /sys/class/power_supply/usb/online 2>/dev/null)"
  [ "$LS" != "$LAST_LS" ] && { log "link: $LS"; LAST_LS=$LS; }
  if [ "$S" = adb ]; then
    # idle state: with no cable attached for 3 s, follow the location rule (armed away from home)
    if [ "$UST" = "not attached" ] && [ ! -f $D/s10.switching ]; then NAI=$((NAI+1)); else NAI=0; fi
    if [ $NAI -ge 3 ] && [ $((TICK % 5)) = 0 ] && [ "$NOSWITCH" != 1 ]; then
      if [ "$(want_state)" = carplay ]; then
        log "idle, no cable, away from home: arming CARPLAY"
        touch $D/s10.switching; ensure_composed; to_carplay "armed (away from home)"; ARMED=1; TEMP_ADB=0; rm -f $D/s10.switching; NAI=0; continue
      fi
      TEMP_ADB=0
    fi
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
      EP_AOA=1
      rm -f $D/s10.aoa $D/s10.apple
      if [ "$NOSWITCH" = 1 ]; then log "NOSWITCH: would switch to CARPLAY now"; else
        touch $D/s10.switching
        sleep 3                       # let Android finish its accessory-mode attempt and settle back to adb
        ensure_composed force
        to_carplay "head unit probed us (AOA)"; rm -f $D/s10.switching
      fi
    fi
  else   # carplay
    if [ -d $D/swap.lock ]; then SERVED=1; EP_OK=1; continue; fi   # a session is running
    if [ $SERVED = 1 ]; then                                      # it just ended (cleanup removed the lock)
      SERVED=0; rm -f $D/s10.aoa $D/s10.apple $D/s10.cfg0; archive session
      if [ $ARMED = 1 ]; then log "session ended; staying armed"; CP_SINCE=$(cut -d. -f1 /proc/uptime); continue; fi
      touch $D/s10.switching; to_adb; rm -f $D/s10.switching; continue
    fi
    NOW=$(cut -d. -f1 /proc/uptime)
    if [ "$UST" = "not attached" ]; then
      CP_SINCE=$NOW                                               # no host: nothing to wait for
      if [ $((NOW - CP_ENTER)) -ge 6 ]; then NA=$((NA+1)); else NA=0; fi
      rm -f $D/s10.cfg0
      if [ $NA -ge 2 ]; then
        # the plug episode is over: record what the radio did, and let learning mode try the next answer if it asked but never swapped
        if [ $EP_APPLE = 1 ] || [ $EP_OK = 1 ] || [ $EP_AOA = 1 ]; then
          log "episode over: aoa=$EP_AOA apple_probe=$EP_APPLE role_swap=$EP_OK"
          if learn_active; then
            if [ $EP_OK = 1 ]; then log "learn: $(learn_success)"
            elif [ $EP_APPLE = 1 ]; then log "learn: $(learn_fail)"; fi
          fi
          archive episode
        fi
        EP_APPLE=0; EP_OK=0; EP_AOA=0
        if [ $ARMED = 1 ]; then
          [ "$(want_state)" = adb ] && { log "armed but now at home: back to ADB"; NA=0; touch $D/s10.switching; to_adb; ARMED=0; rm -f $D/s10.switching; }
        else
          log "cable gone while in temporary CARPLAY: back to ADB"; NA=0; touch $D/s10.switching; to_adb; rm -f $D/s10.switching
        fi
        continue
      fi
    else
      NA=0
      if [ -f $D/s10.apple ]; then rm -f $D/s10.apple $D/s10.cfg0; CP_SINCE=$NOW; EP_APPLE=1; fi   # a radio is talking Apple: keep waiting
      # a PC closes the configuration it just opened (SET_CONFIGURATION 0): never leave it looking at Apple ids
      if [ -f $D/s10.cfg0 ] && [ $((NOW - CP_ENTER)) -ge 2 ]; then
        log "PC pattern (SET_CONFIGURATION 0) while armed: back to ADB until the cable is pulled"
        rm -f $D/s10.cfg0; TEMP_ADB=1; ARMED=0; touch $D/s10.switching; to_adb; rm -f $D/s10.switching; continue
      fi
      if [ $((NOW - CP_SINCE)) -ge $CARPLAY_GRACE ]; then
        log "host attached ${CARPLAY_GRACE}s without an Apple request: back to ADB until the cable is pulled"
        TEMP_ADB=1; ARMED=0; touch $D/s10.switching; to_adb; rm -f $D/s10.switching
      fi
    fi
  fi
done
