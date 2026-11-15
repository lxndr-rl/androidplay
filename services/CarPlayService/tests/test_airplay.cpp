#include "airplay.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstring>
using namespace s10::airplay;
using s10::bplist::Value;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static std::string hex(const uint8_t* d, size_t n) { static const char* h = "0123456789abcdef"; std::string s; for (size_t i = 0; i < n; ++i) { s += h[d[i] >> 4]; s += h[d[i] & 15]; } return s; }

// Python plistlib output of {"a":1,"b":[True,"x",2.5],"c":{"n":300,"big":70000},"e":b"\x01\x02","s":"hi","neg":-5}
static const uint8_t kPyPlist[] = {
0x62,0x70,0x6c,0x69,0x73,0x74,0x30,0x30,0xd6,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x0c,0x11,0x12,0x13,0x51,0x61,0x51,0x62,0x51,0x63,0x51,0x65,0x53,0x6e,0x65,0x67,0x51,0x73,0x10,0x01,0xa3,0x09,0x0a,0x0b,0x09,0x51,0x78,0x23,0x40,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0xd2,0x0d,0x0e,0x0f,0x10,0x53,0x62,0x69,0x67,0x51,0x6e,0x12,0x00,0x01,0x11,0x70,0x11,0x01,0x2c,0x42,0x01,0x02,0x13,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfb,0x52,0x68,0x69,0x08,0x15,0x17,0x19,0x1b,0x1d,0x21,0x23,0x25,0x29,0x2a,0x2c,0x35,0x3a,0x3e,0x40,0x45,0x48,0x4b,0x54,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x57};

