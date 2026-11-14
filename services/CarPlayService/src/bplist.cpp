#include "bplist.hpp"
#include <cstdlib>
#include <cstring>

namespace s10::bplist {

std::optional<int64_t> Value::asInt() const {
    if (t == T::Int) return i;
    if (t == T::Str && !s.empty()) {
        char* end = nullptr;
        int base = (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
        long long v = std::strtoll(s.c_str(), &end, base);
        if (end && *end == 0) return v;
    }
    return std::nullopt;
}

std::optional<bool> Value::asBool() const {
    if (t == T::Bool) return b;
    if (t == T::Int) return i != 0;
    if (t == T::Str) { if (s == "true" || s == "1") return true; if (s == "false" || s == "0") return false; }
    return std::nullopt;
}

// ---------------- writer ----------------
namespace {

size_t countNodes(const Value& v) {
    size_t n = 1;
    if (v.t == Value::T::Array) for (const auto& c : v.a) n += countNodes(c);
    if (v.t == Value::T::Dict) for (const auto& kv : v.m) n += 1 + countNodes(kv.second);
    return n;
}

struct Writer {
    size_t refSize;
    bool ok = true;
    std::vector<std::vector<uint8_t>> objs;

    void putRef(std::vector<uint8_t>& o, size_t r) {
        for (int k = static_cast<int>(refSize) - 1; k >= 0; --k) o.push_back(static_cast<uint8_t>(r >> (8 * k)));
    }
    void head(std::vector<uint8_t>& o, uint8_t type, size_t n) {
        if (n < 15) { o.push_back(static_cast<uint8_t>(type | n)); return; }
        o.push_back(static_cast<uint8_t>(type | 0xF));
        if (n < 256) { o.push_back(0x10); o.push_back(static_cast<uint8_t>(n)); }
        else if (n < 65536) { o.push_back(0x11); o.push_back(static_cast<uint8_t>(n >> 8)); o.push_back(static_cast<uint8_t>(n)); }
        else { o.push_back(0x12); for (int k = 3; k >= 0; --k) o.push_back(static_cast<uint8_t>(n >> (8 * k))); }
    }
    size_t emit(const Value& v) {
        size_t idx = objs.size();
        objs.emplace_back();
        std::vector<uint8_t> o;
        switch (v.t) {
        case Value::T::Bool: o.push_back(v.b ? 0x09 : 0x08); break;
        case Value::T::Int:
            if (v.i >= 0 && v.i < 256) { o.push_back(0x10); o.push_back(static_cast<uint8_t>(v.i)); }
            else if (v.i >= 0 && v.i < 65536) { o.push_back(0x11); o.push_back(static_cast<uint8_t>(v.i >> 8)); o.push_back(static_cast<uint8_t>(v.i)); }
            else if (v.i >= 0 && v.i < (1LL << 32)) { o.push_back(0x12); for (int k = 3; k >= 0; --k) o.push_back(static_cast<uint8_t>(v.i >> (8 * k))); }
            else { o.push_back(0x13); for (int k = 7; k >= 0; --k) o.push_back(static_cast<uint8_t>(static_cast<uint64_t>(v.i) >> (8 * k))); }
            break;
        case Value::T::Real: {
            o.push_back(0x23);
            uint64_t bits; std::memcpy(&bits, &v.r, 8);
            for (int k = 7; k >= 0; --k) o.push_back(static_cast<uint8_t>(bits >> (8 * k)));
            break;
        }
        case Value::T::Str:
            for (unsigned char c : v.s) if (c >= 0x80) ok = false;
            head(o, 0x50, v.s.size());
            o.insert(o.end(), v.s.begin(), v.s.end());
            break;
        case Value::T::Data:
            head(o, 0x40, v.d.size());
            o.insert(o.end(), v.d.begin(), v.d.end());
            break;
        case Value::T::Array: {
            std::vector<size_t> refs;
            for (const auto& c : v.a) refs.push_back(emit(c));
            head(o, 0xA0, refs.size());
            for (auto r : refs) putRef(o, r);
            break;
        }
        case Value::T::Dict: {
            std::vector<size_t> kr, vr;
            for (const auto& kv : v.m) { kr.push_back(emit(Value::Str(kv.first))); vr.push_back(emit(kv.second)); }
            head(o, 0xD0, kr.size());
            for (auto r : kr) putRef(o, r);
            for (auto r : vr) putRef(o, r);
            break;
        }
        }
        objs[idx] = std::move(o);
        return idx;
    }
};

}  // namespace

std::vector<uint8_t> write(const Value& root) {
    size_t n = countNodes(root);
    Writer w;
    w.refSize = n < 256 ? 1 : (n < 65536 ? 2 : 4);
    w.emit(root);
    if (!w.ok) return {};
    std::vector<uint8_t> out{'b', 'p', 'l', 'i', 's', 't', '0', '0'};
    std::vector<size_t> offs;
    for (const auto& o : w.objs) { offs.push_back(out.size()); out.insert(out.end(), o.begin(), o.end()); }
    size_t tableOff = out.size();
    size_t offSize = tableOff < 256 ? 1 : (tableOff < 65536 ? 2 : 4);
    for (auto off : offs) for (int k = static_cast<int>(offSize) - 1; k >= 0; --k) out.push_back(static_cast<uint8_t>(off >> (8 * k)));
    uint8_t tr[32] = {0};
    tr[6] = static_cast<uint8_t>(offSize);
    tr[7] = static_cast<uint8_t>(w.refSize);
    for (int k = 0; k < 8; ++k) {
        tr[8 + k] = static_cast<uint8_t>(static_cast<uint64_t>(w.objs.size()) >> (8 * (7 - k)));
        tr[16 + k] = 0;  // top object index 0
        tr[24 + k] = static_cast<uint8_t>(static_cast<uint64_t>(tableOff) >> (8 * (7 - k)));
    }
    out.insert(out.end(), tr, tr + 32);
    return out;
}

// ---------------- reader ----------------
namespace {

struct Reader {
    const uint8_t* p; size_t len;
    size_t offSize, refSize, nObj, tableOff;

    uint64_t be(size_t pos, size_t n) const {
        uint64_t v = 0;
        for (size_t k = 0; k < n; ++k) v = (v << 8) | p[pos + k];
        return v;
    }
    // reads length nibble/int-object at pos (after the marker byte); advances pos
    bool readLen(size_t& pos, uint8_t nib, size_t& n) const {
        if (nib != 0xF) { n = nib; return true; }
        if (pos >= len) return false;
        uint8_t m = p[pos++];
        if ((m >> 4) != 1) return false;
        size_t sz = size_t(1) << (m & 0xF);
        if (sz > 4 || pos + sz > len) return false;
        n = static_cast<size_t>(be(pos, sz));
        pos += sz;
        return true;
    }
    std::optional<Value> parse(uint64_t idx, int depth) const {
        if (depth > 32 || idx >= nObj) return std::nullopt;
        uint64_t off = be(tableOff + idx * offSize, offSize);
        if (off >= tableOff) return std::nullopt;
        size_t pos = static_cast<size_t>(off);
        uint8_t mk = p[pos++];
        uint8_t type = mk >> 4, nib = mk & 0xF;
        switch (type) {
        case 0x0:
            if (mk == 0x08) return Value::Bool(false);
            if (mk == 0x09) return Value::Bool(true);
            return std::nullopt;
        case 0x1: {
            size_t sz = size_t(1) << nib;
            if (sz > 8 || pos + sz > tableOff) return std::nullopt;
            uint64_t v = be(pos, sz);
            if (sz == 8) return Value::Int(static_cast<int64_t>(v));
            return Value::Int(static_cast<int64_t>(v));
        }
        case 0x2: {
            size_t sz = size_t(1) << nib;
            if ((sz != 4 && sz != 8) || pos + sz > tableOff) return std::nullopt;
            if (sz == 8) { uint64_t b = be(pos, 8); double d; std::memcpy(&d, &b, 8); return Value::Real(d); }
            uint32_t b = static_cast<uint32_t>(be(pos, 4)); float f; std::memcpy(&f, &b, 4); return Value::Real(f);
        }
        case 0x4: case 0x5: case 0x6: {
            size_t n;
            if (!readLen(pos, nib, n)) return std::nullopt;
            size_t bytes = type == 0x6 ? n * 2 : n;
            if (n > tableOff || pos + bytes > tableOff) return std::nullopt;
            if (type == 0x4) return Value::Data(std::vector<uint8_t>(p + pos, p + pos + n));
            if (type == 0x5) return Value::Str(std::string(reinterpret_cast<const char*>(p + pos), n));
            std::string s;
            for (size_t k = 0; k < n; ++k) {
                uint32_t c = static_cast<uint32_t>(be(pos + 2 * k, 2));
                if (c < 0x80) s.push_back(static_cast<char>(c));
                else if (c < 0x800) { s.push_back(static_cast<char>(0xC0 | (c >> 6))); s.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
                else { s.push_back(static_cast<char>(0xE0 | (c >> 12))); s.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); s.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
            }
            return Value::Str(s);
        }
        case 0xA: case 0xD: {
            size_t n;
            if (!readLen(pos, nib, n)) return std::nullopt;
            size_t refs = type == 0xD ? n * 2 : n;
            if (n > tableOff || pos + refs * refSize > tableOff) return std::nullopt;
            Value v = type == 0xA ? Value::Array() : Value::Dict();
            for (size_t k = 0; k < n; ++k) {
                if (type == 0xA) {
                    auto c = parse(be(pos + k * refSize, refSize), depth + 1);
                    if (!c) return std::nullopt;
                    v.a.push_back(std::move(*c));
                } else {
                    auto key = parse(be(pos + k * refSize, refSize), depth + 1);
                    auto val = parse(be(pos + (n + k) * refSize, refSize), depth + 1);
                    if (!key || !val || key->t != Value::T::Str) return std::nullopt;
                    v.m.emplace_back(key->s, std::move(*val));
                }
            }
            return v;
        }
        default: return std::nullopt;
        }
    }
};

}  // namespace

std::optional<Value> read(const uint8_t* data, size_t len) {
    if (len < 8 + 32 || std::memcmp(data, "bplist00", 8) != 0) return std::nullopt;
    Reader r{data, len, 0, 0, 0, 0};
    const uint8_t* tr = data + len - 32;
    r.offSize = tr[6]; r.refSize = tr[7];
    if (r.offSize < 1 || r.offSize > 8 || r.refSize < 1 || r.refSize > 8) return std::nullopt;
    uint64_t n = 0, top = 0, to = 0;
    for (int k = 0; k < 8; ++k) { n = (n << 8) | tr[8 + k]; top = (top << 8) | tr[16 + k]; to = (to << 8) | tr[24 + k]; }
    if (n == 0 || n > len || to < 8 || to > len - 32) return std::nullopt;
    if (n * r.offSize > (len - 32) - to) return std::nullopt;
    r.nObj = static_cast<size_t>(n);
    r.tableOff = static_cast<size_t>(to);
    return r.parse(top, 0);
}

}  // namespace s10::bplist
