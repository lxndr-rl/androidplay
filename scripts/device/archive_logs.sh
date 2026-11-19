#!/system/bin/sh
# Evidence archive. Copies the per-session logs into $D/archive/<stamp>-<tag>/ so that a reboot, a manager restart or the next
# session cannot overwrite them (the Captiva logs of 2026-11-17 were lost that way). Called by the manager at start and at the end of
# every episode, and by the session hook before it cleans its own logs.
#   archive_logs.sh [tag]        tag: boot | episode | session | manual (default manual)
# Retention: the newest KEEP snapshots and at most MAXKB kilobytes in total.
D=${ARCHIVE_SRC:-/data/local/tmp}
A=${ARCHIVE_DIR:-$D/archive}
TAG=${1:-manual}
KEEP=${KEEP:-60}
MAXKB=${MAXKB:-300000}
mkdir -p "$A" || exit 1
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null)
[ -n "$STAMP" ] || STAMP=up$(cut -d. -f1 /proc/uptime)
DIR="$A/$STAMP-$TAG"
n=0
for f in androidplayd.ctrl.log ffs_events.log airplay.log swap.log iap.log projector.log inject.log mdns.log proj.log session_raw.bin info_reply.bin androidplay.reply androidplay.learn.idx androidplay.profile; do
  [ -s "$D/$f" ] || continue
  mkdir -p "$DIR"; cp "$D/$f" "$DIR/$f" 2>/dev/null && n=$((n+1))
done
[ $n -gt 0 ] || exit 0
# the manager log is append-only and survives reboots: keep its tail with the snapshot
tail -n 400 "$D/androidplayd.log" > "$DIR/androidplayd.tail.log" 2>/dev/null
# retention by count, then by size (oldest first; the stamp sorts chronologically)
ls -1d "$A"/* 2>/dev/null | sort | awk -v k="$KEEP" '{a[NR]=$0} END {for (i = 1; i <= NR - k; i++) print a[i]}' | while read -r old; do rm -rf "$old"; done
while [ "$(du -sk "$A" 2>/dev/null | cut -f1)" -gt "$MAXKB" ] 2>/dev/null; do
  oldest=$(ls -1d "$A"/* 2>/dev/null | sort | head -n 1)
  [ -n "$oldest" ] && [ "$oldest" != "$DIR" ] || break
  rm -rf "$oldest"
done
echo "$DIR"
