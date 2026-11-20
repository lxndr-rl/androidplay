#!/usr/bin/env bash
# Bench scenario matrix: the PC plays the head units we have actually met, against the phone's real manager. Run BEFORE any trip and
# before every change to androidplayd.sh / ffs_probe. The phone stays plugged into the PC; it is rebooted for each scenario so
# the cold-start path (the one that failed on the Captiva) is the one under test. Takes about 4 minutes.
#   tests/hardware/bench_matrix.sh [--no-deploy]
# Caveat: this PC runs usbmuxd (udev 39-usbmuxd.rules), which resets a device wearing Apple ids right after it appears; S2/S3 therefore judge the first
# answers only. A real radio does not do that.
# Needs: phone on adb with root, the build in .build/phone (scripts/build-phone.sh), the manager autostart installed.
# Result: one PASS/FAIL line per scenario and a non-zero exit if any failed. It touches only /data/local/tmp flag files and restores them.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
D=/data/local/tmp
fails=0; pass() { echo "PASS  $1"; }; fail() { echo "FAIL  $1  ($2)"; fails=$((fails+1)); }
adbq() { adb shell "$1" 2>/dev/null | tr -d '\r'; }
wait_adb() { for _ in $(seq 1 90); do adb get-state >/dev/null 2>&1 && [ "$(adbq 'getprop sys.boot_completed')" = 1 ] && return 0; sleep 2; done; return 1; }
boot_log() { adbq "awk '/start: ids=/{n=NR} {l[NR]=\$0} END{for(i=n;i<=NR;i++)print l[i]}' $D/androidplayd.log"; }   # lines of the latest manager start

[ "${1:-}" = --no-deploy ] || { bash "$ROOT/scripts/deploy-phone.sh" >/dev/null 2>&1 || { echo "deploy failed"; exit 2; }; }
adb root >/dev/null 2>&1; sleep 1
[ -n "$(adbq "[ -f $D/androidplay.car ] && echo y")" ] || { echo "androidplay.car must be set for the matrix (armed whatever the Wi-Fi)"; exit 2; }
adbq "rm -f $D/androidplay.reply $D/androidplay.learn $D/androidplay.learn.idx" >/dev/null

# --- S1: cold boot with the cable already in (the Captiva failure): armed at start, a PC is recognised, adb comes back by itself
adb reboot; sleep 10; wait_adb || { fail "S1 boot" "adb never came back"; exit 1; }
for _ in $(seq 1 60); do boot_log | grep -q "state ADB" && boot_log | grep -q "armed at start" && break; sleep 3; adb get-state >/dev/null 2>&1 || sleep 3; done
L=$(boot_log)
echo "$L" | grep -q "state CARPLAY (armed at start" && pass "S1 armed at start with the cable in" || fail "S1 armed at start" "no 'armed at start' in the boot log"
echo "$L" | grep -q "PC pattern" && pass "S1 a PC is recognised and the phone returns to ADB" || fail "S1 PC recognised" "no PC pattern line"
[ "$(adbq "cat $D/androidplayd.state")" = adb ] && pass "S1 adb is back with no replug" || fail "S1 adb back" "state=$(adbq "cat $D/androidplayd.state")"

# --- S2: a host that asks Apple first, every 0.3 s like Zlink: baseline answer every time, the phone stays armed while it asks
adb reboot; sleep 8
OUT=$(python3 -I "$ROOT/tests/hardware/pc_apple_probe.py" 8 0.3 100 2>&1); echo "$OUT" | sed 's/^/      /'
wait_adb || { fail "S2 boot" "adb never came back"; exit 1; }
# usbmuxd on the PC resets an iPhone-id device right after the first probe, so judge only the answers actually read: at least one, all baseline
n=$(echo "$OUT" | grep -c 'reply=01000000'); o=$(echo "$OUT" | grep 'reply=' | grep -vc -E 'reply=(01000000|ERR:)')
[ "$n" -ge 1 ] && [ "$o" = 0 ] && pass "S2 baseline answer to c0 53 ($n read, none different)" || fail "S2 baseline answer" "$n baseline answers, $o other values"
for _ in $(seq 1 40); do boot_log | grep -q "state ADB" && break; sleep 3; done
boot_log | grep -q "saw Apple probe" && pass "S2 manager saw the Apple probes" || fail "S2 probes seen" "no 'saw Apple probe'"
[ "$(adbq "cat $D/androidplayd.state")" = adb ] && pass "S2 returns to ADB after the host goes quiet" || fail "S2 returns to ADB" "state=$(adbq "cat $D/androidplayd.state")"

# --- S3: a per-radio answer in force: the same probes now get that answer (proves ffs_probe reads the reply file)
adbq "echo testradio > $D/androidplay.radio; echo 'testradio 02000000' > $D/androidplay.profile" >/dev/null
adb reboot; sleep 8
OUT=$(python3 -I "$ROOT/tests/hardware/pc_apple_probe.py" 4 0.3 100 2>&1); echo "$OUT" | sed 's/^/      /'
wait_adb || { fail "S3 boot" "adb never came back"; exit 1; }
n=$(echo "$OUT" | grep -c 'reply=02000000'); o=$(echo "$OUT" | grep 'reply=' | grep -vc -E 'reply=(02000000|ERR:)')
[ "$n" -ge 1 ] && [ "$o" = 0 ] && pass "S3 per-radio answer is served ($n read, none different)" || fail "S3 per-radio answer" "$n answers of 02000000, $o other values"
adbq "rm -f $D/androidplay.reply $D/androidplay.radio $D/androidplay.profile" >/dev/null

adb root >/dev/null 2>&1; sleep 2
# --- S4: the evidence is kept: the archive has snapshots from these runs, and the logs of an earlier boot survived the restarts above
arch=$(adbq "ls -1d $D/archive/*-boot 2>/dev/null | wc -l"); [ "${arch:-0}" -ge 3 ] && pass "S4 evidence archived across reboots ($arch boot snapshots)" || fail "S4 evidence archive" "only ${arch:-0} boot snapshots"
adbq "[ -x $D/archive_logs.sh ] && ls $D/archive | tail -1" | grep -q . && pass "S4 archive readable" || fail "S4 archive readable" "empty"

echo; [ $fails = 0 ] && echo "bench matrix: all passed" || { echo "bench matrix: $fails FAILED"; exit 1; }
