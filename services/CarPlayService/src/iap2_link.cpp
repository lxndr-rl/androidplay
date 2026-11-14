#include "iap2_link.hpp"

namespace s10::iap2 {

uint8_t checksum(const uint8_t* data, size_t len) {
    unsigned sum = 0;
    for (size_t i = 0; i < len; ++i) sum += data[i];
    return static_cast<uint8_t>((0x100u - (sum & 0xFFu)) & 0xFFu);
}

std::vector<uint8_t> encode(const Packet& p) {
    size_t total = kHeaderLen + (p.payload.empty() ? 0 : p.payload.size() + 1);
    if (total > 0xFFFF) return {};
    std::vector<uint8_t> out;
    out.reserve(total);
    out.push_back(kStart0);
    out.push_back(kStart1);
    out.push_back(static_cast<uint8_t>(total >> 8));
    out.push_back(static_cast<uint8_t>(total & 0xFF));
    out.push_back(p.control);
    out.push_back(p.seq);
    out.push_back(p.ack);
    out.push_back(p.session);
    out.push_back(checksum(out.data(), 8));
    if (!p.payload.empty()) {
        out.insert(out.end(), p.payload.begin(), p.payload.end());
        out.push_back(checksum(p.payload.data(), p.payload.size()));
    }
    return out;
}

DecodeResult decode(const uint8_t* buf, size_t len) {
    if (len >= 1 && buf[0] != kStart0) return {DecodeStatus::BadStart, 0, {}};
    if (len >= 2 && buf[1] != kStart1) return {DecodeStatus::BadStart, 0, {}};
    if (len < kHeaderLen) return {DecodeStatus::NeedMore, 0, {}};
    size_t total = (static_cast<size_t>(buf[2]) << 8) | buf[3];
    // A payload, if present, is >=1 byte plus 1 checksum byte => total is 9 or >= 11.
    if (total < kHeaderLen || total == kHeaderLen + 1) return {DecodeStatus::BadLength, 0, {}};
    if (checksum(buf, 8) != buf[8]) return {DecodeStatus::BadHeaderChecksum, 0, {}};
    if (len < total) return {DecodeStatus::NeedMore, 0, {}};
    DecodeResult r{DecodeStatus::Ok, total, {}};
    r.packet.control = buf[4];
    r.packet.seq = buf[5];
    r.packet.ack = buf[6];
    r.packet.session = buf[7];
    if (total > kHeaderLen) {
        size_t plen = total - kHeaderLen - 1;
        if (checksum(buf + kHeaderLen, plen) != buf[total - 1])
            return {DecodeStatus::BadPayloadChecksum, 0, {}};
        r.packet.payload.assign(buf + kHeaderLen, buf + kHeaderLen + plen);
    }
    return r;
}

std::vector<Packet> StreamParser::feed(const uint8_t* data, size_t len) {
    buf_.insert(buf_.end(), data, data + len);
    std::vector<Packet> out;
    size_t pos = 0;
    while (pos < buf_.size()) {
        DecodeResult r = decode(buf_.data() + pos, buf_.size() - pos);
        if (r.status == DecodeStatus::Ok) {
            out.push_back(std::move(r.packet));
            pos += r.consumed;
        } else if (r.status == DecodeStatus::NeedMore) {
            break;
        } else {
            ++pos;  // resync: drop one byte and rescan
            ++dropped_;
        }
    }
    buf_.erase(buf_.begin(), buf_.begin() + pos);
    return out;
}

}  // namespace s10::iap2
