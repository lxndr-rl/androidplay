#include "iap2_session.hpp"
#include <cstdio>
using namespace s10::iap2;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// Real capture quoted in OrangeCar docs/protocol.md 3.3 (Jaguar F-PACE SYN) and our SYN|ACK.
static const uint8_t kCarSyn[] = {0xff,0x5a,0x00,0x1a,0x80,0x18,0x00,0x00,0xf5,
    0x01,0x05,0x10,0x00,0x01,0xf4,0x00,0xb0,0x1e,0x0a,0x01,0x00,0x02,0x02,0x01,0x01,0x16};
static const uint8_t kOurSynAck[] = {0xff,0x5a,0x00,0x1a,0xc0,0x00,0x18,0x00,0xb5,
    0x01,0x7f,0xff,0xff,0x01,0xf4,0x00,0xb0,0x1e,0x0a,0x01,0x00,0x02,0x02,0x01,0x01,0xae};
static const uint8_t kDetect[] = {0xff,0x55,0x02,0x00,0xee,0x10};

static Packet mk(uint8_t seq, uint8_t ack, uint8_t sess, const Message& m) {
    Packet p; p.control = kCtrlAck; p.seq = seq; p.ack = ack; p.session = sess;
    p.payload = encodeMessage(m);
    return p;
}

