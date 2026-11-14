#include "iap2_session.hpp"

namespace s10::iap2 {

static const uint8_t kDetect[6] = {0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10};

std::optional<LinkParams> parseSyn(const std::vector<uint8_t>& p) {
    if (p.size() < 10) return std::nullopt;
    LinkParams l;
    l.version = p[0];
    l.maxOutstanding = p[1];
    l.maxRxLen = static_cast<uint16_t>((p[2] << 8) | p[3]);
    l.retransmitMs = static_cast<uint16_t>((p[4] << 8) | p[5]);
    l.cumAckMs = static_cast<uint16_t>((p[6] << 8) | p[7]);
    l.maxRetrans = p[8];
    l.maxCumAcks = p[9];
    size_t rest = p.size() - 10;
    if (rest % 3 != 0) return std::nullopt;
    l.rawSessions.assign(p.begin() + 10, p.end());
    l.fileSession = 0;
    bool haveCtrl = false;
    for (size_t i = 0; i < rest; i += 3) {
        uint8_t id = l.rawSessions[i], type = l.rawSessions[i + 1];
        if (type == 0 && !haveCtrl) { l.controlSession = id; haveCtrl = true; }
        else if (type == 1) l.fileSession = id;
    }
    if (!haveCtrl) return std::nullopt;
    return l;
}

std::vector<uint8_t> buildSynAckPayload(const LinkParams& c) {
    std::vector<uint8_t> o;
    o.push_back(c.version);
    o.push_back(0x7F);
    o.push_back(0xFF); o.push_back(0xFF);
    o.push_back(static_cast<uint8_t>(c.retransmitMs >> 8)); o.push_back(static_cast<uint8_t>(c.retransmitMs));
    o.push_back(static_cast<uint8_t>(c.cumAckMs >> 8)); o.push_back(static_cast<uint8_t>(c.cumAckMs));
    o.push_back(c.maxRetrans);
    o.push_back(c.maxCumAcks);
    o.insert(o.end(), c.rawSessions.begin(), c.rawSessions.end());
    return o;
}

void DeviceSession::emit(Packet p) { tx_.push_back(encode(p)); }

void DeviceSession::sendAck() {
    // ACK-only carries the last DATA seq we sent (txSeq_), not the next one.
    Packet a; a.control = kCtrlAck; a.seq = txSeq_; a.ack = lastRxSeq_; a.session = 0;
    emit(a);
}

void DeviceSession::onBytes(const uint8_t* data, size_t len) {
    rx_.insert(rx_.end(), data, data + len);
    size_t pos = 0;
    while (pos < rx_.size() && state_ != State::Closed) {
        const uint8_t* b = rx_.data() + pos;
        size_t n = rx_.size() - pos;
        if (b[0] == 0xFF && n >= 2 && b[1] == 0x55) {
            if (n < 6) break;
            bool same = true;
            for (int i = 0; i < 6; ++i) same = same && b[i] == kDetect[i];
            if (same) { tx_.emplace_back(kDetect, kDetect + 6); pos += 6; }
            else { ++pos; ++dropped_; }
            continue;
        }
        if (b[0] == 0xFF && n == 1) break;
        auto r = decode(b, n);
        if (r.status == DecodeStatus::NeedMore) break;
        if (r.status == DecodeStatus::Ok) { pos += r.consumed; handlePacket(r.packet); }
        else { ++pos; ++dropped_; }
    }
    rx_.erase(rx_.begin(), rx_.begin() + static_cast<std::ptrdiff_t>(pos));
}

void DeviceSession::handlePacket(const Packet& p) {
    if (p.control & kCtrlRst) { state_ = State::Closed; return; }
    if (p.control & kCtrlSyn) {
        auto l = parseSyn(p.payload);
        if (!l) return;
        params_ = *l;
        txSeq_ = 0;
        lastRxSeq_ = p.seq;
        pending_.reset();
        Packet sa; sa.control = static_cast<uint8_t>(kCtrlSyn | kCtrlAck);
        sa.seq = 0; sa.ack = p.seq; sa.payload = buildSynAckPayload(*l);
        emit(sa);
        state_ = State::WaitAck;
        return;
    }
    if (state_ == State::WaitAck && (p.control & kCtrlAck)) state_ = State::Established;
    if (state_ != State::Established) return;
    if ((p.control & kCtrlAck) && pending_ && p.ack == pending_->seq) { pending_.reset(); pump(); }
    if (p.payload.empty()) return;
    uint8_t expect = static_cast<uint8_t>(lastRxSeq_ + 1);
    if (p.seq == expect) {
        lastRxSeq_ = p.seq;
        sendAck();
        if (p.session == params_.controlSession) {
            for (auto& m : asm_.feed(p.payload.data(), p.payload.size())) msgs_.push_back(std::move(m));
        }
    } else {
        sendAck();  // ahead or behind: re-ACK last in-order seq (no buffering of future packets)
    }
}

bool DeviceSession::sendMessage(const Message& m, bool bypassGate) {
    if (state_ != State::Established) return false;
    if (gated_ && !bypassGate && !accepts(accept_, m.id)) return false;
    auto body = encodeMessage(m);
    if (body.empty() || body.size() > static_cast<size_t>(params_.maxRxLen) - 10) return false;
    outq_.push_back(std::move(body));
    pump();
    return true;
}

void DeviceSession::pump() {
    if (pending_ || outq_.empty()) return;
    Packet d; d.control = kCtrlAck;
    d.seq = static_cast<uint8_t>(txSeq_ + 1);
    d.ack = lastRxSeq_;
    d.session = params_.controlSession;
    d.payload = std::move(outq_.front());
    outq_.pop_front();
    txSeq_ = d.seq;
    pending_ = d;
    emit(d);
}

bool DeviceSession::retransmitPending() {
    if (!pending_) return false;
    Packet d = *pending_;
    d.ack = lastRxSeq_;
    emit(d);
    return true;
}

std::vector<std::vector<uint8_t>> DeviceSession::takeTx() { auto o = std::move(tx_); tx_.clear(); return o; }
std::vector<Message> DeviceSession::takeMessages() { auto o = std::move(msgs_); msgs_.clear(); return o; }

}  // namespace s10::iap2
