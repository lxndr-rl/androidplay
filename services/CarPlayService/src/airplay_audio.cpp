#include "airplay_audio.hpp"

namespace s10::airplay {

AudioPacer::AudioPacer(Config c)
    : cfg_(c), pk_(c.channels), packetBytes_(kRtpFramesPerPacket * 2 * static_cast<size_t>(c.channels)) {}

void AudioPacer::push(const uint8_t* d, size_t n, uint64_t nowUs) {
    if (n == 0) return;
    buf_.insert(buf_.end(), d, d + n);
    lastDataUs_ = nowUs;
    size_t frame = 2 * static_cast<size_t>(cfg_.channels);
    size_t maxBytes = static_cast<size_t>(cfg_.maxBufferedUs * cfg_.sampleRate / 1'000'000) * frame;
    if (buf_.size() > maxBytes) {
        size_t drop = (buf_.size() - maxBytes) / frame * frame;   // whole frames only: keeps channel alignment
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(drop));
        stats_.droppedBytes += drop;
    }
}

std::vector<std::vector<uint8_t>> AudioPacer::poll(uint64_t nowUs) {
    std::vector<std::vector<uint8_t>> out;
    if (!started_) { started_ = true; startUs_ = nowUs; lastDataUs_ = nowUs; return out; }
    // Sample clock: tick k is due at k * 352 / rate seconds after the start.
    uint64_t dueTicks = (nowUs - startUs_) * cfg_.sampleRate / (1'000'000ULL * kRtpFramesPerPacket);
    while (ticksDone_ < dueTicks) {
        ++ticksDone_;
        if (buf_.size() >= packetBytes_) {
            std::vector<uint8_t> chunk(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(packetBytes_));
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(packetBytes_));
            auto pk = pk_.feed(chunk.data(), chunk.size());
            for (auto& p : pk) { out.push_back(std::move(p)); ++stats_.packets; }
        } else if (nowUs - lastDataUs_ > cfg_.silenceAfterUs) {
            out.push_back(pk_.silence());
            ++stats_.packets; ++stats_.silencePackets;
        } else {
            ++stats_.skippedTicks;   // short gap: wait for data instead of inserting a click of zeros
        }
    }
    return out;
}

}  // namespace s10::airplay
