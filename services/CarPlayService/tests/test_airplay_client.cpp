// End-to-end of the client sequencer against an in-process fake receiver written from the OrangeCar doc.
// Proves internal consistency (ordering, replies, frame layout, decryption with an independently derived key).
// Does NOT prove compatibility with any real head unit.
#include "airplay_client.hpp"
#include <cstdio>
#include <cstring>
#include <deque>
using namespace s10::airplay;
using s10::bplist::Value;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

struct FakeReceiver {
    RtspParser parser;
    std::vector<std::string> seq;                    // "METHOD path[:type]" in arrival order
    bool failSetup = false;                          // answer SETUP /setup with 466
    bool statsKeepAlive = false;
    bool takeScreen = true;
    int64_t connId = 0;
    std::vector<uint8_t> screenBuf;
    std::vector<uint8_t> avcC; float w = 0, h = 0;
    std::vector<std::vector<uint8_t>> plainFrames;   // decrypted AVCC payloads
    std::unique_ptr<StreamCipher> dec;
    std::vector<int> ops;
    std::vector<uint8_t> audioPkts;
    int audioCount = 0;

    std::vector<uint8_t> onControl(const uint8_t* d, size_t n) {
        std::vector<uint8_t> out;
        for (auto& m : parser.feed(d, n)) {
            std::vector<uint8_t> body;
            int status = 200;
            auto v = m.body.empty() ? std::nullopt : s10::bplist::read(m.body.data(), m.body.size());
            std::string tag = m.method + " " + m.path;
            if (m.path == "/setup") {
                if (failSetup) status = 466;
                else {
                    Value r = Value::Dict(); r.set("eventPort", Value::Int(7001)).set("keepAlivePort", Value::Int(7002)).set("timingPort", Value::Int(7003));
                    body = s10::bplist::write(r);
                }
            } else if (m.path == "/info") {
                Value d0 = Value::Dict(); d0.set("widthPixels", Value::Str("1200")).set("heightPixels", Value::Str("512")).set("uuid", Value::Str("DISP-UUID"));
                Value res = Value::Dict(); res.set("resourceID", Value::Int(1)).set("transferType", Value::Int(1)).set("takeConstraint", Value::Str("100"));
                Value modes = Value::Dict(); modes.set("resources", Value::Array().push(res));
                Value info = Value::Dict(); info.set("displays", Value::Array().push(d0)).set("keepAliveSendStatsAsBody", Value::Bool(statsKeepAlive));
                if (takeScreen) info.set("modes", modes);
                body = s10::bplist::write(info);
            } else if (m.path == "/command" && v) {
                tag += ":" + v->get("type")->s;
                if (v->get("type")->s == "modesChanged") {
                    auto& rs = v->get("params")->get("resources")->a;
                    tag += ":" + std::to_string(rs[0].get("entity")->i) + "/" + std::to_string(rs[1].get("entity")->i);
                }
            } else if (m.path == "/" && m.method == "SETUP" && v) {
                auto& st = v->get("streams")->a[0];
                auto type = st.get("type")->i;
                tag += ":" + std::to_string(type);
                Value s = Value::Dict(); s.set("type", Value::Int(type));
                if (type == 110) {
                    connId = st.get("streamConnectionID")->i;
                    dec = std::make_unique<StreamCipher>(std::to_string(static_cast<uint64_t>(connId)));
                    s.set("dataPort", Value::Int(7100));
                } else s.set("dataPort", Value::Int(7200)).set("controlPort", Value::Int(7201));
                Value r = Value::Dict(); r.set("streams", Value::Array().push(s));
                body = s10::bplist::write(r);
            }
            seq.push_back(tag);
            auto rep = buildReply(status, m.cseq, body);
            out.insert(out.end(), rep.begin(), rep.end());
        }
        return out;
    }
    void onScreen(const std::vector<uint8_t>& b) {
        screenBuf.insert(screenBuf.end(), b.begin(), b.end());
        while (screenBuf.size() >= 128) {
            uint32_t len = screenBuf[0] | screenBuf[1] << 8 | screenBuf[2] << 16 | uint32_t(screenBuf[3]) << 24;
            if (screenBuf.size() < 128 + len) break;
            int op = screenBuf[4];
            ops.push_back(op);
            std::vector<uint8_t> payload(screenBuf.begin() + 128, screenBuf.begin() + 128 + len);
            if (op == 1) {
                avcC = payload;
                uint32_t bw = screenBuf[16] | screenBuf[17] << 8 | screenBuf[18] << 16 | uint32_t(screenBuf[19]) << 24;
                uint32_t bh = screenBuf[20] | screenBuf[21] << 8 | screenBuf[22] << 16 | uint32_t(screenBuf[23]) << 24;
                std::memcpy(&w, &bw, 4); std::memcpy(&h, &bh, 4);
            } else if (op == 0 && dec) plainFrames.push_back(dec->process(payload.data(), payload.size()));
            screenBuf.erase(screenBuf.begin(), screenBuf.begin() + 128 + len);
        }
    }
};

