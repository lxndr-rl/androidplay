#!/usr/bin/env python3
"""PC side of the S10+ FunctionFS bulk loopback. No libusb/pyusb: raw usbfs ioctls (Linux, 64-bit).
Finds the phone (default 04e8:685d), claims the vendor interface with class 255/sub 0/proto 0
(our FFS function; adb is 255/66/1), sends random blocks to the OUT endpoint, reads them back
from the IN endpoint, compares, prints throughput. Needs rw access to /dev/bus/usb (udev uaccess)."""
import ctypes, fcntl, os, struct, sys, time, glob

VID, PID = 0x04e8, 0x685d
IOCTL_CLAIM, IOCTL_RELEASE, IOCTL_BULK = 0x8004550F, 0x80045510, 0xC0185502

class Bulk(ctypes.Structure):
    _fields_ = [("ep", ctypes.c_uint), ("len", ctypes.c_uint), ("timeout", ctypes.c_uint), ("data", ctypes.c_void_p)]

def rd(p):
    try: return open(p).read().strip()
    except OSError: return ""

def find():
    for d in glob.glob("/sys/bus/usb/devices/*"):
        if rd(d + "/idVendor") == "%04x" % VID and rd(d + "/idProduct") == "%04x" % PID:
            return d, "/dev/bus/usb/%03d/%03d" % (int(rd(d + "/busnum")), int(rd(d + "/devnum")))
    return None, None

def our_interface(sysdir):
    for i in sorted(glob.glob(sysdir + "/*:1.*")):
        if (rd(i + "/bInterfaceClass"), rd(i + "/bInterfaceSubClass"), rd(i + "/bInterfaceProtocol")) == ("ff", "00", "00"):
            eps = {}
            for e in glob.glob(i + "/ep_*"):
                eps[rd(e + "/direction")] = int(rd(e + "/bEndpointAddress"), 16)
            return int(rd(i + "/bInterfaceNumber")), eps
    return None, None

def bulk(fd, ep, buf, timeout_ms):
    b = Bulk(ep, len(buf), timeout_ms, ctypes.cast(buf, ctypes.c_void_p))
    return fcntl.ioctl(fd, IOCTL_BULK, b)

def main():
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 4096
    sysdir, node = find()
    if not node: print("FAIL: no %04x:%04x device" % (VID, PID)); return 1
    num, eps = our_interface(sysdir)
    if num is None: print("FAIL: our vendor interface (ff/00/00) not present"); return 1
    print("device", node, "interface", num, "eps", {k: hex(v) for k, v in eps.items()})
    fd = os.open(node, os.O_RDWR)
    fcntl.ioctl(fd, IOCTL_CLAIM, struct.pack("I", num))
    out_ep, in_ep = eps["out"], eps["in"]
    try:
        bn = ctypes.create_string_buffer(64)
        got = bulk(fd, in_ep, bn, 4000)
        print("banner from device (%d bytes): %r" % (got, bn.raw[:got]))
    except OSError as e:
        print("banner read failed:", e)
    ok = bad = 0; t0 = time.time(); total = 0
    try:
        for r in range(rounds):
            pat = os.urandom(size)
            tx = ctypes.create_string_buffer(pat, size)
            n = bulk(fd, out_ep, tx, 2000)
            rx = ctypes.create_string_buffer(size)
            m = bulk(fd, in_ep, rx, 2000)
            if n == size and m == size and rx.raw == pat: ok += 1
            else:
                bad += 1
                if bad <= 3: print("MISMATCH round", r, "tx", n, "rx", m)
            total += size
    except OSError as e:
        print("ERROR", e, "after ok=%d bad=%d" % (ok, bad))
    dt = time.time() - t0
    print("rounds ok=%d bad=%d size=%d bytes_each_way=%d time=%.2fs throughput=%.2f MB/s each way" % (ok, bad, size, total, dt, total / dt / 1e6 if dt else 0))
    try: fcntl.ioctl(fd, IOCTL_RELEASE, struct.pack("I", num))
    except OSError: pass
    os.close(fd)
    return 0 if bad == 0 and ok == rounds else 2

sys.exit(main())
