#include "usbmux.hpp"
#include <algorithm>
#include <cstring>

namespace s10::usbmux {
namespace {
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s)); }
void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); }
constexpr size_t kMaxPacket = 65536;  // host side DEV_MRU
constexpr uint32_t kWindow = 131072;  // advertised as 0x0200 (units of 256) like the host does
}  // namespace

int Framer::next(Packet& out) {
    if (buf_.size() < 8) return 0;
    uint32_t len = be32(&buf_[4]);
    size_t hdr = *v2_ ? 16 : 8;
    if (len < hdr || len > kMaxPacket) return -1;
    if (buf_.size() < len) return 0;
    out = Packet();
    out.proto = be32(&buf_[0]);
    out.v2 = *v2_;
    if (*v2_) {
        if (be32(&buf_[8]) != kMagic) return -1;
        out.tx_seq = be16(&buf_[12]);
        out.rx_seq = be16(&buf_[14]);
    }
    out.body.assign(buf_.begin() + hdr, buf_.begin() + len);
    buf_.erase(buf_.begin(), buf_.begin() + len);
    return 1;
}

void Device::emit(uint32_t proto, const uint8_t* body, size_t n) {
    std::vector<uint8_t> v;
    size_t hdr = v2_ ? 16 : 8;
    put32(v, proto);
    put32(v, uint32_t(hdr + n));
    if (v2_) {
        put32(v, kMagic);
        put16(v, tx_seq_++);
        put16(v, rx_seq_);
    }
    if (n) v.insert(v.end(), body, body + n);
    out_(v);
}

void Device::emit_tcp(const Conn& c, uint8_t flags, const uint8_t* d, size_t n) {
    std::vector<uint8_t> b;
    put16(b, c.dev_port);
    put16(b, c.host_port);
    put32(b, c.tx_seq);
    put32(b, c.tx_ack);
    b.push_back(0x50);  // data offset 5 words
    b.push_back(flags);
    put16(b, uint16_t(kWindow >> 8));
    put16(b, 0);  // checksum (host sends 0 as well)
    put16(b, 0);  // urgent
    if (n) b.insert(b.end(), d, d + n);
    emit(kProtoTcp, b.data(), b.size());
}

void Device::rst(uint16_t sport, uint16_t dport, uint32_t ack) {
    Conn c;
    c.host_port = sport;
    c.dev_port = dport;
    c.tx_ack = ack;
    emit_tcp(c, kRst, nullptr, 0);
}

bool Device::feed(const uint8_t* p, size_t n) {
    framer_.append(p, n);
    Packet pk;
    for (;;) {
        int r = framer_.next(pk);
        if (r < 0) return false;
        if (r == 0) return true;
        if (!on_packet(pk)) return false;
    }
}

bool Device::on_packet(const Packet& p) {
    if (p.v2) rx_seq_ = p.tx_seq;
    switch (p.proto) {
    case kProtoVersion: {
        if (p.body.size() < 12) return false;
        if (be32(p.body.data()) != 2) return false;  // v1 (8-byte header only) not implemented
        std::vector<uint8_t> b;
        put32(b, 2);
        put32(b, 0);
        put32(b, 0);
        emit(kProtoVersion, b.data(), b.size());  // still an 8-byte header: the host switches after reading it
        v2_ = true;
        return true;
    }
    case kProtoSetup:
        if (!v2_ || p.body.empty() || p.body[0] != 7) return false;
        tx_seq_ = 0;
        rx_seq_ = 0xFFFF;
        setup_ = true;
        return true;
    case kProtoControl:
        return true;  // host log/error strings; ignored
    case kProtoTcp:
        return v2_ && on_tcp(p);
    default:
        return false;
    }
}

bool Device::on_tcp(const Packet& p) {
    const auto& b = p.body;
    if (b.size() < kTcpHdr) return false;
    size_t off = size_t(b[12] >> 4) * 4;
    if (off < kTcpHdr || off > b.size()) return false;
    uint16_t sport = be16(&b[0]), dport = be16(&b[2]);
    uint32_t seq = be32(&b[4]), ack = be32(&b[8]);
    uint8_t flags = b[13];
    uint32_t win = uint32_t(be16(&b[14])) << 8;
    const uint8_t* pl = b.data() + off;
    size_t pn = b.size() - off;

    auto it = conns_.find(dport);
    if (flags == kSyn) {
        if (it != conns_.end() || !ports_.count(dport) || !h_.accept || !h_.accept(dport)) {
            rst(sport, dport, seq + 1);
            return true;
        }
        Conn c;
        c.host_port = sport;
        c.dev_port = dport;
        c.tx_ack = seq + 1;
        c.peer_win = win;
        emit_tcp(c, kSyn | kAck, nullptr, 0);
        c.tx_seq = 1;
        c.established = true;
        conns_[dport] = std::move(c);
        return true;
    }
    if (it == conns_.end() || it->second.host_port != sport) {
        if (!(flags & kRst)) rst(sport, dport, seq);
        return true;
    }
    Conn& c = it->second;
    if (flags & kRst) {
        if (h_.closed) h_.closed(dport);
        conns_.erase(it);
        return true;
    }
    if (flags & kFin) {
        c.tx_ack = seq + uint32_t(pn) + 1;
        emit_tcp(c, kFin | kAck, nullptr, 0);
        if (h_.closed) h_.closed(dport);
        conns_.erase(it);
        return true;
    }
    c.peer_ack = ack;
    c.peer_win = win;
    if (pn) {
        if (seq != c.tx_ack) {  // USB is reliable and ordered: a gap means a broken host, not loss
            rst(sport, dport, seq);
            if (h_.closed) h_.closed(dport);
            conns_.erase(it);
            return true;
        }
        c.tx_ack += uint32_t(pn);
        if (h_.data) h_.data(dport, pl, pn);
        auto again = conns_.find(dport);  // handler may have closed it
        if (again == conns_.end()) return true;
        emit_tcp(again->second, kAck, nullptr, 0);
        flush(again->second);
        return true;
    }
    flush(c);
    return true;
}

void Device::flush(Conn& c) {
    while (!c.outq.empty()) {
        uint32_t inflight = c.tx_seq - c.peer_ack;
        if (c.peer_win <= inflight) break;
        size_t n = std::min<size_t>({c.outq.size(), kMaxPayload, size_t(c.peer_win - inflight)});
        emit_tcp(c, kAck, c.outq.data(), n);
        c.tx_seq += uint32_t(n);
        c.outq.erase(c.outq.begin(), c.outq.begin() + n);
    }
}

bool Device::send(uint16_t port, const uint8_t* p, size_t n) {
    auto it = conns_.find(port);
    if (it == conns_.end() || !it->second.established) return false;
    it->second.outq.insert(it->second.outq.end(), p, p + n);
    flush(it->second);
    return true;
}

void Device::close(uint16_t port) {
    auto it = conns_.find(port);
    if (it == conns_.end()) return;
    emit_tcp(it->second, kRst, nullptr, 0);
    conns_.erase(it);
}

size_t Device::pending(uint16_t port) const {
    auto it = conns_.find(port);
    return it == conns_.end() ? 0 : it->second.outq.size();
}

}  // namespace s10::usbmux
