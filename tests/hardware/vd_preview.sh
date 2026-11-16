#!/usr/bin/env bash
# Bring-up helper (phone on adb, rooted): run the projection pipeline locally and record what the radio would get.
# usage: vd_preview.sh OUT_H264 SECONDS [TAP "x y" ...]   e.g. vd_preview.sh /tmp/a.h264 20 "320 300"
set -u
S=${S10_SERIAL:-}; OUT=$1; SECS=$2; shift 2
ADB="adb ${S:+-s $S}"
$ADB shell 'svc power stayon true; input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard; am force-stop com.s10caros.projection; am force-stop com.s10caros.launcher; for p in $(ps -A | grep app_process | awk "{print \$2}"); do kill $p; done; rm -f /data/local/tmp/touch.fifo; mkfifo /data/local/tmp/touch.fifo; chmod 666 /data/local/tmp/touch.fifo; logcat -c'
$ADB reverse tcp:47500 tcp:47500 >/dev/null
SINK=$(mktemp -d)/sink.py
cat > $SINK <<'PY'
import socket,struct,sys,time
s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(('127.0.0.1',47500)); s.listen(1); s.settimeout(60)
c,_=s.accept(); c.settimeout(8)
out=open(sys.argv[1],'wb'); n=0; b=b''; t0=time.time(); dur=float(sys.argv[2])
try:
    while time.time()-t0<dur:
        d=c.recv(65536)
        if not d: break
        b+=d
        while len(b)>=4:
            l=struct.unpack('>I',b[:4])[0]
            if len(b)<4+l: break
            out.write(b[4:4+l]); b=b[4+l:]; n+=1
except Exception as e: print('err',e)
print('AUs',n)
PY
python3 -I $SINK $OUT $SECS > $OUT.log 2>&1 &
sleep 1
$ADB shell "am start-foreground-service -n com.s10caros.projection/.ProjectionService --ei w 1280 --ei h 720 --ei fps 30 --ei kbps 4000 --ei seconds $((SECS+5)) --es sink 127.0.0.1:47500" >/dev/null 2>&1
sleep 3
D=$($ADB logcat -d -s S10CarOS-PROJ | grep -o "display=[0-9]*" | tail -1 | cut -d= -f2)
echo "display=$D"
$ADB shell "am start --display $D -n com.s10caros.launcher/.app.LauncherActivity" >/dev/null 2>&1
$ADB shell "nohup sh -c 'CLASSPATH=/data/local/tmp/inject.dex app_process / com.s10caros.input.shell.InjectMain --display $D --size 1280 720 --listen 47600 < /data/local/tmp/touch.fifo > /data/local/tmp/inject.log 2>&1' > /dev/null 2>&1 &"
sleep 4
for t in "$@"; do
  $ADB shell "exec 3<>/data/local/tmp/touch.fifo; echo 'D $t' >&3; echo 'S 90' >&3; echo 'U $t' >&3"
  sleep 3
done
$ADB shell "dumpsys activity activities | grep -E 'Display #|topResumedActivity' | head -8"
wait
$ADB reverse --remove-all >/dev/null 2>&1
cat $OUT.log