int main() {
    // ---- bplist: read real Python output ----
    {
        auto v = s10::bplist::read(kPyPlist, sizeof kPyPlist);
        CHECK(v && v->t == Value::T::Dict && v->m.size() == 6);
        if (v) {
            CHECK(v->get("a")->i == 1);
            CHECK(v->get("neg")->i == -5);
            CHECK(v->get("s")->s == "hi");
            CHECK(v->get("e")->d == (std::vector<uint8_t>{1, 2}));
            auto* b = v->get("b");
            CHECK(b && b->a.size() == 3 && b->a[0].b && b->a[1].s == "x" && b->a[2].r == 2.5);
            CHECK(v->get("c")->get("n")->i == 300 && v->get("c")->get("big")->i == 70000);
        }
        // truncations / corruption never crash
        for (size_t n = 0; n < sizeof kPyPlist; ++n) (void)s10::bplist::read(kPyPlist, n);
        for (size_t i = 0; i < sizeof kPyPlist; ++i) { auto c = std::vector<uint8_t>(kPyPlist, kPyPlist + sizeof kPyPlist); c[i] ^= 0xFF; (void)s10::bplist::read(c.data(), c.size()); }
        // writer -> reader round trip (and file for python plistlib cross-check)
        Value w = Value::Dict();
        w.set("i", Value::Int(70000)).set("n", Value::Int(-5)).set("t", Value::Bool(true))
         .set("s", Value::Str(std::string(40, 'q'))).set("arr", Value::Array().push(Value::Int(1)).push(Value::Real(1.5)));
        auto bytes = s10::bplist::write(w);
        auto rt = s10::bplist::read(bytes.data(), bytes.size());
        CHECK(rt && rt->get("s")->s.size() == 40 && rt->get("n")->i == -5 && rt->get("arr")->a[1].r == 1.5);
        if (const char* p = std::getenv("S10_DUMP_BPLIST")) { std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
        CHECK(s10::bplist::write(Value::Str("\xC3\xA9")).empty());           // non-ASCII rejected
        CHECK(keepAliveBody().size() == 42);                                // matches doc: opcode 5 body is 42 bytes
        Value s = Value::Str("0x8000"), d = Value::Str("100"), bad = Value::Str("zz");
        CHECK(s.asInt() == 0x8000 && d.asInt() == 100 && !bad.asInt());
    }

    // ---- RTSP ----
    {
        auto body = s10::bplist::write(initialSetup(SetupConfig{"AA:BB:CC:DD:EE:FF", "U1", "U2", 5000}));
        auto req = buildRequest("SETUP", "/setup", 1, body);
        std::string head(req.begin(), req.begin() + 170);
        CHECK(head.rfind("SETUP /setup RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: AirPlay/566.25.21\r\nContent-Type: application/x-apple-binary-plist\r\nContent-Length: ", 0) == 0);
        auto rec = buildRequest("RECORD", "/", 2);
        CHECK(std::string(rec.begin(), rec.end()) == "RECORD / RTSP/1.0\r\nCSeq: 2\r\nUser-Agent: AirPlay/566.25.21\r\n\r\n");  // no body -> no Content-*
        // parse a response with a bplist body fed byte by byte, then a car request
        Value rep = Value::Dict(); rep.set("eventPort", Value::Int(7001)).set("keepAlivePort", Value::Str("7002"));
        auto rb = s10::bplist::write(rep);
        std::string rh = "RTSP/1.0 200 OK\r\nCSeq: 1\r\ncontent-length: " + std::to_string(rb.size()) + "\r\n\r\n";
        std::vector<uint8_t> wire(rh.begin(), rh.end()); wire.insert(wire.end(), rb.begin(), rb.end());
        auto cmd = buildRequest("POST", "/command", 9, rb);
        wire.insert(wire.end(), cmd.begin(), cmd.end());
        RtspParser p; std::vector<RtspMessage> got;
        for (uint8_t b : wire) for (auto& m : p.feed(&b, 1)) got.push_back(std::move(m));
        CHECK(got.size() == 2 && !p.failed());
        CHECK(got[0].status == 200 && got[0].cseq == 1 && got[0].body == rb);
        CHECK(got[1].isRequest && got[1].method == "POST" && got[1].path == "/command" && got[1].cseq == 9);
        auto sr = s10::bplist::read(got[0].body.data(), got[0].body.size());
        auto setup = sr ? parseSetupReply(*sr) : std::nullopt;
        CHECK(setup && setup->eventPort == 7001 && setup->keepAlivePort == 7002);
        { auto ok = buildOkReply(9); CHECK(std::string(ok.begin(), ok.end()) == "RTSP/1.0 200 OK\r\nCSeq: 9\r\n\r\n"); }
        RtspParser bad; const char* g = "garbage line\r\n\r\n"; bad.feed(reinterpret_cast<const uint8_t*>(g), strlen(g));
        CHECK(bad.failed());
        RtspParser huge; std::string hh = "RTSP/1.0 200 OK\r\nContent-Length: 99999999\r\n\r\n"; huge.feed(reinterpret_cast<const uint8_t*>(hh.data()), hh.size());
        CHECK(huge.failed());
    }

    // ---- bodies / info (Jaguar sends numbers as strings) ----
    {
        Value d0 = Value::Dict(); d0.set("widthPixels", Value::Str("1200")).set("heightPixels", Value::Int(512)).set("uuid", Value::Str("DISP-1"));
        Value info = Value::Dict(); info.set("displays", Value::Array().push(d0)).set("keepAliveSendStatsAsBody", Value::Bool(true));
        auto r = parseInfo(info);
        CHECK(r && r->width == 1200 && r->height == 512 && r->displayUuid == "DISP-1" && r->keepAliveStats);
        CHECK(!parseInfo(Value::Dict()));
        auto ms = screenSetup(123456789012345LL, "DISP-1");
        auto st = ms.get("streams")->a[0];
        CHECK(st.get("type")->i == 110 && st.get("latencyMs")->i == 25 && st.get("streamConnectionID")->i == 123456789012345LL);
        auto is = initialSetup(SetupConfig{});
        CHECK(is.get("et")->i == 1 && is.get("sourceVersion")->s == "566.25.21" && is.get("keepAliveLowPower")->b);
        auto mc = modesChanged(ModesState{});
        CHECK(mc.get("type")->s == "modesChanged");
        auto& res = mc.get("params")->get("resources")->a;
        CHECK(res.size() == 2 && res[0].get("entity")->i == 1 && res[1].get("resourceID")->i == 2);
        ModesState call; call.phoneCall = true; call.audioEntity = 2;
        auto mc2 = modesChanged(call);
        CHECK(mc2.get("params")->get("appStates")->a[0].get("entity")->i == 1);
        CHECK(mc2.get("params")->get("resources")->a[1].get("entity")->i == 2);
    }

    // ---- frame headers ----
    {
        auto h = frameHeader(Op::VideoConfig, 0x1234, 0, 1200.f, 512.f);
        CHECK(h.size() == 128 && h[0] == 0x34 && h[1] == 0x12 && h[4] == 1 && h[6] == 0x04);
        float w; uint32_t b = h[16] | h[17] << 8 | h[18] << 16 | uint32_t(h[19]) << 24; std::memcpy(&w, &b, 4);
        CHECK(w == 1200.f);
        auto f = frameHeader(Op::VideoFrame, 10, 0x0102030405060708ULL);
        CHECK(f[4] == 0 && f[6] == 0 && f[8] == 0x08 && f[15] == 0x01);
        auto k = frameHeader(Op::KeepAliveStats, 42);
        CHECK(k[4] == 5 && k[0] == 42 && k[124] == 0x00 && k[125] == 0x00 && k[126] == 0x28 && k[127] == 0x42);   // 42.0f LE
        auto k2 = frameHeader(Op::KeepAlive, 0);
        CHECK(k2[4] == 2 && k2[0] == 0);
    }

    // ---- H.264 ----
    {
        std::vector<uint8_t> sps{0x67, 0x42, 0xC0, 0x1F, 0xAA}, pps{0x68, 0xCE, 0x3C};
        auto avcc = buildAvcC(sps, pps);
        CHECK(hex(avcc.data(), avcc.size()) == "0142c01fffe1" "0005" "6742c01faa" "01" "0003" "68ce3c");
        std::vector<uint8_t> annex{0,0,0,1,0x09,0xF0, 0,0,0,1,0x67,0x42,0xC0,0x1F,0xAA, 0,0,1,0x68,0xCE,0x3C, 0,0,0,1,0x65,0x88,0x84,0x00};
        auto au = toAvcc(annex.data(), annex.size());
        CHECK(au.idr && au.sps == sps && au.pps == pps);
        CHECK(hex(au.avcc.data(), au.avcc.size()) == "00000005" "6742c01faa" "00000003" "68ce3c" "00000004" "65888400");
        auto nals = splitAnnexB(annex.data(), annex.size());
        CHECK(nals.size() == 4);
        CHECK(buildAvcC({0x67, 1}, pps).empty());
    }

    // ---- AES-CTR: golden vector from the doc + openssl CLI keystream, continuity across frames ----
    {
        StreamCipher c("0");
        CHECK(c.ok() && hex(c.key().data(), 16) == "51376cf5cf85231963cec9ba9f4806b6" && hex(c.iv().data(), 16) == "025641101bed4a53c861a33a234074e2");
        std::vector<uint8_t> z(48, 0);
        auto a = c.process(z.data(), 5);       // partial block...
        auto b = c.process(z.data() + 5, 43);  // ...continues in the next frame
        a.insert(a.end(), b.begin(), b.end());
        StreamCipher d("0");
        auto whole = d.process(z.data(), 48);
        CHECK(a == whole);
        CHECK(hex(whole.data(), 48) == "6cb1e1b4a9a1b0444a58244667fbb6565715e040f770143bd20429202cbfbbf977879734db304c3a7222470c4ccf807a");
        StreamCipher e("12345678901234567890");
        CHECK(e.key() != c.key());
        CHECK(c.process(nullptr, 0).empty());
    }

    // ---- audio RTP ----
    {
        AudioPacketizer ap(2, 65535, 0xFFFFFF00u);
        std::vector<uint8_t> pcm(1408 * 2 + 100);
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<uint8_t>(i);
        auto pk = ap.feed(pcm.data(), pcm.size());
        CHECK(pk.size() == 2 && pk[0].size() == 12 + 1408);
        CHECK(pk[0][0] == 0x80 && pk[0][1] == 100 && pk[0][2] == 0xFF && pk[0][3] == 0xFF);
        CHECK(pk[1][2] == 0x00 && pk[1][3] == 0x00);                                   // seq wraps
        CHECK(pk[0][4] == 0xFF && pk[0][5] == 0xFF && pk[0][6] == 0xFF && pk[0][7] == 0x00);
        CHECK(pk[1][4] == 0x00 && pk[1][5] == 0x00 && pk[1][6] == 0x00 && pk[1][7] == 0x60);  // 0xFFFFFF00 + 352 wraps to 0x60
        CHECK(pk[0][8] == 0 && pk[0][11] == 0);                                        // SSRC 0
        CHECK(pk[0][12] == 1 && pk[0][13] == 0 && pk[0][14] == 3 && pk[0][15] == 2);   // LE->BE swap
        auto more = ap.feed(pcm.data(), 1408 - 100);                                   // completes the pending 100 bytes
        CHECK(more.size() == 1);
        auto sil = ap.silence();
        CHECK(sil.size() == 12 + 1408 && sil[12] == 0 && sil[1419] == 0 && sil[3] == 2);
    }

    // ---- timing ----
    {
        std::vector<uint8_t> req(32, 0); req[0] = 0x80; req[1] = 0xD2; req[3] = 7;
        for (int i = 0; i < 8; ++i) req[24 + i] = static_cast<uint8_t>(0xA0 + i);
        uint64_t now = ntpFromUnixMicros(1700000000123456ULL);
        CHECK((now >> 32) == 1700000000ULL + 2208988800ULL);
        auto r = timingReply(req.data(), req.size(), now);
        CHECK(r.size() == 32 && r[0] == 0x80 && r[1] == 0xD3 && r[3] == 7 && r[4] == 0 && r[8] == 0xA0 && r[15] == 0xA7);
        CHECK(r[16] == r[24] && r[19] == r[27] && r[20] == uint8_t(now >> 24));
        CHECK(timingReply(req.data(), 31, now).empty());
        req[1] = 0xD0; CHECK(timingReply(req.data(), 32, now).empty());
    }

    // ---- touch: logical range from the HID descriptor of a real radio (Zlink 5.2.44, captured 2026-11-15) ----
    {
        const uint8_t desc[] = {0x05,0x0d,0x09,0x04,0xa1,0x01,0x05,0x0d,0x09,0x22,0xa1,0x02,0x05,0x0d,0x09,0x33,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02,
            0x75,0x07,0x95,0x01,0x81,0x01,0x05,0x01,0x09,0x30,0x15,0x00,0x26,0x00,0x05,0x75,0x10,0x95,0x01,0x81,0x02,0x09,0x31,0x15,0x00,0x26,0xd0,0x02,0x75,0x10,0x95,0x01,0x81,0x02,0xc0,0xc0};
        int mx = 0, my = 0;
        CHECK(hidTouchLogicalMax(desc, sizeof desc, mx, my) && mx == 1280 && my == 720);
        TouchTracker t(1280, 720, mx, my);
        uint8_t tap[5] = {1, 0xcb, 0x02, 0x8c, 0x01};      // a real report from the radio: x=715 y=396 (pixels)
        auto e = t.onReport(tap, 5);
        CHECK(e && e->kind == TouchEvent::Kind::Down && e->x == 714 && e->y == 395);
        uint8_t corner[5] = {1, 0x00, 0x05, 0xd0, 0x02};   // 1280,720 -> bottom-right
        e = t.onReport(corner, 5);
        CHECK(e && e->x == 1279 && e->y == 719);
        CHECK(!hidTouchLogicalMax(desc, 10, mx, my));      // truncated descriptor: no answer
    }
    // ---- touch ----
    {
        TouchTracker t(1200, 512);
        uint8_t down[5] = {1, 0xFF, 0x7F, 0xFF, 0x7F};      // 32767,32767 -> bottom-right
        auto e = t.onReport(down, 5);
        CHECK(e && e->kind == TouchEvent::Kind::Down && e->x == 1199 && e->y == 511);
        uint8_t mid[5] = {1, 0x00, 0x40, 0x00, 0x40};       // 16384
        e = t.onReport(mid, 5);
        CHECK(e && e->kind == TouchEvent::Kind::Move && e->x == 599 && e->y == 255);
        uint8_t up[5] = {0, 0x00, 0x40, 0x00, 0x40};
        e = t.onReport(up, 5);
        CHECK(e && e->kind == TouchEvent::Kind::Up);
        CHECK(!t.onReport(up, 5));
        CHECK(!t.onReport(up, 4));
        uint8_t over[5] = {1, 0xFF, 0xFF, 0xFF, 0xFF};      // out of range clamps
        e = t.onReport(over, 5);
        CHECK(e && e->x == 1199 && e->y == 511);
    }

    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("ALL PASSED\n");
    return 0;
}