struct Harness {
    ClientConfig cfg;
    FakeReceiver car;
    std::vector<TouchEvent> touches;
    std::vector<Action> events;                 // collected non-control actions
    std::vector<int> opened;
    std::vector<std::vector<uint8_t>> eventReplies;
    std::string failed;
    uint64_t ntp = 1000;
    std::unique_ptr<Client> c;
    int keyReq = 0;

    Harness() {
        cfg.uuid = "UUID-1"; cfg.correlationUuid = "UUID-2"; cfg.timingPort = 5000;
        cfg.randomConnId = [] { return int64_t(123456789012345678LL); };
        cfg.nowNtp = [this] { return ntp; };
        ClientCallbacks cb;
        cb.onTouch = [this](const TouchEvent& e) { touches.push_back(e); };
        cb.onKeyFrameRequested = [this] { ++keyReq; };
        c = std::make_unique<Client>(cfg, cb);
    }
    void run(std::vector<Action> acts) {
        std::deque<Action> q(acts.begin(), acts.end());
        while (!q.empty()) {
            Action a = q.front(); q.pop_front();
            std::vector<Action> more;
            switch (a.kind) {
            case Action::Kind::SendControl: {
                auto rep = car.onControl(a.bytes.data(), a.bytes.size());
                more = c->onControlBytes(rep.data(), rep.size());
                break;
            }
            case Action::Kind::OpenEvent: case Action::Kind::OpenScreen: case Action::Kind::OpenAudio:
                opened.push_back(a.port); break;
            case Action::Kind::SendScreen: car.onScreen(a.bytes); break;
            case Action::Kind::SendAudio: ++car.audioCount; car.audioPkts = a.bytes; break;
            case Action::Kind::SendEvent: eventReplies.push_back(a.bytes); break;
            case Action::Kind::SendUdpKeepAlive: events.push_back(a); break;
            case Action::Kind::Failed: failed = a.note; break;
            }
            q.insert(q.end(), more.begin(), more.end());
        }
    }
    void carRequest(const Value& body, int cseq = 1) {
        auto req = buildRequest("POST", "/command", cseq, s10::bplist::write(body));
        run(c->onEventBytes(req.data(), req.size()));
    }
};

static std::vector<uint8_t> nalu(std::initializer_list<uint8_t> v) { return std::vector<uint8_t>(v); }
static std::vector<uint8_t> annexB(std::vector<std::vector<uint8_t>> nals) {
    std::vector<uint8_t> o;
    for (auto& n : nals) { o.insert(o.end(), {0, 0, 0, 1}); o.insert(o.end(), n.begin(), n.end()); }
    return o;
}

