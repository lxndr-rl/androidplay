// usb_host_iap2: phone-as-USB-host tool for the radio's "iAP Interface" (class ff/f0/00, bulk IN 0x81 / OUT 0x01).
// Runs the phone-side iAP2 Driver (CP-005, host-simulated so far) over the real bulk endpoints through usbfs and logs
// everything. Hardware probe, not the final daemon. usage: usb_host_iap2 BUSNUM DEVNUM [SECONDS] [IFACE] [EP_IN] [EP_OUT]
// STATUS: first contact with a real accessory; nothing here is verified against the radio until a log says so.
#include "iap2_driver.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
using namespace s10::iap2;

static double now() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void hex(const char* tag, const uint8_t* d, size_t n) {
    printf("%.3f %s len=%zu", now(), tag, n);
    for (size_t i = 0; i < n && i < 96; i++) printf(" %02x", d[i]);
    if (n > 96) printf(" ...");
    printf("\n"); fflush(stdout);
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: usb_host_iap2 BUS DEV [SECONDS] [IFACE] [EP_IN] [EP_OUT]\n"); return 2; }
    int bus = atoi(argv[1]), dev = atoi(argv[2]);
    int secs = argc > 3 ? atoi(argv[3]) : 60;
    unsigned iface = argc > 4 ? strtoul(argv[4], 0, 0) : 0;
    unsigned ep_in = argc > 5 ? strtoul(argv[5], 0, 0) : 0x81, ep_out = argc > 6 ? strtoul(argv[6], 0, 0) : 0x01;
    char path[64]; snprintf(path, sizeof path, "/dev/bus/usb/%03d/%03d", bus, dev);
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror(path); return 1; }
    if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &iface) != 0) { perror("claim interface"); return 1; }
    printf("%.3f claimed %s iface %u in=0x%02x out=0x%02x\n", now(), path, iface, ep_in, ep_out); fflush(stdout);

    DriverConfig cfg;
    cfg.unixTime = [] { return (uint64_t)time(nullptr); };
    cfg.random = [](uint8_t* o, size_t n) { FILE* f = fopen("/dev/urandom", "rb"); if (f) { size_t r = fread(o, 1, n, f); (void)r; fclose(f); } };
    DriverCallbacks cb;
    cb.onCertificate = [](const std::vector<uint8_t>& c) { printf("%.3f CERT len=%zu\n", now(), c.size()); fflush(stdout); };
    cb.onAuthResponse = [](const std::vector<uint8_t>&, const std::vector<uint8_t>& r) { printf("%.3f AUTH_RESPONSE len=%zu\n", now(), r.size()); fflush(stdout); };
    cb.onUnhandled = [](const Message& m) { printf("%.3f UNHANDLED msg id=0x%04x params=%zu\n", now(), m.id, m.params.size()); fflush(stdout); };
    Driver drv(cfg, cb);

    auto bulk = [&](unsigned ep, void* buf, unsigned len, unsigned to) {
        struct usbdevfs_bulktransfer b; b.ep = ep; b.len = len; b.timeout = to; b.data = buf;
        return ioctl(fd, USBDEVFS_BULK, &b);
    };
    auto send = [&](const std::vector<std::vector<uint8_t>>& pk) {
        for (auto p : pk) {
            hex("TX", p.data(), p.size());
            int w = bulk(ep_out, p.data(), p.size(), 1000);
            if (w < 0) { printf("%.3f TX error errno=%d (%s)\n", now(), errno, strerror(errno)); fflush(stdout); }
        }
    };
    static uint8_t buf[16384];
    Driver::Phase last = drv.phase(); double t0 = now(), lastRetx = t0; int errs = 0;
    printf("%.3f phase=%d (waiting for the accessory's SYN)\n", now(), (int)last); fflush(stdout);
    while (now() - t0 < secs) {
        int n = bulk(ep_in, buf, sizeof buf, 200);
        if (n > 0) { hex("RX", buf, n); send(drv.onBytes(buf, n)); errs = 0; }
        else if (n < 0 && errno != ETIMEDOUT) {
            if (errs++ < 5) { printf("%.3f RX error errno=%d (%s)\n", now(), errno, strerror(errno)); fflush(stdout); }
            if (errno == ENODEV || errno == EPROTO || errno == ESHUTDOWN) break;
            usleep(100000);
        }
        if (drv.session().hasPending() && now() - lastRetx > 0.5) { send(drv.retransmit()); lastRetx = now(); }
        if (drv.phase() != last) { last = drv.phase(); printf("%.3f phase=%d session_state=%d\n", now(), (int)last, (int)drv.session().state()); fflush(stdout); }
    }
    printf("%.3f DONE phase=%d session_state=%d\n", now(), (int)drv.phase(), (int)drv.session().state());
    return 0;
}
