#!/system/bin/sh
# Per-radio learning (sourced by androidplayd.sh; also tested on the host with LEARN_DIR=<tmp>).
#
# Why: a radio we have never seen may want a different answer to its `c0 53` query than the one that works for Zlink. Instead of
# one hypothesis per trip, learning mode tries one answer per radio-initiated plug episode (the radio cuts and re-plugs by itself when
# it gives up) and records what the radio did. Normal use never learns: the answer is the baseline unless a profile says otherwise.
#
# Files (all under $LD, default /data/local/tmp), read by ffs_probe / the manager:
#   androidplay.reply       current answer to `c0 53`, 8 hex digits; absent = baseline 01000000. ffs_probe re-reads it on every query.
#   androidplay.learn       exists = learning is on (touch it before parking next to the radio)
#   androidplay.learn.idx   index of the strategy being tried
#   androidplay.radio       name of the radio the user is next to (e.g. captiva); picks the profile
#   androidplay.profile     lines "<radio> <8 hex digits>": the answer that worked for each radio
LD=${LEARN_DIR:-/data/local/tmp}
LEARN_COUNT=12

learn_value() {   # strategy index -> 8 hex digits (the 4 bytes answered to c0 53); empty = out of range
  case "$1" in
    0) echo 01000000;;   # baseline (what the OrangeCar document says; works on Zlink)
    1) echo 00000000;; 2) echo 02000000;; 3) echo 03000000;; 4) echo 01010000;; 5) echo 00000001;;
    6) echo 01000001;; 7) echo ffffffff;; 8) echo 04000000;; 9) echo 08000000;; 10) echo 10000000;; 11) echo 01000100;;
    *) echo "";;
  esac
}
learn_active() { [ -f "$LD/androidplay.learn" ]; }
learn_idx() { i=$(cat "$LD/androidplay.learn.idx" 2>/dev/null); echo "${i:-0}"; }
learn_apply() {   # write (or clear, for the baseline) the answer ffs_probe uses
  v=$(learn_value "$1")
  if [ -z "$v" ] || [ "$1" = 0 ]; then rm -f "$LD/androidplay.reply"; else echo "$v" > "$LD/androidplay.reply"; fi
}
learn_start() { echo 0 > "$LD/androidplay.learn.idx"; touch "$LD/androidplay.learn"; learn_apply 0; }
learn_fail() {    # an episode ended with the radio probing but never asking for the role swap: next strategy
  i=$(( $(learn_idx) + 1 ))
  if [ "$i" -ge "$LEARN_COUNT" ]; then
    rm -f "$LD/androidplay.learn" "$LD/androidplay.learn.idx"; learn_apply 0; echo "exhausted"; return 1
  fi
  echo "$i" > "$LD/androidplay.learn.idx"; learn_apply "$i"; echo "next $i $(learn_value "$i")"
}
learn_success() { # the radio asked for the role swap: remember the answer for this radio and stop learning
  idx=$(learn_idx); v=$(learn_value "$idx"); name=$(cat "$LD/androidplay.radio" 2>/dev/null); [ -n "$name" ] || name=learned
  { grep -v "^$name " "$LD/androidplay.profile" 2>/dev/null; echo "$name $v"; } > "$LD/androidplay.profile.tmp"
  mv "$LD/androidplay.profile.tmp" "$LD/androidplay.profile"
  learn_apply "$idx"   # keep the answer that worked in force until the next manager start, which reads the profile
  rm -f "$LD/androidplay.learn" "$LD/androidplay.learn.idx"
  echo "profile $name $v"
}
profile_apply() { # at manager start: use the profile of the named radio, else the baseline
  [ -f "$LD/androidplay.learn" ] && return 0
  name=$(cat "$LD/androidplay.radio" 2>/dev/null); v=""
  [ -n "$name" ] && v=$(grep "^$name " "$LD/androidplay.profile" 2>/dev/null | tail -n 1 | cut -d' ' -f2)
  if [ -n "$v" ] && [ "$v" != 01000000 ]; then echo "$v" > "$LD/androidplay.reply"; else rm -f "$LD/androidplay.reply"; fi
}