int main() {
    const auto sps = nalu({0x67, 0x42, 0xC0, 0x1F, 0xAA}), pps = nalu({0x68, 0xCE, 0x3C});
    const auto idr = nalu({0x65, 0x88, 0x84, 0x00, 0x11}), pframe = nalu({0x41, 0x9A, 0x22});

    // ---------- happy path ----------
    {
        Harness h;
        h.run(h.c->start());
        CHECK(h.failed.empty());
        const std::vector<std::string> expect = {
            "SETUP /setup", "GET /info", "RECORD /",
            "POST /command:modesChanged:2/1",   // initial screen sync (car offers Take/100)
            "POST /command:modesChanged:1/1",
            "SETUP /:110", "SETUP /:100"};
        CHECK(h.car.seq == expect);
        CHECK(h.c->state() == Client::State::Streaming);
        CHECK(h.opened == (std::vector<int>{7001, 7100, 7200}));          // event, screen, audio
        CHECK(h.c->info() && h.c->info()->width == 1200 && h.c->info()->height == 512);
        CHECK(h.c->needKeyFrame());

        // video: P-frame before first IDR dropped; IDR carries SPS/PPS -> config + frame
        auto p0 = annexB({pframe});
        h.run(h.c->pushVideo(p0.data(), p0.size()));
        CHECK(h.car.ops.empty());
        auto au1 = annexB({nalu({0x09, 0xF0}), sps, pps, idr});
        h.ntp = 1000 + 5;
        h.run(h.c->pushVideo(au1.data(), au1.size()));
        CHECK(h.car.ops == (std::vector<int>{1, 0}));
        CHECK(h.car.avcC == buildAvcC(sps, pps) && h.car.w == 1200.f && h.car.h == 512.f);
        CHECK(!h.c->needKeyFrame());
        auto au2 = annexB({pframe});
        h.run(h.c->pushVideo(au2.data(), au2.size()));
        CHECK(h.car.plainFrames.size() == 2);
        // receiver decrypted with its own key derived from the SETUP value; continuity across frames proven
        CHECK(h.car.plainFrames[0] == toAvcc(au1.data(), au1.size()).avcc);
        CHECK(h.car.plainFrames[1] == toAvcc(au2.data(), au2.size()).avcc);

        // audio: 1 packet from 1408 bytes
        std::vector<uint8_t> pcm(1408, 7);
        h.run(h.c->pushAudioPcm(pcm.data(), pcm.size()));
        CHECK(h.car.audioCount == 1 && h.car.audioPkts[1] == 100 && h.car.audioPkts[12] == 7);

        // housekeeping once per second: feedback + keepalive frame + UDP keepalive
        size_t before = h.car.seq.size();
        h.run(h.c->tick(10000));
        CHECK(h.car.seq.size() == before + 1 && h.car.seq.back() == "POST /feedback");
        CHECK(h.car.ops.back() == 2);
        CHECK(h.events.size() == 1 && h.events[0].port == 7002 && h.events[0].bytes == std::vector<uint8_t>(4, 0));
        h.run(h.c->tick(10500));
        CHECK(h.car.seq.size() == before + 1);          // no second one within 1 s
        h.run(h.c->tick(11000));
        CHECK(h.car.seq.size() == before + 2);

        // timing responder
        std::vector<uint8_t> t(32, 0); t[0] = 0x80; t[1] = 0xD2; t[3] = 7; t[24] = 0xAB;
        auto tr = h.c->onTimingPacket(t.data(), t.size());
        CHECK(tr.size() == 32 && tr[1] == 0xD3 && tr[8] == 0xAB);
    }

    // ---------- car-initiated commands on the event channel ----------
    {
        Harness h;
        h.run(h.c->start());
        size_t base = h.car.seq.size();
        // Borrow main audio: reply first with status 0, then fresh modesChanged (audio entity 2)
        Value res = Value::Dict(); res.set("resourceID", Value::Int(2)).set("transferType", Value::Int(3));
        Value p = Value::Dict(); p.set("resources", Value::Array().push(res));
        Value cm = Value::Dict(); cm.set("type", Value::Str("changeModes")).set("params", p);
        h.carRequest(cm, 5);
        CHECK(h.eventReplies.size() == 1);
        RtspParser rp; auto rm = rp.feed(h.eventReplies[0].data(), h.eventReplies[0].size());
        CHECK(rm.size() == 1 && rm[0].status == 200 && rm[0].cseq == 5);
        auto rv = s10::bplist::read(rm[0].body.data(), rm[0].body.size());
        CHECK(rv && rv->get("status")->i == 0 && rv->get("params")->get("resources")->a[1].get("entity")->i == 2);
        CHECK(h.car.seq.size() == base + 1 && h.car.seq.back() == "POST /command:modesChanged:1/2");
        CHECK(h.c->modes().audioEntity == 2 && h.c->modes().audioPermanent == 1);
        CHECK(h.c->needKeyFrame() && h.keyReq == 1);
        // Unborrow
        Value res4 = Value::Dict(); res4.set("resourceID", Value::Int(2)).set("transferType", Value::Int(4));
        Value p2 = Value::Dict(); p2.set("resources", Value::Array().push(res4));
        Value cm2 = Value::Dict(); cm2.set("type", Value::Str("changeModes")).set("params", p2);
        h.carRequest(cm2, 6);
        CHECK(h.car.seq.back() == "POST /command:modesChanged:1/1");
        // Take is refused with status 1, no state change, no new modesChanged
        size_t n = h.car.seq.size();
        Value rs = Value::Dict(); rs.set("resourceID", Value::Int(1)).set("transferType", Value::Int(1));
        Value p3 = Value::Dict(); p3.set("resources", Value::Array().push(rs));
        Value cm3 = Value::Dict(); cm3.set("type", Value::Str("changeModes")).set("params", p3);
        h.carRequest(cm3, 7);
        RtspParser rp3; auto rm3 = rp3.feed(h.eventReplies.back().data(), h.eventReplies.back().size());
        auto rv3 = s10::bplist::read(rm3[0].body.data(), rm3[0].body.size());
        CHECK(rv3 && rv3->get("status")->i == 1 && h.car.seq.size() == n);
        // forceKeyFrame + requestUI
        Value fk = Value::Dict(); fk.set("type", Value::Str("forceKeyFrame"));
        auto au = annexB({sps, pps, idr}); h.run(h.c->pushVideo(au.data(), au.size()));
        CHECK(!h.c->needKeyFrame());
        h.carRequest(fk, 8);
        CHECK(h.c->needKeyFrame());
        h.run(h.c->pushVideo(au.data(), au.size()));
        CHECK(h.car.ops == (std::vector<int>{1, 0, 1, 0}));   // config re-sent with the next IDR
        // touch via hidSendReport, scaled; ignored while the car owns the screen
        Value hid = Value::Dict(); hid.set("type", Value::Str("hidSendReport")).set("uuid", Value::Str("TOUCH")).set("hidReport", Value::Data({1, 0xFF, 0x7F, 0xFF, 0x7F}));
        h.carRequest(hid, 9);
        CHECK(h.touches.size() == 1 && h.touches[0].kind == TouchEvent::Kind::Down && h.touches[0].x == 1199 && h.touches[0].y == 511);
        Value unk = Value::Dict(); unk.set("type", Value::Str("somethingNew"));
        h.carRequest(unk, 10);
        RtspParser ru; auto rmu = ru.feed(h.eventReplies.back().data(), h.eventReplies.back().size());
        CHECK(rmu.size() == 1 && rmu[0].status == 200 && rmu[0].cseq == 10);   // unknown commands answered 200
    }

    // ---------- failure paths ----------
    {
        Harness h; h.car.failSetup = true;
        h.run(h.c->start());
        CHECK(h.c->state() == Client::State::Failed && h.failed.find("466") != std::string::npos && h.failed.find("pairing") != std::string::npos);
        CHECK(h.car.seq.size() == 1);
        auto none = h.c->pushVideo(nullptr, 0);
        CHECK(none.empty());
    }
    {   // no initial sync when the car does not offer Take/100; stats keepalive when the car asks
        Harness h; h.car.takeScreen = false; h.car.statsKeepAlive = true;
        h.run(h.c->start());
        CHECK(h.car.seq.size() == 6 && h.car.seq[3] == "POST /command:modesChanged:1/1");
        h.run(h.c->tick(5000));
        CHECK(h.car.ops.back() == 5);
    }
    {   // missing config
        Client c(ClientConfig{});
        auto a = c.start();
        CHECK(a.size() == 1 && a[0].kind == Action::Kind::Failed && c.state() == Client::State::Failed);
    }
    {   // garbage on the event channel
        Harness h; h.run(h.c->start());
        const char* g = "nonsense\r\n\r\n";
        h.run(h.c->onEventBytes(reinterpret_cast<const uint8_t*>(g), strlen(g)));
        CHECK(h.c->state() == Client::State::Failed);
    }

    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("ALL PASSED\n");
    return 0;
}
