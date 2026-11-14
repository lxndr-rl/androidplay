// Real capture from the Android emulator (API 36): REMOTE_SUBMIX of a 440 Hz AudioTrack, 48 kHz 16-bit stereo LE, 1 s.
// Pushed live (10 ms chunks) through AudioPacer; RTP payloads must reproduce the capture exactly (BE->LE), with no
// silence inserted and the tone still at 440 Hz. Emulator audio path, not the S10+ audio HAL.
#include "airplay_audio.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace s10::airplay;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static double goertzel(const std::vector<int16_t>& x, double f) {
    double k = 2 * std::cos(2 * M_PI * f / 48000), s1 = 0, s2 = 0;
    for (auto v : x) { double s0 = v + k * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(s1 * s1 + s2 * s2 - k * s1 * s2) / x.size();
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/emulator_tone440_48k_stereo_1s.pcm";
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("cannot open %s\n", path); return 2; }
    std::vector<uint8_t> pcm((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(pcm.size() == 192000);

    AudioPacer p;
    std::vector<uint8_t> rebuilt;
    p.poll(0);
    const size_t chunk = 480 * 4;   // 10 ms
    for (size_t i = 0, t = 10'000; i + chunk <= pcm.size(); i += chunk, t += 10'000) {
        p.push(pcm.data() + i, chunk, t);
        for (auto& pk : p.poll(t)) {
            CHECK(pk.size() == 12 + 1408 && pk[0] == 0x80 && pk[1] == 100);
            for (size_t k = 12; k + 1 < pk.size(); k += 2) { rebuilt.push_back(pk[k + 1]); rebuilt.push_back(pk[k]); }   // BE -> LE
        }
    }
    CHECK(p.stats().silencePackets == 0 && p.stats().droppedBytes == 0);
    CHECK(rebuilt.size() >= 130 * 1408);                   // ~1 s of packets
    CHECK(std::equal(rebuilt.begin(), rebuilt.end(), pcm.begin()));   // bit-exact prefix of the capture
    std::vector<int16_t> left;
    for (size_t i = 0; i + 3 < rebuilt.size(); i += 4) left.push_back(int16_t(rebuilt[i] | rebuilt[i + 1] << 8));
    double a440 = goertzel(left, 440), a1000 = goertzel(left, 1000), a300 = goertzel(left, 300);
    CHECK(a440 > 50 * (a1000 + a300 + 0.001));
    std::printf("packets=%llu bytes=%zu a440=%.1f a1000=%.2f\n", (unsigned long long)p.stats().packets, rebuilt.size(), a440, a1000);
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("ALL PASSED\n");
    return 0;
}
