#!/usr/bin/env python3
"""Plays the head unit's side of the Android Open Accessory start sequence against the phone (raw usbfs, no libusb):
c0 33 (getProtocol), 40 34 x6 (identification strings), 40 35 (start). Same sequence Zlink sends. For bench tests only.
usage: VID=04e8 PID=685d python3 -I pc_aoa.py"""
import fcntl, glob, os, struct, ctypes, sys, time
def find():
    for d in glob.glob('/sys/bus/usb/devices/*'):
        try:
            if open(d+'/idVendor').read().strip()==os.environ.get('VID','04e8') and open(d+'/idProduct').read().strip()==os.environ.get('PID','685d'):
                return int(open(d+'/busnum').read()), int(open(d+'/devnum').read())
        except OSError: pass
dev=find()
if not dev: sys.exit('device not found')
fd=os.open('/dev/bus/usb/%03d/%03d'%dev, os.O_RDWR)
def ctrl(t,r,v,i,data):
    buf=ctypes.create_string_buffer(data, max(len(data),2)) if data else ctypes.create_string_buffer(2)
    req=struct.pack('BBHHHIQ',t,r,v,i,len(data) if (t&0x80)==0 else 2,1000,ctypes.addressof(buf))
    try: fcntl.ioctl(fd,0xC0185500,bytearray(req)); return bytes(buf.raw[:2]) if t&0x80 else b'ok'
    except OSError as e: return str(e).encode()
print('getProtocol ->', ctrl(0xC0,0x33,0,0,b'').hex())
for i,sv in enumerate([b'Android', b'Android Auto', b'Android Auto', b'1.0', b'http://www.android.com', b'0123456789']):
    ctrl(0x40,0x34,0,i,sv+b'\0')
print('start ->', ctrl(0x40,0x35,0,0,b''))
