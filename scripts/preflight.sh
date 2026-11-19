#!/usr/bin/env bash
# GO / NO-GO before a trip. Run with the phone plugged into the PC. Read-only: it changes nothing.
#   scripts/preflight.sh
# While away from the PC the same answer is on the phone: the "AndroidPlay" notification says ARMED or NOT armed.
set -u
adb get-state >/dev/null 2>&1 || { echo "NO-GO: no phone on adb"; exit 1; }
adb root >/dev/null 2>&1 || true; sleep 1
D=/data/local/tmp
bad=0
ok()   { printf '  ok    %s\n' "$1"; }
fail() { printf '  FAIL  %s\n' "$1"; bad=$((bad+1)); }
note() { printf '  note  %s\n' "$1"; }
sh() { adb shell "$1" 2>/dev/null | tr -d '\r'; }
echo "AndroidPlay pre-flight"
[ "$(sh 'getprop init.svc.androidplayd')" = running ] && ok "manager service is running (starts by itself at boot)" || fail "manager service is NOT running"
[ -n "$(sh '[ -f /vendor/etc/init/androidplay.rc ] && echo y')" ] && ok "boot autostart is installed" || fail "boot autostart (androidplay.rc) is missing: scripts/install-autostart.sh"
[ -z "$(sh "[ -f $D/androidplay.off ] && echo y")" ] && ok "kill switch is not set" || fail "kill switch androidplay.off is set: the manager will not arm"
[ -z "$(sh "[ -f $D/androidplay.home ] && echo y")" ] && ok "home override is not set" || fail "androidplay.home forces ADB: the phone will never arm"
[ -n "$(sh "[ -f $D/androidplay.car ] && echo y")" ] && ok "armed whatever the Wi-Fi (androidplay.car)" || note "androidplay.car is not set: the phone arms only away from your home Wi-Fi"
st=$(sh "cat $D/androidplayd.state")
[ "$st" = adb ] && ok "state adb now (the PC is attached): it arms 10 s after the cable is pulled, and at boot" || note "state now: $st"
sn=$(sh "cat $D/androidplayd.status"); [ -n "$sn" ] && note "last status: $sn"
bat=$(sh "dumpsys battery | grep -m1 level | awk '{print \$2}'")
[ "${bat:-0}" -ge 30 ] 2>/dev/null && ok "battery ${bat}%" || fail "battery ${bat:-?}% is low"
free=$(sh "df -k /data | tail -1 | awk '{print \$4}'")
[ "${free:-0}" -ge 500000 ] 2>/dev/null && ok "free space $((free/1024)) MB" || fail "free space low (${free:-?} KB)"
arch=$(sh "ls -1d $D/archive/* 2>/dev/null | wc -l"); note "evidence archive: ${arch:-0} snapshots on the phone (scripts/pull-evidence.sh copies them to the PC)"
[ -n "$(sh "[ -f $D/androidplay.learn ] && echo y")" ] && note "LEARNING MODE is on: strategy $(sh "cat $D/androidplay.learn.idx") (radio: $(sh "cat $D/androidplay.radio"))" || note "learning mode off; radio profile: '$(sh "cat $D/androidplay.radio")'"
[ -n "$(sh "[ -f $D/androidplay.mic ] && echo y")" ] && note "radio microphone request is ON (untested on the Captiva)" || note "radio microphone request is off"
[ -f "$(dirname "${BASH_SOURCE[0]}")/../.build/phone/projector.dex" ] && cmp -s "$(dirname "${BASH_SOURCE[0]}")/../.build/phone/projector.dex" <(adb shell "cat $D/projector.dex") && ok "phone has the current build" || note "phone build differs from .build (run scripts/build-phone.sh && scripts/deploy-phone.sh)"
echo
[ $bad = 0 ] && echo "GO. Unplug the cable, wait 10 s (the notification must say ARMED), then plug into the radio." || { echo "NO-GO: $bad problem(s) above."; exit 1; }