int main() {
    // --- known-answer: captured SYN decodes (both checksums valid) ---
    auto d = decode(kCarSyn, sizeof kCarSyn);
    CHECK(d.status == DecodeStatus::Ok && d.consumed == 26);
    CHECK(d.packet.control == kCtrlSyn && d.packet.seq == 0x18);
    auto lp = parseSyn(d.packet.payload);
    CHECK(lp && lp->controlSession == 1 && lp->fileSession == 2 && lp->retransmitMs == 500 &&
          lp->maxRxLen == 4096 && lp->maxOutstanding == 5 && lp->cumAckMs == 176);
    // Our SYN|ACK payload byte-exact vs the documented one.
    auto sa = encode(Packet{static_cast<uint8_t>(kCtrlSyn | kCtrlAck), 0x00, 0x18, 0, buildSynAckPayload(*lp)});
    CHECK(sa.size() == sizeof kOurSynAck);
    CHECK(std::vector<uint8_t>(kOurSynAck, kOurSynAck + sizeof kOurSynAck) == sa);

    // --- detect echo, SYN -> SYN|ACK, ACK -> Established; fed in 1-byte chunks ---
    DeviceSession s;
    for (uint8_t b : kDetect) s.onBytes(&b, 1);
    auto tx = s.takeTx();
    CHECK(tx.size() == 1 && tx[0] == std::vector<uint8_t>(kDetect, kDetect + 6));
    for (uint8_t b : kCarSyn) s.onBytes(&b, 1);
    CHECK(s.state() == DeviceSession::State::WaitAck);
    tx = s.takeTx();
    CHECK(tx.size() == 1 && tx[0] == std::vector<uint8_t>(kOurSynAck, kOurSynAck + sizeof kOurSynAck));
    auto ackPkt = encode(Packet{kCtrlAck, 0x18, 0x00, 0, {}});
    s.onBytes(ackPkt.data(), ackPkt.size());
    CHECK(s.state() == DeviceSession::State::Established);

    // --- car sends StartIdentification-reply flow: car data seq 0x19 with 0x1D01 ---
    Message idInfo; idInfo.id = kIdentificationInformation;
    idInfo.params.push_back(strParam(0, "Test Car"));
    idInfo.params.push_back(Param{7, {0x4E, 0x09, 0x4E, 0x0A}});
    auto w = encode(mk(0x19, 0x00, 1, idInfo));
    s.onBytes(w.data(), w.size());
    auto msgs = s.takeMessages();
    CHECK(msgs.size() == 1 && msgs[0].id == kIdentificationInformation && msgs[0].params.size() == 2);
    tx = s.takeTx();
    CHECK(tx.size() == 1);
    auto a = decode(tx[0].data(), tx[0].size());
    CHECK(a.status == DecodeStatus::Ok && a.packet.payload.empty() && a.packet.ack == 0x19 && a.packet.seq == 0x00);

    // gate: only 4E09/4E0A accepted
    s.setAccessoryAcceptList(parseMessageList(msgs[0].params[1]));
    CHECK(!s.sendMessage(Message{0x4E0C, {}}));
    CHECK(s.sendMessage(Message{kIdentificationAccepted, {}}) == false);  // 0x1D02 not in list -> gated
    Message info{0x4E09, {strParam(0, "iPhone")}};
    Message lang{0x4E0A, {strParam(0, "en")}};
    CHECK(s.sendMessage(info));
    CHECK(s.sendMessage(lang));            // queued: one outstanding at a time
    tx = s.takeTx();
    CHECK(tx.size() == 1);                 // only first on wire
    auto p1 = decode(tx[0].data(), tx[0].size());
    CHECK(p1.packet.seq == 0x01 && p1.packet.control == kCtrlAck && p1.packet.session == 1);
    CHECK(p1.packet.payload == encodeMessage(info));
    CHECK(s.retransmitPending());
    CHECK(s.takeTx().size() == 1);
    // car ACKs seq 1 -> second goes out with seq 2
    auto ack1 = encode(Packet{kCtrlAck, 0x19, 0x01, 0, {}});
    s.onBytes(ack1.data(), ack1.size());
    tx = s.takeTx();
    CHECK(tx.size() == 1);
    auto p2 = decode(tx[0].data(), tx[0].size());
    CHECK(p2.packet.seq == 0x02 && p2.packet.payload == encodeMessage(lang));
    // wire example from doc: 4E0A language update
    CHECK(encodeMessage(lang) == (std::vector<uint8_t>{0x40,0x40,0x00,0x0D,0x4E,0x0A,0x00,0x07,0x00,0x00,0x65,0x6E,0x00}));

    // out-of-order (seq jumps) is re-ACKed with last in-order, not delivered
    auto ahead = encode(mk(0x1B, 0x02, 1, idInfo));
    s.onBytes(ahead.data(), ahead.size());
    CHECK(s.takeMessages().empty());
    tx = s.takeTx();
    CHECK(tx.size() == 1 && decode(tx[0].data(), tx[0].size()).packet.ack == 0x19);

    // garbage resync
    uint8_t junk[] = {0x00, 0x13, 0xFF};
    s.onBytes(junk, sizeof junk);
    CHECK(s.droppedBytes() >= 2);
    auto in2 = encode(mk(0x1A, 0x02, 1, Message{0xAE00, {}}));
    s.onBytes(in2.data(), in2.size());
    msgs = s.takeMessages();
    CHECK(msgs.size() == 1 && msgs[0].id == 0xAE00);

    // control message reassembly across packets + decode errors
    {
        ControlAssembler ca;
        Message big{0xAA01, {Param{0, std::vector<uint8_t>(908, 0xAB)}}};
        auto e = encodeMessage(big);
        CHECK(e.size() == 6 + 4 + 908);
        CHECK(ca.feed(e.data(), 400).empty());
        auto m = ca.feed(e.data() + 400, e.size() - 400);
        CHECK(m.size() == 1 && m[0].params[0].data.size() == 908);
        uint8_t bad[] = {0x40, 0x40, 0x00, 0x08, 0x00, 0x01, 0x00, 0x09};  // param len > msg
        CHECK(decodeMessage(bad, sizeof bad).status == MsgStatus::BadParam);
        uint8_t bm[] = {0x41};
        CHECK(decodeMessage(bm, 1).status == MsgStatus::BadMagic);
    }

    // RST closes
    auto rst = encode(Packet{kCtrlRst, 0, 0, 0, {}});
    s.onBytes(rst.data(), rst.size());
    CHECK(s.state() == DeviceSession::State::Closed);

    // bad SYN (odd session table) ignored
    { DeviceSession t; Packet bad; bad.control = kCtrlSyn; bad.payload = {1,5,0x10,0,1,0xf4,0,0xb0,0x1e,0x0a,1,0};
      auto bw = encode(bad); t.onBytes(bw.data(), bw.size());
      CHECK(t.state() == DeviceSession::State::WaitSyn && t.takeTx().empty()); }

    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("ALL PASSED\n");
    return 0;
}
