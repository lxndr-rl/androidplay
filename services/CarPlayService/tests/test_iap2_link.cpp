#include "iap2_link.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace s10::iap2;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// SYN payload constants as read from the gist (13 bytes).
static const uint8_t kSynPayload[13] = {0x01,0x05,0x10,0x00,0x07,0xD0,0x00,0x16,0x1E,0x03,0x01,0x00,0x02};

int main() {
    // checksum: sum + checksum == 0 mod 256
    { uint8_t d[] = {1,2,3,250}; uint8_t c = checksum(d, 4); CHECK(((1+2+3+250+c) & 0xFF) == 0); }
    CHECK(checksum(nullptr, 0) == 0);

    // Known-answer: SYN header per gist parameters (seq=100, ack=0, session=0, len=9+13+1=23).
    // header bytes 0..7 = FF 5A 00 17 80 64 00 00 -> sum=0x1B0? compute by hand:
    // 0xFF+0x5A=0x159; +0x17=0x170; +0x80=0x1F0; +0x64=0x254 -> &0xFF=0x54; checksum=0xAC
    Packet syn; syn.control = kCtrlSyn; syn.seq = 100; syn.payload.assign(kSynPayload, kSynPayload + 13);
    auto w = encode(syn);
    CHECK(w.size() == 23);
    CHECK(w[0]==0xFF && w[1]==0x5A && w[2]==0x00 && w[3]==0x17 && w[4]==0x80 && w[5]==0x64 && w[8]==0xAC);
    unsigned ps = 0; for (int i = 0; i < 13; ++i) ps += kSynPayload[i];
    CHECK(w[22] == static_cast<uint8_t>((0x100 - (ps & 0xFF)) & 0xFF));

    // Payload-less ACK is exactly 9 bytes.
    Packet ack; ack.control = kCtrlAck; ack.seq = 101; ack.ack = 100;
    auto a = encode(ack);
    CHECK(a.size() == 9);
    auto ra = decode(a.data(), a.size());
    CHECK(ra.status == DecodeStatus::Ok && ra.consumed == 9 && ra.packet.ack == 100 && ra.packet.payload.empty());

    // Round trip with payload.
    auto rs = decode(w.data(), w.size());
    CHECK(rs.status == DecodeStatus::Ok && rs.packet.payload == syn.payload && rs.packet.seq == 100);

    // Truncation -> NeedMore at every prefix length.
    for (size_t n = 0; n < w.size(); ++n) CHECK(decode(w.data(), n).status == DecodeStatus::NeedMore);

    // Corruption detection.
    { auto b = w; b[5] ^= 1; CHECK(decode(b.data(), b.size()).status == DecodeStatus::BadHeaderChecksum); }
    { auto b = w; b[12] ^= 1; CHECK(decode(b.data(), b.size()).status == DecodeStatus::BadPayloadChecksum); }
    { auto b = w; b[0] = 0; CHECK(decode(b.data(), b.size()).status == DecodeStatus::BadStart); }
    { auto b = w; b[2] = 0; b[3] = 5; b[8] = checksum(b.data(), 8); CHECK(decode(b.data(), b.size()).status == DecodeStatus::BadLength); }

    // Oversize payload rejected.
    { Packet big; big.payload.assign(70000, 1); CHECK(encode(big).empty()); }

    // Stream parser: garbage + split chunks + two packets back to back.
    {
        std::vector<uint8_t> stream = {0x00, 0xFF, 0x00, 0x12};  // garbage incl. false start
        stream.insert(stream.end(), w.begin(), w.end());
        stream.insert(stream.end(), a.begin(), a.end());
        StreamParser sp; size_t got = 0;
        for (size_t i = 0; i < stream.size(); ++i) got += sp.feed(&stream[i], 1).size();
        CHECK(got == 2);
        CHECK(sp.droppedBytes() == 4);
        StreamParser sp2; auto v = sp2.feed(stream.data(), stream.size());
        CHECK(v.size() == 2 && v[0].payload == syn.payload && v[1].control == kCtrlAck);
    }
    // Fuzz-ish: random bytes never crash.
    { srand(1); StreamParser sp; for (int i = 0; i < 20000; ++i) { uint8_t b = rand(); sp.feed(&b, 1); } }

    std::printf(fails ? "FAILED (%d)\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
