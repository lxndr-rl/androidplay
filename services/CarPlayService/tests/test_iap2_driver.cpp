#include "iap2_driver.hpp"
#include <cstdio>
#include <deque>
using namespace s10::iap2;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static const uint8_t kCarSyn[] = {0xff,0x5a,0x00,0x1a,0x80,0x18,0x00,0x00,0xf5,
    0x01,0x05,0x10,0x00,0x01,0xf4,0x00,0xb0,0x1e,0x0a,0x01,0x00,0x02,0x02,0x01,0x01,0x16};

// Simulated accessory: speaks the link layer, ACKs every phone DATA packet, collects messages.
struct FakeCar {
    Driver& d;
    uint8_t seq = 0x18;
    uint8_t lastPhoneSeq = 0;
    ControlAssembler rx;
    std::vector<Message> got;
    std::deque<std::vector<uint8_t>> wire;  // bytes to deliver to the phone

    void deliverTx(const std::vector<std::vector<uint8_t>>& tx) {
        for (const auto& b : tx) {
            auto r = decode(b.data(), b.size());
            if (r.status != DecodeStatus::Ok) continue;       // e.g. detect echo
            const Packet& p = r.packet;
            if (p.control & kCtrlSyn) { lastPhoneSeq = p.seq; wire.push_back(encode(Packet{kCtrlAck, seq, p.seq, 0, {}})); continue; }
            if (p.payload.empty()) continue;
            lastPhoneSeq = p.seq;
            for (auto& m : rx.feed(p.payload.data(), p.payload.size())) got.push_back(std::move(m));
            wire.push_back(encode(Packet{kCtrlAck, seq, p.seq, 0, {}}));
        }
    }
    void settle() {
        while (!wire.empty()) { auto b = wire.front(); wire.pop_front(); deliverTx(d.onBytes(b.data(), b.size())); }
    }
    void send(const Message& m) {
        Packet p; p.control = kCtrlAck; p.seq = ++seq; p.ack = lastPhoneSeq; p.session = 1; p.payload = encodeMessage(m);
        wire.push_back(encode(p));
        settle();
    }
    bool has(uint16_t id) const { for (auto& m : got) if (m.id == id) return true; return false; }
    const Message* first(uint16_t id) const { for (auto& m : got) if (m.id == id) return &m; return nullptr; }
    size_t count(uint16_t id) const { size_t n = 0; for (auto& m : got) n += m.id == id; return n; }
};

