#include "airplay_audio.hpp"
#include <cstdio>
using namespace s10::airplay;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static std::vector<uint8_t> tone(size_t frames, uint8_t v) { return std::vector<uint8_t>(frames * 4, v); }
static uint16_t seqOf(const std::vector<uint8_t>& p) { return uint16_t(p[2] << 8 | p[3]); }

int main() {
    // ---- cadence: 1 s of real time at 48 kHz = 136 packets (48000/352 = 136.36) ----
    {
        AudioPacer p;
        CHECK(p.poll(0).empty());                               // first poll only starts the clock
        size_t total = 0;
        auto ms = tone(48, 5);                                  // live capture: 1 ms of audio every 1 ms
        for (uint64_t t = 1000; t <= 1'000'000; t += 1000) { p.push(ms.data(), ms.size(), t); total += p.poll(t).size(); }
        CHECK(total == 136);
        CHECK(p.stats().silencePackets == 0);
    }
    // ---- bounded latency: oldest dropped, whole frames only ----
    {
        AudioPacer p;
        auto big = tone(48000, 1);                              // 1 s
        p.push(big.data(), big.size(), 0);
        CHECK(p.bufferedBytes() == 12000 * 4);                  // 250 ms @ 48 kHz = 12000 frames
        CHECK(p.stats().droppedBytes == (48000 - 12000) * 4);
        p.push(big.data(), 3, 0);                               // partial frame stays buffered, no crash
        CHECK(p.bufferedBytes() % 1 == 0);
    }
    // ---- short gap: no zeros inserted; long gap: silence at cadence; recovery: real audio again ----
    {
        AudioPacer p;
        p.poll(0);
        p.push(tone(352 * 4, 9).data(), 352 * 4 * 4, 0);       // 4 packets of data
        size_t real = 0, sil = 0;
        auto count = [&](const std::vector<std::vector<uint8_t>>& v) { for (auto& x : v) { if (x[12] == 9) ++real; else if (x[12] == 0) ++sil; } };
        for (uint64_t t = 7334; t <= 150'000; t += 7334) count(p.poll(t));    // data runs out after 4, gap < 200 ms
        CHECK(real == 4 && sil == 0 && p.stats().skippedTicks > 0);
        for (uint64_t t = 157'000; t <= 400'000; t += 7334) count(p.poll(t)); // gap > 200 ms: zeros
        CHECK(sil > 10);
        auto before = p.stats().silencePackets;
        p.push(tone(352 * 2, 9).data(), 352 * 2 * 4, 400'000);
        size_t realAfter = 0;
        for (uint64_t t = 407'000; t <= 430'000; t += 7334) for (auto& x : p.poll(t)) if (x[12] == 9) ++realAfter;
        CHECK(realAfter == 2 && p.stats().silencePackets >= before);
    }
    // ---- sequence numbers continue across real and silence packets ----
    {
        AudioPacer p;
        p.poll(0);
        std::vector<uint16_t> seqs;
        for (uint64_t t = 7334; t <= 600'000; t += 7334) for (auto& x : p.poll(t)) seqs.push_back(seqOf(x));
        bool ok = !seqs.empty();
        for (size_t i = 1; i < seqs.size(); ++i) ok = ok && seqs[i] == uint16_t(seqs[i - 1] + 1);
        CHECK(ok);
    }
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("ALL PASSED\n");
    return 0;
}
