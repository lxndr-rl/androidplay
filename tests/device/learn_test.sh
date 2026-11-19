#!/usr/bin/env bash
# Host test of scripts/device/learn.sh (POSIX sh logic: strategy ladder, profile per radio, baseline safety).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export LEARN_DIR=$T
fails=0
check() { if [ "$2" = "$3" ]; then :; else echo "FAIL $1: got '$2' want '$3'"; fails=$((fails+1)); fi; }
sh -c ". $ROOT/scripts/device/learn.sh; learn_value 0" > $T/v0; check baseline "$(cat $T/v0)" 01000000
run() { sh -c ". $ROOT/scripts/device/learn.sh; $1"; }

# normal use: no learning, no reply file -> ffs_probe answers the baseline
run "profile_apply"; check no-reply-file "$([ -e $T/androidplay.reply ] && echo yes || echo no)" no

# learning: starts at the baseline, each failed episode moves to the next answer
run "learn_start"; check learn-flag "$([ -f $T/androidplay.learn ] && echo yes || echo no)" yes
check idx0 "$(cat $T/androidplay.learn.idx)" 0
check reply0 "$([ -e $T/androidplay.reply ] && echo yes || echo no)" no
out=$(run "learn_fail"); check fail1 "$out" "next 1 00000000"; check reply1 "$(cat $T/androidplay.reply)" 00000000
out=$(run "learn_fail"); check fail2 "$out" "next 2 02000000"; check reply2 "$(cat $T/androidplay.reply)" 02000000

# success is saved under the radio's name and the learning flag is cleared; the working answer stays in force
echo captiva > $T/androidplay.radio
out=$(run "learn_success"); check success "$out" "profile captiva 02000000"
check learn-off "$([ -f $T/androidplay.learn ] && echo yes || echo no)" no
check profile "$(cat $T/androidplay.profile)" "captiva 02000000"
check reply-kept "$(cat $T/androidplay.reply)" 02000000

# the next manager start: the named radio gets its profile, an unnamed or unknown radio gets the baseline (Zlink is never touched)
run "profile_apply"; check captiva-profile "$(cat $T/androidplay.reply)" 02000000
echo zlink > $T/androidplay.radio; run "profile_apply"; check zlink-baseline "$([ -e $T/androidplay.reply ] && echo yes || echo no)" no
rm -f $T/androidplay.radio; run "profile_apply"; check unnamed-baseline "$([ -e $T/androidplay.reply ] && echo yes || echo no)" no

# a second success for the same radio replaces the line instead of duplicating it
run "learn_start"; run "learn_fail" >/dev/null; echo captiva > $T/androidplay.radio; run "learn_success" >/dev/null
check no-duplicate "$(grep -c '^captiva ' $T/androidplay.profile)" 1
check updated "$(grep '^captiva ' $T/androidplay.profile)" "captiva 00000000"

# exhausting the ladder ends learning and goes back to the baseline instead of looping forever
run "learn_start"; r=""; for i in $(seq 1 20); do o=$(run "learn_fail"); case "$o" in exhausted) r=exhausted; break;; esac; done
check exhausted "$r" exhausted
check exhausted-flag "$([ -f $T/androidplay.learn ] && echo yes || echo no)" no
check exhausted-reply "$([ -e $T/androidplay.reply ] && echo yes || echo no)" no

# the archive keeps files out of harm's way and honours retention
export ARCHIVE_SRC=$T/src ARCHIVE_DIR=$T/arch; mkdir -p $T/src; echo one > $T/src/ffs_events.log; echo log > $T/src/androidplayd.log
d1=$(KEEP=2 sh $ROOT/scripts/device/archive_logs.sh episode); sleep 1; echo two > $T/src/ffs_events.log
d2=$(KEEP=2 sh $ROOT/scripts/device/archive_logs.sh episode); sleep 1; echo three > $T/src/ffs_events.log
d3=$(KEEP=2 sh $ROOT/scripts/device/archive_logs.sh session)
check snap-content "$(cat $d3/ffs_events.log)" three
check retention "$(ls -1 $T/arch | wc -l | tr -d ' ')" 2
check oldest-dropped "$([ -d $d1 ] && echo yes || echo no)" no
check tail-kept "$([ -f $d3/androidplayd.tail.log ] && echo yes || echo no)" yes
check empty-source "$(ARCHIVE_SRC=$T/none ARCHIVE_DIR=$T/arch2 sh $ROOT/scripts/device/archive_logs.sh boot; ls $T/arch2 2>/dev/null | wc -l | tr -d ' ')" 0

[ $fails = 0 ] && echo "learn_test: all passed" || { echo "learn_test: $fails failed"; exit 1; }
