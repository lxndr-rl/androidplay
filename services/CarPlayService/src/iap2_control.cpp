#include "iap2_control.hpp"

namespace s10::iap2 {

static void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

Param strParam(uint16_t id, const std::string& s) {
    Param p; p.id = id;
    p.data.assign(s.begin(), s.end());
    p.data.push_back(0);
    return p;
}
Param u8Param(uint16_t id, uint8_t v) { return Param{id, {v}}; }
Param u16Param(uint16_t id, uint16_t v) { return Param{id, {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)}}; }
Param u64Param(uint16_t id, uint64_t v) {
    Param p; p.id = id;
    for (int i = 7; i >= 0; --i) p.data.push_back(static_cast<uint8_t>(v >> (8 * i)));
    return p;
}

Param groupParam(uint16_t id, const std::vector<Param>& ch) {
    Param g; g.id = id;
    for (const auto& c : ch) {
        uint16_t l = static_cast<uint16_t>(4 + c.data.size());
        g.data.push_back(static_cast<uint8_t>(l >> 8)); g.data.push_back(static_cast<uint8_t>(l));
        g.data.push_back(static_cast<uint8_t>(c.id >> 8)); g.data.push_back(static_cast<uint8_t>(c.id));
        g.data.insert(g.data.end(), c.data.begin(), c.data.end());
    }
    return g;
}

bool parseGroup(const Param& g, std::vector<Param>& out) {
    size_t off = 0;
    while (off < g.data.size()) {
        if (g.data.size() - off < 4) return false;
        size_t pl = (static_cast<size_t>(g.data[off]) << 8) | g.data[off + 1];
        if (pl < 4 || off + pl > g.data.size()) return false;
        out.push_back(Param{static_cast<uint16_t>((g.data[off + 2] << 8) | g.data[off + 3]),
                            std::vector<uint8_t>(g.data.begin() + static_cast<std::ptrdiff_t>(off + 4),
                                                 g.data.begin() + static_cast<std::ptrdiff_t>(off + pl))});
        off += pl;
    }
    return true;
}

std::vector<uint8_t> encodeMessage(const Message& m) {
    size_t total = 6;
    for (const auto& p : m.params) {
        if (p.data.size() + 4 > 0xFFFF) return {};
        total += 4 + p.data.size();
    }
    if (total > 0xFFFF) return {};
    std::vector<uint8_t> out;
    out.reserve(total);
    out.push_back(0x40); out.push_back(0x40);
    put16(out, static_cast<uint16_t>(total));
    put16(out, m.id);
    for (const auto& p : m.params) {
        put16(out, static_cast<uint16_t>(4 + p.data.size()));
        put16(out, p.id);
        out.insert(out.end(), p.data.begin(), p.data.end());
    }
    return out;
}

MsgResult decodeMessage(const uint8_t* buf, size_t len) {
    MsgResult r{};
    r.status = MsgStatus::NeedMore;
    if (len >= 1 && buf[0] != 0x40) { r.status = MsgStatus::BadMagic; return r; }
    if (len >= 2 && buf[1] != 0x40) { r.status = MsgStatus::BadMagic; return r; }
    if (len < 6) return r;
    size_t total = (static_cast<size_t>(buf[2]) << 8) | buf[3];
    if (total < 6) { r.status = MsgStatus::BadLength; return r; }
    if (len < total) return r;
    r.msg.id = static_cast<uint16_t>((buf[4] << 8) | buf[5]);
    size_t off = 6;
    while (off < total) {
        if (total - off < 4) { r.status = MsgStatus::BadParam; return r; }
        size_t pl = (static_cast<size_t>(buf[off]) << 8) | buf[off + 1];
        if (pl < 4 || off + pl > total) { r.status = MsgStatus::BadParam; return r; }
        Param p;
        p.id = static_cast<uint16_t>((buf[off + 2] << 8) | buf[off + 3]);
        p.data.assign(buf + off + 4, buf + off + pl);
        r.msg.params.push_back(std::move(p));
        off += pl;
    }
    r.status = MsgStatus::Ok;
    r.consumed = total;
    return r;
}

std::vector<Message> ControlAssembler::feed(const uint8_t* data, size_t len) {
    buf_.insert(buf_.end(), data, data + len);
    std::vector<Message> out;
    size_t pos = 0;
    while (pos < buf_.size()) {
        auto r = decodeMessage(buf_.data() + pos, buf_.size() - pos);
        if (r.status == MsgStatus::NeedMore) break;
        if (r.status == MsgStatus::Ok) {
            pos += r.consumed;
            out.push_back(std::move(r.msg));
        } else {
            ++pos; ++dropped_;  // resync byte by byte
        }
    }
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos));
    return out;
}

std::vector<uint16_t> parseMessageList(const Param& p) {
    std::vector<uint16_t> v;
    for (size_t i = 0; i + 1 < p.data.size(); i += 2)
        v.push_back(static_cast<uint16_t>((p.data[i] << 8) | p.data[i + 1]));
    return v;
}

bool accepts(const std::vector<uint16_t>& list, uint16_t id) {
    if (list.empty()) return true;
    for (auto x : list) if (x == id) return true;
    return false;
}

}  // namespace s10::iap2
