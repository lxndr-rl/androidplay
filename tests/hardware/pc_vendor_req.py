#!/usr/bin/env python3
"""Send a vendor control request (default C0 53 ... len 4) to the phone presenting 05ac:12a8 and print the reply.
Raw usbfs ioctl, no libusb. Tests whether pre-configuration/vendor setups reach the phone's FunctionFS ep0.
usage: python3 -I pc_vendor_req.py [bmRequestType bRequest wValue wIndex wLength]  (numbers in hex)"""
import fcntl, glob, os, struct, sys, ctypes, time
a = [int(x, 16) for x in sys.argv[1:6]] if len(sys.argv) >= 6 else [0xC0, 0x53, 0, 0, 4]
def find():
    for d in glob.glob('/sys/bus/usb/devices/*'):
        try:
            if open(d + '/idVendor').read().strip() == os.environ.get('VID', '05ac') and open(d + '/idProduct').read().strip() == os.environ.get('PID', '12a8'):
                return (int(open(d + '/busnum').read()), int(open(d + '/devnum').read()))
        except OSError:
            pass
for _ in range(60):
    dev = find()
    if dev: break
    time.sleep(1)
else:
    sys.exit('no 05ac:12a8 device')
fd = os.open('/dev/bus/usb/%03d/%03d' % dev, os.O_RDWR)
buf = ctypes.create_string_buffer(max(a[4], 1))
# struct usbdevfs_ctrltransfer {u8 t,r; u16 v,i,l; u32 timeout; void* data}
req = struct.pack('BBHHHIQ', a[0], a[1], a[2], a[3], a[4], 1000, ctypes.addressof(buf))
IOCTL_CONTROL = 0xC0185500
try:
    n = fcntl.ioctl(fd, IOCTL_CONTROL, bytearray(req))
    print('OK reply', buf.raw[:a[4]].hex(), 'n=%s' % n)
except OSError as e:
    print('ERR', e)
