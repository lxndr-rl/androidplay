// ffs_probe: mount FunctionFS, write descriptors/strings so the gadget can bind,
// then log EP0 events for N seconds. Hardware probe for USB-002; not the final daemon.
// usage: ffs_probe INSTANCE SECONDS [echo]   (instance = ffs.<INSTANCE> in configfs)
// modes (3rd arg): echo = loopback on bulk endpoints; noeps = interface with NO endpoints (ep0 only);
// clone = interface class ff/42/01 with two bulk endpoints, i.e. byte-for-byte what adb shows (use with adb removed).
// echo: bytes read from the OUT endpoint (ep2) are written back on the IN endpoint (ep1).
#include "gadget_manager.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <atomic>
#include <thread>
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
    bool noeps = argc > 3 && std::string(argv[3]) == "noeps";
    if (argc > 3 && std::string(argv[3]) == "clone") { itf.cls = 0xff; itf.sub = 0x42; itf.proto = 0x01; }
    if (!noeps) itf.eps = {{0x81, 0x02, 64, 512}, {0x02, 0x02, 64, 512}};  // bulk IN, bulk OUT
    auto d = build_ffs_descriptors({itf});
    auto s = build_ffs_strings("s10probe");
    if (write(ep0, d.data(), d.size()) != (ssize_t)d.size()) { perror("write descriptors"); return 1; }
    if (write(ep0, s.data(), s.size()) != (ssize_t)s.size()) { perror("write strings"); return 1; }
    bool echo = argc > 3 && std::string(argv[3]) == "echo";
    int epin = -1, epout = -1;  // FFS names endpoints in descriptor order: ep1 = 0x81 IN, ep2 = 0x02 OUT
    std::atomic<unsigned long long> rx{0}, tx{0}, chunks{0};
    std::atomic<bool> stop{false};
    // FunctionFS endpoint files do not support poll: OUT data only arrives while a blocking
    // read is pending, so the echo runs in its own thread.
    std::thread echo_thread;
    // Started on the first ENABLE: a read pending across the composition UNBIND/BIND never
    // sees data on this kernel (measured), so the thread must start with endpoints active.
    auto start_echo = [&] { if (echo && !echo_thread.joinable()) echo_thread = std::thread([&] {
        static unsigned char buf[16384];
        epin = open((mnt + "/ep1").c_str(), O_WRONLY);
        epout = open((mnt + "/ep2").c_str(), O_RDONLY);
        if (epin < 0 || epout < 0) { perror("open ep1/ep2"); return; }
        int last_err = 0;
        ssize_t bw = write(epin, "S10HELLO", 8);  // banner: proves device->host before testing host->device
        printf("ECHO banner write=%zd errno=%d\n", bw, bw < 0 ? errno : 0); fflush(stdout);
        while (!stop) {
            ssize_t n = read(epout, buf, sizeof buf);
            if (n < 0) {
                // unbind/rebind and not-yet-enabled endpoints make reads fail; keep trying
                if (errno != last_err) { printf("ECHO read errno=%d (%s)\n", errno, strerror(errno)); fflush(stdout); last_err = errno; }
                usleep(5000); continue;
            }
            last_err = 0;
            if (n == 0) continue;
            ssize_t w = write(epin, buf, n);
            rx += n; if (w > 0) tx += w; chunks++;
            if (w != n) { printf("ECHO short write %zd/%zd errno=%d\n", w, n, errno); fflush(stdout); }
        }
    }); };
    printf("READY descriptors=%zu strings=%zu echo=%d\n", d.size(), s.size(), echo);
    fflush(stdout);
    RoleSwitchHandler h;
    const bool sweep = getenv("S10_REPLY_SWEEP") && *getenv("S10_REPLY_SWEEP") == '1';
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
        if (t.tv_sec - t0.tv_sec >= secs) break;
        struct pollfd p[1] = {{ep0, POLLIN, 0}};
        if (poll(p, 1, 500) <= 0) continue;
        if (!(p[0].revents & POLLIN)) continue;
        FfsEvent ev;
        ssize_t n = read(ep0, &ev, sizeof ev);
        if (n != (ssize_t)sizeof ev) { if (n < 0 && errno != EAGAIN) perror("read ev"); continue; }
        printf("EVENT %s t=%lds", ev_name(ev.type), (long)(t.tv_sec - t0.tv_sec));
        if (ev.type == 2) start_echo();
        if (ev.type == 4) {
            SetupPacket sp = parse_setup(ev.setup);
            printf(" setup type=%02x req=%02x val=%04x idx=%04x len=%u", sp.type, sp.request, sp.value, sp.index, sp.length);
            Ep0Result r = h.handle(sp);
            // Learning / per-radio profile: the manager writes the answer to `c0 53` (8 hex digits) into S10_REPLY_FILE; absent or
            // malformed = the baseline from the handler. Re-read on every query so the manager never has to restart this process.
            if (sp.type == 0xC0 && sp.request == 0x53 && r.action == Ep0Action::Reply) {
                const char* rf = getenv("S10_REPLY_FILE"); if (!rf || !*rf) rf = "/data/local/tmp/androidplay.reply";
                if (FILE* f = fopen(rf, "r")) {
                    char hex[32] = {0}; if (fgets(hex, sizeof hex, f)) {
                        unsigned b[4]; if (sscanf(hex, "%2x%2x%2x%2x", &b[0], &b[1], &b[2], &b[3]) == 4) {
                            r.data.assign(4, 0); for (int k = 0; k < 4; k++) r.data[k] = static_cast<uint8_t>(b[k]);
                            printf(" [answer from %s: %02x %02x %02x %02x]", rf, b[0], b[1], b[2], b[3]);
                        }
                    }
                    fclose(f);
                }
            }
            // Experiment (S10_REPLY_SWEEP=1): a radio that sends C0 53 but never follows with 40 51 may want a different answer.
            // Every other C0 53 gets the known-good 01 00 00 00 (so Zlink still works); the odd ones walk a list of variants.
            if (sweep && sp.type == 0xC0 && sp.request == 0x53 && r.action == Ep0Action::Reply) {
                static const uint8_t V[][4] = {{0,0,0,0},{2,0,0,0},{3,0,0,0},{1,1,0,0},{0,0,0,1},{1,0,0,1},{0xff,0xff,0xff,0xff},{4,0,0,0},{8,0,0,0},{0x10,0,0,0},{1,2,3,4},{1,0,1,0}};
                static unsigned n = 0, k = 0;
                if (n++ % 2 == 1) { unsigned i = k++ % (sizeof V / sizeof V[0]); r.data.assign(V[i], V[i] + 4); printf(" [sweep variant %u = %02x %02x %02x %02x]", i, V[i][0], V[i][1], V[i][2], V[i][3]); }
                else printf(" [baseline 01 00 00 00]");
            }
            // Probe only: answer like the doc (reply/ack), never swap roles here.
            if (r.action == Ep0Action::Reply) { (void)!write(ep0, r.data.data(), r.data.size()); printf(" -> reply"); }
            else if (r.action == Ep0Action::Ack) {
                (void)!read(ep0, nullptr, 0);
                // S10_SWAP_HOOK: optional external script run in the background after the ack (hardware experiment).
                const char* hook = getenv("S10_SWAP_HOOK");
                if (r.role_switch_requested && hook && *hook) { std::string c = std::string(hook) + " &"; (void)!system(c.c_str()); printf(" -> ack, swap hook started"); }
                else printf(" -> ack(role switch NOT performed)");
            }
            else { (void)!(sp.type & 0x80 ? read(ep0, nullptr, 0) : write(ep0, nullptr, 0)); printf(" -> stall"); }
        }
        printf("\n"); fflush(stdout);
    }
    printf("ECHO totals rx=%llu tx=%llu chunks=%llu\n", (unsigned long long)rx, (unsigned long long)tx, (unsigned long long)chunks);
    printf("DONE\n"); fflush(stdout);
    _exit(0);  // the echo thread may be blocked in read(); do not wait for it
    return 0;
}
