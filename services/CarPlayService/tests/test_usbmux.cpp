// Host tests for the device-side usbmux. The mux HOST used here is written by us from the usbmuxd source
// (src/device.c, read as a specification), so passing proves self-consistency and the byte layout we derived by
// hand, NOT interoperability with a real head unit.
#include "usbmux.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace s10::usbmux;
using Bytes = std::vector<uint8_t>;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void p32(Bytes& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); }
static void p16(Bytes& v, uint16_t x) { v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); }
static uint32_t g32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
static uint16_t g16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

// Minimal host: same sequencing as usbmuxd (version, setup, SYN, ACK, data).
struct Host {
    Device* dev = nullptr;
    std::vector<Bytes> from_dev;  // transfers received from the device
    bool v2 = false;
    uint16_t txs = 0;
    struct C { uint16_t sport, dport; uint32_t tx_seq = 0, tx_ack = 0; bool up = false, refused = false; Bytes in; } c;

    Bytes pkt(uint32_t proto, const Bytes& body) {
        Bytes v; p32(v, proto); p32(v, uint32_t((v2 ? 16 : 8) + body.size()));
        if (v2) { p32(v, kMagic); p16(v, txs++); p16(v, 0xFFFF); }
        v.insert(v.end(), body.begin(), body.end());
        return v;
    }
    Bytes tcp(uint8_t flags, const Bytes& data = {}) {
        Bytes b; p16(b, c.sport); p16(b, c.dport); p32(b, c.tx_seq); p32(b, c.tx_ack);
        b.push_back(0x50); b.push_back(flags); p16(b, 0x0200); p16(b, 0); p16(b, 0);
        b.insert(b.end(), data.begin(), data.end());
        return pkt(kProtoTcp, b);
    }
    bool send(const Bytes& x) { return dev->feed(x.data(), x.size()); }
    bool handshake() {
        Bytes vb; p32(vb, 2); p32(vb, 0); p32(vb, 0);
        if (!send(pkt(kProtoVersion, vb))) return false;
        if (from_dev.empty()) return false;
        // reply is an 8-byte-header version packet
        const Bytes& r = from_dev.back();
        if (r.size() != 20 || g32(&r[0]) != kProtoVersion || g32(&r[4]) != 20 || g32(&r[8]) != 2) return false;
        v2 = true; txs = 0;
        return send(pkt(kProtoSetup, Bytes{7}));
    }
    // Feed whatever the device emitted since last call into the host connection state.
    void drain() {
        for (auto& t : from_dev) {
            if (t.size() < 16 + kTcpHdr || g32(&t[0]) != kProtoTcp) continue;
            const uint8_t* th = &t[16];
            if (g16(th + 2) != c.sport || g16(th) != c.dport) continue;
            uint8_t fl = th[13];
            if (fl & kRst) { c.refused = true; continue; }
            if (fl == (kSyn | kAck)) { c.tx_seq++; c.tx_ack++; c.up = true; send(tcp(kAck)); continue; }
            size_t n = t.size() - 16 - kTcpHdr;
            if (n) { c.in.insert(c.in.end(), t.begin() + 16 + kTcpHdr, t.end()); c.tx_ack += uint32_t(n); }
        }
        from_dev.clear();
    }
};

struct Rig {
    Host host;
    Bytes got;
    std::vector<uint16_t> closed;
    Device dev;
    Rig(std::set<uint16_t> ports = {62078}, bool accept = true)
        : dev([this](const Bytes& b) { host.from_dev.push_back(b); },
              Device::Handlers{[accept](uint16_t) { return accept; },
                               [this](uint16_t, const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); },
                               [this](uint16_t p) { closed.push_back(p); }},
              std::move(ports)) { host.dev = &dev; }
    bool connect(uint16_t port) {
        host.c = {};
        host.c.sport = 1; host.c.dport = port;
        if (!host.send(host.tcp(kSyn))) return false;
        host.drain();
        return true;
    }
};

static void test_golden_bytes() {
    // Hand-derived from usbmuxd send_packet(): VERSION request with the 8-byte header.
    Host h;
    Bytes vb; p32(vb, 2); p32(vb, 0); p32(vb, 0);
    Bytes v = h.pkt(kProtoVersion, vb);
    const uint8_t want[] = {0,0,0,0, 0,0,0,0x14, 0,0,0,2, 0,0,0,0, 0,0,0,0};
    CHECK(v == Bytes(want, want + sizeof want));
    // SYN to lockdownd (62078 = 0xF27E) as the host's first connection: sport 1, seq 0, window 0x0200.
    h.v2 = true; h.txs = 1; h.c.sport = 1; h.c.dport = 62078;
    Bytes s = h.tcp(kSyn);
    const uint8_t syn[] = {0,0,0,6, 0,0,0,0x24, 0xfe,0xed,0xfa,0xce, 0,1, 0xff,0xff,
                           0,1, 0xf2,0x7e, 0,0,0,0, 0,0,0,0, 0x50, 0x02, 0x02,0x00, 0,0, 0,0};
    CHECK(s == Bytes(syn, syn + sizeof syn));
}

static void test_handshake_and_connect() {
    Rig r;
    CHECK(!r.dev.active());
    CHECK(r.host.handshake());
    CHECK(r.dev.active());
    CHECK(r.connect(62078));
    CHECK(r.host.c.up);
    CHECK(!r.host.c.refused);
    CHECK(r.host.c.tx_ack == 1);
    CHECK(r.host.c.tx_seq == 1);
}

