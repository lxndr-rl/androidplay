#!/system/bin/sh
# OPT-IN experiment (user decision 2026-11-14): present the phone to the user's OWN head unit with
# Apple's USB identity (05ac:12a8, "Apple Inc."/"iPhone") to see whether the radio starts its CarPlay path.
# Observation only: no iAP2/usbmux is served. NOT for distribution; never part of a ROM image or release.
# Usage (root, on the phone):  APPLE_ID_OK=own_radio_only radio_apple_id_probe.sh start [seconds]
#                              radio_apple_id_probe.sh restore
G=/config/usb_gadget/g1
O=/data/local/tmp/probe_orig_ids   # outside radiolog/: radio_observe.sh wipes that dir
UDC=$(cat $G/UDC)
setids() { # vid pid manufacturer product
  echo "" > $G/UDC; sleep 1
  echo $1 > $G/idVendor; echo $2 > $G/idProduct
  echo "$3" > $G/strings/0x409/manufacturer; echo "$4" > $G/strings/0x409/product
  echo $UDC > $G/UDC
}
restore() {
  if [ -f $O ]; then . $O; else VID=0x04e8; PID=0x685d; MAN=samsung; PROD=SM-G975F; fi  # fallback: this phone's stock ids
  UDC=${UDC_SAVED:-$UDC}
  setids $VID $PID "$MAN" "$PROD"
  echo "restored $(cat $G/idVendor):$(cat $G/idProduct) $(date)" >> /data/local/tmp/radiolog/probe.log
}
case "$1" in
start)
  [ "$APPLE_ID_OK" = own_radio_only ] || { echo "refused: set APPLE_ID_OK=own_radio_only"; exit 1; }
  DUR=${2:-900}
  mkdir -p /data/local/tmp/radiolog
  printf 'VID=%s\nPID=%s\nMAN="%s"\nPROD="%s"\nUDC_SAVED=%s\n' "$(cat $G/idVendor)" "$(cat $G/idProduct)" \
    "$(cat $G/strings/0x409/manufacturer)" "$(cat $G/strings/0x409/product)" "$UDC" > $O
  /data/local/tmp/radio_observe.sh $((DUR+60)) >/dev/null 2>&1 &
  sleep 3
  setids 0x05ac 0x12a8 "Apple Inc." "iPhone"
  echo "apple ids set $(date) dur=$DUR" >> /data/local/tmp/radiolog/probe.log
  sleep $DUR
  restore
  ;;
restore) restore ;;
*) echo "usage: start [sec] | restore" ;;
esac
