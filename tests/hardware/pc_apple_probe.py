#!/usr/bin/env python3
"""Bench stand-in for a head unit that asks Apple first: waits for the phone's Apple ids (05ac:12a8), then sends `c0 53` (vendor,
device-to-host, wLength 4) COUNT times INTERVAL seconds apart and prints each 4-byte answer. Raw usbfs, no libusb. Bench only.
usage: python3 -I pc_apple_probe.py [COUNT=5] [INTERVAL=0.3] [WAIT=90]      exit 0 if it probed, 3 if the device never appeared"""
import fcntl, glob, os, struct, ctypes, sys, time
count = int(sys.argv[1]) if len(sys.argv) > 1 else 5
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 0.3
wait = float(sys.argv[3]) if len(sys.argv) > 3 else 90
def find():
    for d in glob.glob('/sys/bus/usb/devices/*'):
        try:
            if open(d + '/idVendor').read().strip() == '05ac' and open(d + '/idProduct').read().strip() == '12a8':
                return int(open(d + '/busnum').read()), int(open(d + '/devnum').read())
        except OSError:
            pass
dev = None; t0 = time.time()
while time.time() - t0 < wait:
    dev = find()
    if dev: break
    time.sleep(0.05)
if not dev: print('apple device never appeared'); sys.exit(3)
fd = os.open('/dev/bus/usb/%03d/%03d' % dev, os.O_RDWR)
def vendor_in(req, length):
    buf = ctypes.create_string_buffer(length)
    r = struct.pack('BBHHHIQ', 0xC0, req, 0, 0, length, 1000, ctypes.addressof(buf))
    try:
        n = fcntl.ioctl(fd, 0xC0185500, bytearray(r)); return bytes(buf.raw[:max(n, 0)]).hex() if isinstance(n, int) else bytes(buf.raw[:length]).hex()
    except OSError as e:
        return 'ERR:%s' % e.errno
print('appeared after %.1fs' % (time.time() - t0), flush=True)
for i in range(count):
    print('probe %d reply=%s' % (i, vendor_in(0x53, 4)), flush=True)
    time.sleep(interval)
