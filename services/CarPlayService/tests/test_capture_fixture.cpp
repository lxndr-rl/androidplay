// Feeds a REAL MediaCodec capture (Android emulator API 36, ProjectionService probe: Settings app on a
// VirtualDisplay, 1200x512 baseline H.264) through the AirPlay video path: AU split -> AVCC -> avcC -> AES-CTR.
// The fixture comes from the emulator's software encoder, not from the S10+ hardware encoder.
#include "airplay.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace s10::airplay;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/emulator_settings_1200x512.h264";
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("cannot open %s\n", path); return 2; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(data.size() > 1000);

    // Access units: a VCL NAL (type 1 or 5) closes the AU; SPS/PPS/SEI/AUD before it belong to it (1 slice per frame).
    auto nals = splitAnnexB(data.data(), data.size());
    std::vector<std::vector<uint8_t>> aus;      // Annex-B per AU
    std::vector<uint8_t> cur;
    for (auto& n : nals) {
        cur.insert(cur.end(), {0, 0, 0, 1});
        cur.insert(cur.end(), n.begin(), n.end());
        int t = n[0] & 0x1F;
        if (t == 1 || t == 5) { aus.push_back(cur); cur.clear(); }
    }
    CHECK(cur.empty());
    CHECK(aus.size() >= 60);

    int idrs = 0;
    std::vector<uint8_t> sps, pps;
    StreamCipher enc("424242"), dec("424242");
    for (size_t i = 0; i < aus.size(); ++i) {
        AccessUnit au = toAvcc(aus[i].data(), aus[i].size());
        CHECK(!au.avcc.empty());
        if (i == 0) CHECK(au.idr && !au.sps.empty() && !au.pps.empty());   // stream starts with a decodable IDR
        if (au.idr) { ++idrs; CHECK(!au.sps.empty() && !au.pps.empty()); if (sps.empty()) { sps = au.sps; pps = au.pps; } }
        // AVCC length prefixes must tile the buffer exactly
        size_t pos = 0;
        while (pos + 4 <= au.avcc.size()) {
            size_t l = (size_t(au.avcc[pos]) << 24) | (au.avcc[pos + 1] << 16) | (au.avcc[pos + 2] << 8) | au.avcc[pos + 3];
            pos += 4 + l;
        }
        CHECK(pos == au.avcc.size());
        auto c = enc.process(au.avcc.data(), au.avcc.size());
        auto p = dec.process(c.data(), c.size());
        CHECK(p == au.avcc);
        CHECK(c != au.avcc);
    }
    CHECK(idrs >= 2);   // 1 s GOP over 3 s
    auto avcC = buildAvcC(sps, pps);
    CHECK(avcC.size() > 10 && avcC[0] == 1 && avcC[1] == sps[1] && avcC[2] == sps[2] && avcC[3] == sps[3]);
    CHECK(sps[1] == 0x42);   // Baseline profile_idc 66 (CarPlay legacy path expects baseline)
    std::printf("AUs=%zu IDRs=%d avcC=%zu bytes sps=%zu pps=%zu\n", aus.size(), idrs, avcC.size(), sps.size(), pps.size());
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("ALL PASSED\n");
    return 0;
}
