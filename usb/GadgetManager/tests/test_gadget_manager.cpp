#include "gadget_manager.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
using namespace s10;

static int fails = 0, checks = 0;
static void sh(const std::string& c) { if (system(c.c_str())) {} }
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::string mktmp() {
    char t[] = "/tmp/gmtestXXXXXX";
    return mkdtemp(t);
}
static void touch(const std::string& p, const std::string& v = "") { std::ofstream(p) << v; }
static void mk(const std::string& p) { sh("mkdir -p '" + p + "'"); }

static void test_dry_run_touches_nothing() {
    std::string r = mktmp();
    Fs fs; fs.root = r; fs.dry_run = true;
    GadgetManager m(fs, {});
    CHECK(m.setup());
    CHECK(m.bind());
    CHECK(m.teardown());
    CHECK(!fs.exists("/config/usb_gadget"));
    CHECK(fs.log.size() > 15);
    CHECK(fs.log[0] == "mkdir /config/usb_gadget/s10carplay");
    sh("rm -rf " + r);
}

static void test_setup_bind_teardown_fake_tree() {
    std::string r = mktmp();
    mk(r + "/config/usb_gadget"); mk(r + "/sys/class/udc/10c00000.dwc3");
    Fs fs; fs.root = r; fs.create_attrs = true;
    GadgetConfig c;
    GadgetManager m(fs, c);
    CHECK(m.setup());
    std::string g = "/config/usb_gadget/s10carplay";
    CHECK(fs.read(g + "/idVendor") == "0x05ac");
    CHECK(fs.read(g + "/idProduct") == "0x12a8");
    CHECK(fs.read(g + "/strings/0x409/product") == "iPhone");
    CHECK(fs.read(g + "/configs/b.1/MaxPower") == "500");
    CHECK(fs.exists(g + "/functions/ffs.carplay"));
    CHECK(fs.exists(g + "/configs/b.1/ffs.carplay"));
    CHECK(m.pick_udc() == "10c00000.dwc3");
    CHECK(m.bind());
    CHECK(fs.read(g + "/UDC") == "10c00000.dwc3");
    CHECK(m.status().find("UDC=10c00000.dwc3") != std::string::npos);
    CHECK(m.setup());  // idempotent
    // teardown: attribute files do not vanish with rmdir in a plain fs, so
    // emulate configfs by deleting them first (real configfs auto-removes).
    sh("cd " + r + g + " && rm -f idVendor idProduct bcdUSB bcdDevice UDC");
    sh("cd " + r + g + "/strings/0x409 && rm -f manufacturer product serialnumber");
    sh("cd " + r + g + "/configs/b.1 && rm -f MaxPower strings/0x409/configuration");
    CHECK(m.teardown());
    CHECK(!fs.exists(g));
    sh("rm -rf " + r);
}

static void test_takeover_unbinds_g1_first() {
    std::string r = mktmp();
    mk(r + "/config/usb_gadget/g1"); mk(r + "/sys/class/udc/10c00000.dwc3");
    touch(r + "/config/usb_gadget/g1/UDC", "10c00000.dwc3\n");
    Fs fs; fs.root = r; fs.create_attrs = true;
    GadgetConfig c; c.takeover = "g1";
    GadgetManager m(fs, c);
    CHECK(m.setup());
    fs.log.clear();
    CHECK(m.bind());
    CHECK(fs.log.size() == 2);
    CHECK(fs.log[0].find("g1/UDC") != std::string::npos);
    CHECK(fs.log[1].find("s10carplay/UDC") != std::string::npos);
    CHECK(fs.read("/config/usb_gadget/g1/UDC") == "");
    sh("rm -rf " + r);
}

static void test_bind_without_udc_fails() {
    std::string r = mktmp();
    mk(r + "/config/usb_gadget"); mk(r + "/sys/class/udc");
    Fs fs; fs.root = r; fs.create_attrs = true;
    GadgetManager m(fs, {});
    CHECK(m.setup());
    CHECK(!m.bind());
    sh("rm -rf " + r);
}

static void test_ffs_blobs() {
    FfsInterface i; i.eps = {{0x81, 2, 64, 512}, {0x02, 2, 64, 512}};
    auto d = build_ffs_descriptors({i});
    CHECK(d[0] == 3 && d[1] == 0 && d[2] == 0 && d[3] == 0);   // magic v2
    uint32_t len = d[4] | d[5] << 8 | d[6] << 16 | d[7] << 24;
    CHECK(len == d.size());
    uint32_t flags = d[8] | d[9] << 8;
    CHECK(flags == (1 | 2 | 64 | 128));
    CHECK(d[12] == 3 && d[16] == 3);        // fs / hs descriptor counts
    // 20-byte header + fs(9+7+7) + hs(9+7+7)
    CHECK(d.size() == 20 + 23 + 23);
    CHECK(d[20 + 0] == 9 && d[20 + 1] == 4);                  // interface desc
    CHECK(d[20 + 9 + 4] == 64 && d[20 + 23 + 9 + 4] == 0x00 && d[20 + 23 + 9 + 5] == 0x02);  // HS 512
    auto s = build_ffs_strings("iAP2");
    CHECK(s[0] == 2);
    uint32_t sl = s[4] | s[5] << 8;
    CHECK(sl == s.size());
    CHECK(s.size() == 16 + 2 + 5);
}

static void test_role_switch_sequence() {
    RoleSwitchHandler h;
    const uint8_t probe[8] = {0xC0, 0x53, 0, 0, 0, 0, 4, 0};
    const uint8_t sw[8] = {0x40, 0x51, 1, 0, 0, 0, 0, 0};
    auto sw_early = h.handle(parse_setup(sw));
    CHECK(sw_early.action == Ep0Action::Stall && !sw_early.role_switch_requested);
    auto r1 = h.handle(parse_setup(probe));
    CHECK(r1.action == Ep0Action::Reply);
    CHECK((r1.data == std::vector<uint8_t>{1, 0, 0, 0}));
    CHECK(h.state() == RoleSwitchHandler::Probed);
    auto r2 = h.handle(parse_setup(sw));
    CHECK(r2.action == Ep0Action::Ack && r2.role_switch_requested);
    CHECK(h.state() == RoleSwitchHandler::SwitchRequested);
    const uint8_t junk[8] = {0xC0, 0x99, 0, 0, 0, 0, 4, 0};
    CHECK(RoleSwitchHandler().handle(parse_setup(junk)).action == Ep0Action::Stall);
    const uint8_t sw0[8] = {0x40, 0x51, 0, 0, 0, 0, 0, 0};  // wValue 0 = stay
    RoleSwitchHandler h2; h2.handle(parse_setup(probe));
    CHECK(h2.handle(parse_setup(sw0)).action == Ep0Action::Stall);
}

int main() {
    test_dry_run_touches_nothing();
    test_setup_bind_teardown_fake_tree();
    test_takeover_unbinds_g1_first();
    test_bind_without_udc_fails();
    test_ffs_blobs();
    test_role_switch_sequence();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
