#!/usr/bin/env bash
# Pulls everything the phone knows about the last trips into a new folder next to the repo, BEFORE anything else touches the phone.
# usage: scripts/pull-evidence.sh [label]      folder: ../s10caros-evidence-archive/diagnostics-<label>-<date>/
# Safe to run any time: it only reads. Do not reboot or restart the manager before running it.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
adb get-state >/dev/null 2>&1 || { echo "no phone on adb (plug it into the PC; do not reboot it)"; exit 1; }
adb root >/dev/null 2>&1 || true; sleep 1
OUT="$ROOT/../s10caros-evidence-archive/diagnostics-${1:-trip}-$(date +%Y%m%d-%H%M)"
mkdir -p "$OUT"
D=/data/local/tmp
# 1. snapshot first, so the live logs are also inside the phone's archive
adb shell "[ -x $D/archive_logs.sh ] && $D/archive_logs.sh manual" >/dev/null 2>&1 || true
# 2. the live logs
for f in androidplayd.log androidplayd.ctrl.log androidplayd.status ffs_events.log airplay.log swap.log iap.log projector.log inject.log mdns.log proj.log session_raw.bin info_reply.bin androidplay.reply androidplay.profile androidplay.radio; do
  adb pull "$D/$f" "$OUT/" >/dev/null 2>&1 || true
done
# 3. the phone's own archive (every episode and session since the archive existed)
adb pull "$D/archive" "$OUT/phone-archive" >/dev/null 2>&1 || true
echo "saved to $OUT"; ls -1 "$OUT" | head -40