static void test_refused_port_and_policy() {
    Rig r;
    CHECK(r.host.handshake());
    CHECK(r.connect(1234));          // not a listening port
    CHECK(r.host.c.refused && !r.host.c.up);
    Rig q({62078}, false);           // accept() says no
    CHECK(q.host.handshake());
    CHECK(q.connect(62078));
    CHECK(q.host.c.refused && !q.host.c.up);
}

static void test_data_both_ways() {
    Rig r;
    CHECK(r.host.handshake());
    CHECK(r.connect(62078));
    Bytes hello(100, 'x');
    CHECK(r.host.send(r.host.tcp(kAck, hello)));
    r.host.c.tx_seq += uint32_t(hello.size());
    CHECK(r.got == hello);
    r.host.drain();  // device ACK
    Bytes big(40000, 0);
    for (size_t i = 0; i < big.size(); i++) big[i] = uint8_t(i * 7);
    CHECK(r.dev.send(62078, big.data(), big.size()));
    CHECK(r.host.from_dev.size() == 3);  // 16000 + 16000 + 8000
    for (auto& t : r.host.from_dev) CHECK(t.size() < 16384);  // below the host's "continued transfer" size
    r.host.drain();
    CHECK(r.host.c.in == big);
    CHECK(r.dev.pending(62078) == 0);
}

static void test_window_limits_sending() {
    Rig r;
    CHECK(r.host.handshake());
    CHECK(r.connect(62078));
    // Host advertises 0x0200<<8 = 131072. Send more than that: the rest must wait for ACKs.
    Bytes big(200000, 0x5a);
    CHECK(r.dev.send(62078, big.data(), big.size()));
    size_t sent = 0;
    for (auto& t : r.host.from_dev) sent += t.size() - 16 - kTcpHdr;
    CHECK(sent <= 131072);
    CHECK(r.dev.pending(62078) == big.size() - sent);
    r.host.drain();
    CHECK(r.host.send(r.host.tcp(kAck)));  // ack everything received so far, window reopens
    r.host.drain();
    CHECK(r.host.send(r.host.tcp(kAck)));
    r.host.drain();
    CHECK(r.host.send(r.host.tcp(kAck)));
    r.host.drain();
    CHECK(r.host.c.in.size() == big.size());
    CHECK(r.dev.pending(62078) == 0);
}

static void test_reassembly_of_split_transfers() {
    Rig r;
    Bytes vb; p32(vb, 2); p32(vb, 0); p32(vb, 0);
    Bytes v = r.host.pkt(kProtoVersion, vb);
    // byte at a time
    for (uint8_t b : v) CHECK(r.dev.feed(&b, 1));
    CHECK(r.host.from_dev.size() == 1);
    r.host.v2 = true; r.host.txs = 0;
    Bytes s = r.host.pkt(kProtoSetup, Bytes{7});
    Bytes t; r.host.c = {}; r.host.c.sport = 1; r.host.c.dport = 62078; t = r.host.tcp(kSyn);
    Bytes both = s; both.insert(both.end(), t.begin(), t.end());  // two packets in one transfer
    CHECK(r.dev.feed(both.data(), both.size()));
    CHECK(r.dev.active());
    r.host.drain();
    CHECK(r.host.c.up);
}

static void test_close_and_reset() {
    Rig r;
    CHECK(r.host.handshake());
    CHECK(r.connect(62078));
    CHECK(r.host.send(r.host.tcp(kRst)));
    CHECK(r.closed.size() == 1 && r.closed[0] == 62078);
    CHECK(!r.dev.send(62078, (const uint8_t*)"a", 1));
    CHECK(r.connect(62078));         // port reusable after close
    r.dev.close(62078);
    r.host.drain();
    CHECK(r.host.c.refused);         // RST from device
    CHECK(r.closed.size() == 1);     // local close() does not call back
}

static void test_protocol_violations() {
    { Rig r; Bytes b(16, 0); CHECK(!r.dev.feed(b.data(), b.size())); }  // length 0 < header
    { Rig r; Bytes vb; p32(vb, 1); p32(vb, 0); p32(vb, 0); CHECK(!r.host.send(r.host.pkt(kProtoVersion, vb))); }  // v1 unsupported
    { Rig r; CHECK(!r.host.send(r.host.tcp(kSyn))); }                    // TCP before version
    { Rig r; CHECK(r.host.handshake()); Bytes bad = r.host.pkt(kProtoTcp, Bytes(30, 0)); bad[8] = 0; CHECK(!r.dev.feed(bad.data(), bad.size())); }  // bad magic
    { Rig r; CHECK(r.host.handshake()); CHECK(r.connect(62078));
      r.host.c.tx_seq += 5;                                              // sequence gap
      CHECK(r.host.send(r.host.tcp(kAck, Bytes(4, 1))));
      CHECK(r.closed.size() == 1); CHECK(r.got.empty()); }
    { Rig r; CHECK(r.host.handshake()); r.host.c = {}; r.host.c.sport = 7; r.host.c.dport = 62078;
      CHECK(r.host.send(r.host.tcp(kAck, Bytes(4, 1))));                 // data without a connection: RST, no crash
      CHECK(r.got.empty()); }
}

int main() {
    test_golden_bytes();
    test_handshake_and_connect();
    test_refused_port_and_policy();
    test_data_both_ways();
    test_window_limits_sending();
    test_reassembly_of_split_transfers();
    test_close_and_reset();
    test_protocol_violations();
    printf("test_usbmux: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
