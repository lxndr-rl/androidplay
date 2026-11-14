// ffs_probe: mount FunctionFS, write descriptors/strings so the gadget can bind,
// then log EP0 events for N seconds. Hardware probe for USB-002; not the final daemon.
// usage: ffs_probe INSTANCE SECONDS   (instance = ffs.<INSTANCE> in configfs)
#include "gadget_manager.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
using namespace s10;

struct FfsEvent { uint8_t setup[8]; uint8_t type; uint8_t pad[3]; };  // usb_functionfs_event, 12 bytes

static const char* ev_name(int t) {
    switch (t) { case 0: return "BIND"; case 1: return "UNBIND"; case 2: return "ENABLE";
                 case 3: return "DISABLE"; case 4: return "SETUP"; case 5: return "SUSPEND";
                 case 6: return "RESUME"; default: return "?"; }
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: ffs_probe INSTANCE SECONDS\n"); return 2; }
    std::string inst = argv[1];
    int secs = atoi(argv[2]);
    std::string mnt = "/dev/ffs-" + inst;
    mkdir(mnt.c_str(), 0770);
    if (mount(inst.c_str(), mnt.c_str(), "functionfs", 0, nullptr) != 0 && errno != EBUSY) {
        perror("mount functionfs"); return 1;
    }
    int ep0 = open((mnt + "/ep0").c_str(), O_RDWR);
    if (ep0 < 0) { perror("open ep0"); return 1; }
    FfsInterface itf;
    itf.eps = {{0x81, 0x02, 64, 512}, {0x02, 0x02, 64, 512}};  // bulk IN, bulk OUT
    auto d = build_ffs_descriptors({itf});
    auto s = build_ffs_strings("s10probe");
    if (write(ep0, d.data(), d.size()) != (ssize_t)d.size()) { perror("write descriptors"); return 1; }
    if (write(ep0, s.data(), s.size()) != (ssize_t)s.size()) { perror("write strings"); return 1; }
    printf("READY descriptors=%zu strings=%zu\n", d.size(), s.size());
    fflush(stdout);
    RoleSwitchHandler h;
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
        if (t.tv_sec - t0.tv_sec >= secs) break;
        struct pollfd p = {ep0, POLLIN, 0};
        if (poll(&p, 1, 500) <= 0) continue;
        FfsEvent ev;
        ssize_t n = read(ep0, &ev, sizeof ev);
        if (n != (ssize_t)sizeof ev) { if (n < 0 && errno != EAGAIN) perror("read ev"); continue; }
        printf("EVENT %s t=%lds", ev_name(ev.type), (long)(t.tv_sec - t0.tv_sec));
        if (ev.type == 4) {
            SetupPacket sp = parse_setup(ev.setup);
            printf(" setup type=%02x req=%02x val=%04x idx=%04x len=%u", sp.type, sp.request, sp.value, sp.index, sp.length);
            Ep0Result r = h.handle(sp);
            // Probe only: answer like the doc (reply/ack), never swap roles here.
            if (r.action == Ep0Action::Reply) { (void)!write(ep0, r.data.data(), r.data.size()); printf(" -> reply"); }
            else if (r.action == Ep0Action::Ack) { (void)!read(ep0, nullptr, 0); printf(" -> ack(role switch NOT performed)"); }
            else { (void)!(sp.type & 0x80 ? read(ep0, nullptr, 0) : write(ep0, nullptr, 0)); printf(" -> stall"); }
        }
        printf("\n"); fflush(stdout);
    }
    printf("DONE\n");
    close(ep0);
    return 0;
}