int main() {
    DriverConfig cfg;
    cfg.unixTime = [] { return uint64_t(1700000000); };
    cfg.random = [](uint8_t* o, size_t n) { for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(i + 1); };
    std::vector<uint8_t> certSeen, chalSeen, respSeen;
    std::vector<std::string> nmea;
    DriverCallbacks cb;
    cb.onCertificate = [&](const std::vector<uint8_t>& c) { certSeen = c; };
    cb.onAuthResponse = [&](const std::vector<uint8_t>& ch, const std::vector<uint8_t>& r) { chalSeen = ch; respSeen = r; };
    cb.onLocation = [&](const std::string& s) { nmea.push_back(s); };
    Driver d(cfg, cb);
    FakeCar car{d};

    // link up: detect, SYN, ACK
    uint8_t det[] = {0xff,0x55,0x02,0x00,0xee,0x10};
    car.deliverTx(d.onBytes(det, 6));
    car.wire.push_back(std::vector<uint8_t>(kCarSyn, kCarSyn + sizeof kCarSyn));
    car.settle();
    CHECK(d.session().state() == DeviceSession::State::Established);
    CHECK(d.phase() == Driver::Phase::Identifying);
    CHECK(car.has(kStartIdentification));               // phone starts identification

    // car identifies; declared "receives from device" list as on the Jaguar; no 4E0C/4E0E
    Message id; id.id = kIdentificationInformation;
    id.params.push_back(strParam(0, "Jaguar F-PACE"));
    Param recv; recv.id = 7;
    for (uint16_t v : {0x4C01,0x4C04,0x5001,0xFFFA,0xFFFC,0x4158,0x4155,0x4E09,0x4E0A,0x4E0B,0x4E04,0xAE01}) {
        recv.data.push_back(uint8_t(v >> 8)); recv.data.push_back(uint8_t(v));
    }
    id.params.push_back(recv);
    id.params.push_back(groupParam(0x0016, {strParam(0, "GPGGA"), strParam(1, "GPRMC"), strParam(2, "GPGSV"), strParam(3, "PASCD")}));
    car.send(id);
    CHECK(car.has(kIdentificationAccepted));
    CHECK(car.has(kRequestAuthCertificate));
    CHECK(d.phase() == Driver::Phase::Authenticating);

    // certificate (908 B, multi-packet is link-agnostic) -> challenge of 32 bytes
    Message cert{kAuthCertificate, {Param{0, std::vector<uint8_t>(908, 0xC3)}}};
    car.send(cert);
    CHECK(certSeen.size() == 908);
    auto ch = car.first(kRequestAuthChallengeResponse);
    CHECK(ch && ch->params.size() == 1 && ch->params[0].data.size() == 32 && ch->params[0].data[0] == 1 && ch->params[0].data[31] == 32);

    // auth response -> AuthSucceeded + gated burst (4E09 4E0A 4E0B FFFA; no 4E0C/4E0E)
    car.send(Message{kAuthResponse, {Param{0, std::vector<uint8_t>(64, 0x5A)}}});
    CHECK(chalSeen.size() == 32 && respSeen.size() == 64);
    CHECK(car.has(kAuthSucceeded));
    CHECK(d.phase() == Driver::Phase::Ready);
    auto di = car.first(0x4E09); CHECK(di && di->params[0].data == (std::vector<uint8_t>{'i','P','h','o','n','e',0}));
    auto tm = car.first(0x4E0B);
    CHECK(tm && tm->params.size() == 3 && tm->params[0].data.size() == 8 && tm->params[0].data[7] == (1700000000 & 0xFF) &&
          tm->params[1].data.size() == 2 && tm->params[2].data.size() == 1);
    auto loc = car.first(0xFFFA);
    CHECK(loc && loc->params.size() == 4);
    CHECK(!car.has(0x4E0C) && !car.has(0x4E0E) && !car.has(0x4E0D));
    // gate blocks an undeclared id
    CHECK(!const_cast<DeviceSession&>(d.session()).sendMessage(Message{0x4E0C, {}}));

    // car subscriptions answered
    car.send(Message{0xAE00, {u8Param(0, 1)}});
    CHECK(car.count(0xAE01) == 2);
    car.send(Message{0x5000, {}});
    auto np = car.first(0x5001);
    CHECK(np);
    if (np) CHECK(encodeMessage(*np) == (std::vector<uint8_t>{0x40,0x40,0x00,0x19,0x50,0x01,0x00,0x13,0x00,0x01,
        0x00,0x05,0x00,0x00,0x00, 0x00,0x05,0x00,0x07,0x00, 0x00,0x05,0x00,0x10,0x00}));  // exact bytes from the doc
    car.send(Message{0x4C00, {}});
    CHECK(car.has(0x4C01));
    car.send(Message{0x4E03, {u16Param(0, 1)}});
    CHECK(car.has(0x4E04));
    car.send(Message{0xAE03, {}});
    car.send(Message{0xFFFB, {strParam(0, "$GPRMC,1,A*00\n")}});
    CHECK(nmea.size() == 1 && nmea[0].rfind("$GPRMC", 0) == 0);

    // group helpers
    { std::vector<Param> out; Param g = groupParam(9, {u8Param(1, 7), strParam(2, "x")});
      CHECK(parseGroup(g, out) && out.size() == 2 && out[0].data[0] == 7);
      Param bad{9, {0x00, 0x09, 0x00, 0x01}}; out.clear(); CHECK(!parseGroup(bad, out)); }

    // RST -> Failed
    auto rst = encode(Packet{kCtrlRst, 0, 0, 0, {}});
    d.onBytes(rst.data(), rst.size());
    CHECK(d.phase() == Driver::Phase::Failed);

    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("ALL PASSED\n");
    return 0;
}
