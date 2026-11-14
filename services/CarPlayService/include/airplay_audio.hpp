// Real-time pacing for the CarPlay audio stream (pure: caller supplies the clock).
//
// EVIDENCE: sbuysse/OrangeCar docs/protocol.md 6.5: PCM 48 kHz/16-bit/stereo, 352 frames per RTP packet (one packet
// every 7.33 ms), silence (zeros) after the source has been quiet for more than 200 ms so the stream never stops, and
// the car plays about 250 ms behind what is sent. The buffer cap/drop policy below is OUR design choice (bounded
// latency), not from the source. Not tested against a car.
#pragma once
#include "airplay.hpp"
#include <cstdint>
#include <deque>
#include <vector>

namespace s10::airplay {

class AudioPacer {
public:
    struct Config {
        int channels = 2;
        uint32_t sampleRate = 48000;
        uint64_t maxBufferedUs = 250'000;     // beyond this the OLDEST samples are dropped (bounded latency)
        uint64_t silenceAfterUs = 200'000;    // source quiet this long -> send zero packets
    };
    struct Stats {
        uint64_t packets = 0, silencePackets = 0, skippedTicks = 0, droppedBytes = 0;
    };

    AudioPacer() : AudioPacer(Config{}) {}
    explicit AudioPacer(Config c);

    // 16-bit little-endian interleaved PCM from the capture side. Any chunk size.
    void push(const uint8_t* pcmLe, size_t n, uint64_t nowUs);
    // Packets due by `nowUs` (monotonic microseconds). Real data first; zeros once starved > silenceAfterUs.
    std::vector<std::vector<uint8_t>> poll(uint64_t nowUs);

    const Stats& stats() const { return stats_; }
    size_t bufferedBytes() const { return buf_.size(); }

private:
    Config cfg_;
    AudioPacketizer pk_;
    std::deque<uint8_t> buf_;
    Stats stats_;
    bool started_ = false;
    uint64_t startUs_ = 0, ticksDone_ = 0, lastDataUs_ = 0;
    size_t packetBytes_;
};

}  // namespace s10::airplay
