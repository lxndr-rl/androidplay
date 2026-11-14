#include "airplay.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <openssl/evp.h>

namespace s10::airplay {
using bplist::Value;

// ---------------- RTSP ----------------
std::vector<uint8_t> buildRequest(const std::string& method, const std::string& path, int cseq,
                                  const std::vector<uint8_t>& body) {
    std::string h = method + " " + path + " RTSP/1.0\r\nCSeq: " + std::to_string(cseq) +
                    "\r\nUser-Agent: AirPlay/" + kSourceVersion + "\r\n";
    if (!body.empty())
        h += "Content-Type: application/x-apple-binary-plist\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    h += "\r\n";
    std::vector<uint8_t> out(h.begin(), h.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<uint8_t> buildOkReply(int cseq) {
    std::string s = "RTSP/1.0 200 OK\r\nCSeq: " + std::to_string(cseq) + "\r\n\r\n";
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::vector<uint8_t> buildReply(int status, int cseq, const std::vector<uint8_t>& body) {
    std::string h = "RTSP/1.0 " + std::to_string(status) + (status == 200 ? " OK" : " Error") + "\r\nCSeq: " + std::to_string(cseq) + "\r\n";
    if (!body.empty()) h += "Content-Type: application/x-apple-binary-plist\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    h += "\r\n";
    std::vector<uint8_t> out(h.begin(), h.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

std::vector<RtspMessage> RtspParser::feed(const uint8_t* d, size_t n) {
    std::vector<RtspMessage> out;
    if (failed_) return out;
    buf_.insert(buf_.end(), d, d + n);
    for (;;) {
        static const char kEnd[] = "\r\n\r\n";
        auto it = std::search(buf_.begin(), buf_.end(), kEnd, kEnd + 4);
        if (it == buf_.end()) { if (buf_.size() > 65536) failed_ = true; break; }
        size_t hdrLen = static_cast<size_t>(it - buf_.begin()) + 4;
        std::string head(buf_.begin(), it);
        RtspMessage m;
        size_t pos = 0, eol = head.find("\r\n");
        std::string first = head.substr(0, eol == std::string::npos ? head.size() : eol);
        if (first.rfind("RTSP/1.0", 0) == 0) {
            m.status = std::atoi(first.c_str() + 8);
            if (m.status == 0) { failed_ = true; break; }
        } else {
            size_t a = first.find(' '), b = first.rfind(' ');
            if (a == std::string::npos || b == a || first.substr(b + 1).rfind("RTSP/", 0) != 0) { failed_ = true; break; }
            m.isRequest = true;
            m.method = first.substr(0, a);
            m.path = first.substr(a + 1, b - a - 1);
        }
        size_t contentLen = 0;
        pos = eol == std::string::npos ? head.size() : eol + 2;
        while (pos < head.size()) {
            size_t e = head.find("\r\n", pos);
            std::string line = head.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            pos = e == std::string::npos ? head.size() : e + 2;
            size_t c = line.find(':');
            if (c == std::string::npos) continue;
            std::string k = lower(line.substr(0, c)), v = line.substr(c + 1);
            v.erase(0, v.find_first_not_of(' '));
            if (k == "cseq") m.cseq = std::atoi(v.c_str());
            else if (k == "content-length") {
                long long l = std::atoll(v.c_str());
                if (l < 0 || l > (8 << 20)) { failed_ = true; return out; }
                contentLen = static_cast<size_t>(l);
            } else if (k == "content-type") m.contentType = v;
        }
        if (buf_.size() < hdrLen + contentLen) break;
        m.body.assign(buf_.begin() + static_cast<std::ptrdiff_t>(hdrLen),
                      buf_.begin() + static_cast<std::ptrdiff_t>(hdrLen + contentLen));
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(hdrLen + contentLen));
        out.push_back(std::move(m));
    }
    return out;
}

// ---------------- bodies ----------------
Value initialSetup(const SetupConfig& c) {
    Value v = Value::Dict();
    v.set("deviceID", Value::Str(c.deviceId))
     .set("macAddress", Value::Str(c.deviceId))
     .set("model", Value::Str("iPhone8.1"))
     .set("name", Value::Str("iPhone"))
     .set("osName", Value::Str("iPhone OS"))
     .set("osVersion", Value::Str("15.0.2"))
     .set("osBuildVersion", Value::Str("19A404"))
     .set("sourceVersion", Value::Str(kSourceVersion))
     .set("sessionUUID", Value::Str(c.uuid))
     .set("sessionCorrelationUUID", Value::Str(c.correlationUuid))
     .set("timingPort", Value::Int(c.timingPort))
     .set("statsCollectionEnabled", Value::Bool(false))
     .set("updateSessionRequest", Value::Bool(false))
     .set("keepAliveLowPower", Value::Bool(true))
     .set("et", Value::Int(1))   // kAirPlayEncryptionType_None; 0 makes the car answer 466
     .set("isScreenMirroringSession", Value::Bool(false))
     .set("features", Value::Array());
    return v;
}

Value screenSetup(int64_t id, const std::string& uuid) {
    Value s = Value::Dict();
    s.set("type", Value::Int(110)).set("streamConnectionID", Value::Int(id))
     .set("latencyMs", Value::Int(25)).set("uuid", Value::Str(uuid));
    Value streams = Value::Array(); streams.push(s);
    Value v = Value::Dict(); v.set("streams", streams);
    return v;
}

Value audioSetup(const std::string& audioType, int mask, bool input, int dataPort, int controlPort) {
    Value s = Value::Dict();
    s.set("type", Value::Int(100)).set("audioType", Value::Str(audioType))
     .set("audioFormat", Value::Int(mask))   // mask-vs-bit index UNVERIFIED in source
     .set("input", Value::Bool(input)).set("streamConnectionID", Value::Int(0));
    if (input) s.set("dataPort", Value::Int(dataPort));
    s.set("audioLatencyMs", Value::Int(100)).set("supportsRTPPacketRedundancy", Value::Bool(false))
     .set("supportsHighAccuracyTimestamps", Value::Bool(true)).set("controlPort", Value::Int(controlPort));
    Value streams = Value::Array(); streams.push(s);
    Value v = Value::Dict(); v.set("streams", streams);
    return v;
}

Value modesChanged(const ModesState& st) {
    auto res = [](int id, int e, int p) { Value r = Value::Dict(); r.set("resourceID", Value::Int(id)).set("entity", Value::Int(e)).set("permanentEntity", Value::Int(p)); return r; };
    auto app = [](int id, int e) { Value r = Value::Dict(); r.set("appStateID", Value::Int(id)).set("entity", Value::Int(e)); return r; };
    Value resources = Value::Array();
    resources.push(res(1, st.screenEntity, st.screenPermanent)).push(res(2, st.audioEntity, st.audioPermanent));
    Value speech = app(1, 0); speech.set("speechMode", Value::Int(-1));
    Value states = Value::Array();
    states.push(app(2, st.phoneCall ? 1 : 0)).push(app(3, 0)).push(speech);
    Value params = Value::Dict(); params.set("resources", resources).set("appStates", states);
    Value v = Value::Dict(); v.set("type", Value::Str("modesChanged")).set("params", params);
    return v;
}

// ---------------- replies ----------------
static std::optional<int> intField(const Value& d, const char* k) {
    if (d.t != Value::T::Dict) return std::nullopt;
    auto* p = d.get(k);
    if (!p) return std::nullopt;
    auto i = p->asInt();
    if (!i || *i < 0 || *i > 0x7FFFFFFF) return std::nullopt;
    return static_cast<int>(*i);
}

std::optional<InfoReply> parseInfo(const Value& v) {
    if (v.t != Value::T::Dict) return std::nullopt;
    auto* disp = v.get("displays");
    if (!disp || disp->t != Value::T::Array || disp->a.empty()) return std::nullopt;
    const Value& d0 = disp->a[0];
    InfoReply r;
    auto w = intField(d0, "widthPixels"), h = intField(d0, "heightPixels");
    if (!w || !h || *w == 0 || *h == 0) return std::nullopt;
    r.width = *w; r.height = *h;
    if (auto* u = d0.get("uuid"); u && u->t == Value::T::Str) r.displayUuid = u->s;
    if (auto* k = v.get("keepAliveSendStatsAsBody")) if (auto b = k->asBool()) r.keepAliveStats = *b;
    // modes layout UNVERIFIED: resources[] entries with resourceID 1, transferType 1, takeConstraint 100.
    if (auto* m = v.get("modes"); m && m->t == Value::T::Dict)
        if (auto* rs = m->get("resources"); rs && rs->t == Value::T::Array)
            for (const auto& e : rs->a) {
                auto id = e.get("resourceID"), tt = e.get("transferType"), tc = e.get("takeConstraint");
                if (id && tt && tc && id->asInt() == 1 && tt->asInt() == 1 && tc->asInt() == 100) r.mainScreenTake100 = true;
            }
    return r;
}

std::optional<SetupReply> parseSetupReply(const Value& v) {
    if (v.t != Value::T::Dict) return std::nullopt;
    SetupReply r;
    if (auto p = intField(v, "eventPort")) r.eventPort = *p;
    if (auto p = intField(v, "keepAlivePort")) r.keepAlivePort = *p;
    if (auto p = intField(v, "timingPort")) r.timingPort = *p;
    if (auto* s = v.get("streams"); s && s->t == Value::T::Array)
        for (const auto& e : s->a) {
            auto t = e.get("type");
            if (!t || !t->asInt()) continue;
            if (*t->asInt() == 110) { if (auto p = intField(e, "dataPort")) r.screenDataPort = *p; }
            else if (*t->asInt() == 100) {
                if (auto p = intField(e, "dataPort")) r.audioDataPort = *p;
                if (auto p = intField(e, "controlPort")) r.audioControlPort = *p;
            }
        }
    if (r.eventPort == 0 && r.screenDataPort == 0 && r.audioDataPort == 0) return std::nullopt;
    return r;
}

// ---------------- frames ----------------
std::array<uint8_t, kFrameHeaderLen> frameHeader(Op op, uint32_t body, uint64_t ts, float w, float h) {
    std::array<uint8_t, kFrameHeaderLen> o{};
    for (int k = 0; k < 4; ++k) o[k] = static_cast<uint8_t>(body >> (8 * k));
    o[4] = static_cast<uint8_t>(op);
    auto putf = [&](size_t off, float f) { uint32_t b; std::memcpy(&b, &f, 4); for (int k = 0; k < 4; ++k) o[off + k] = static_cast<uint8_t>(b >> (8 * k)); };
    if (op == Op::VideoConfig) { o[6] |= 0x04; putf(16, w); putf(20, h); }
    if (op == Op::VideoFrame) for (int k = 0; k < 8; ++k) o[8 + k] = static_cast<uint8_t>(ts >> (8 * k));
    if (op == Op::KeepAliveStats) putf(124, 42.0f);
    return o;
}

std::vector<uint8_t> buildAvcC(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps) {
    if (sps.size() < 4 || pps.empty() || sps.size() > 0xFFFF || pps.size() > 0xFFFF) return {};
    std::vector<uint8_t> o{0x01, sps[1], sps[2], sps[3], 0xFF, 0xE1,
                           static_cast<uint8_t>(sps.size() >> 8), static_cast<uint8_t>(sps.size())};
    o.insert(o.end(), sps.begin(), sps.end());
    o.push_back(0x01);
    o.push_back(static_cast<uint8_t>(pps.size() >> 8)); o.push_back(static_cast<uint8_t>(pps.size()));
    o.insert(o.end(), pps.begin(), pps.end());
    return o;
}

std::vector<std::vector<uint8_t>> splitAnnexB(const uint8_t* d, size_t n) {
    std::vector<std::vector<uint8_t>> nals;
    size_t i = 0, start = std::string::npos;
    while (i + 2 < n) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            if (start != std::string::npos) {
                size_t end = i;
                while (end > start && d[end - 1] == 0) --end;   // trailing zero of a 4-byte start code
                if (end > start) nals.emplace_back(d + start, d + end);
            }
            start = i + 3; i += 3;
        } else ++i;
    }
    if (start != std::string::npos && start < n) nals.emplace_back(d + start, d + n);
    return nals;
}

AccessUnit toAvcc(const uint8_t* d, size_t n) {
    AccessUnit au;
    for (auto& nal : splitAnnexB(d, n)) {
        int type = nal[0] & 0x1F;
        if (type == 9) continue;
        if (type == 5) au.idr = true;
        if (type == 7) au.sps = nal;
        if (type == 8) au.pps = nal;
        uint32_t l = static_cast<uint32_t>(nal.size());
        for (int k = 3; k >= 0; --k) au.avcc.push_back(static_cast<uint8_t>(l >> (8 * k)));
        au.avcc.insert(au.avcc.end(), nal.begin(), nal.end());
    }
    return au;
}

std::vector<uint8_t> keepAliveBody() { return bplist::write(Value::Dict()); }

// ---------------- cipher ----------------
static std::array<uint8_t, 16> derive(const char* label, const std::string& id) {
    std::string in = std::string(label) + id + std::string(16, '\0');
    unsigned char md[64]; unsigned int mdl = 0;
    std::array<uint8_t, 16> out{};
    if (EVP_Digest(in.data(), in.size(), md, &mdl, EVP_sha512(), nullptr) == 1) std::memcpy(out.data(), md, 16);
    return out;
}

StreamCipher::StreamCipher(const std::string& id) {
    key_ = derive("AirPlayStreamKey", id);
    iv_ = derive("AirPlayStreamIV", id);
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    if (c && EVP_EncryptInit_ex(c, EVP_aes_128_ctr(), nullptr, key_.data(), iv_.data()) == 1) ctx_ = c;
    else if (c) EVP_CIPHER_CTX_free(c);
}
StreamCipher::~StreamCipher() { if (ctx_) EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(ctx_)); }

std::vector<uint8_t> StreamCipher::process(const uint8_t* d, size_t n) {
    std::vector<uint8_t> out(n);
    if (!ctx_ || n == 0 || n > 0x7FFFFFFF) return n == 0 ? out : std::vector<uint8_t>{};
    int outl = 0;
    if (EVP_EncryptUpdate(static_cast<EVP_CIPHER_CTX*>(ctx_), out.data(), &outl, d, static_cast<int>(n)) != 1 ||
        static_cast<size_t>(outl) != n) return {};
    return out;
}

// ---------------- audio ----------------
std::vector<uint8_t> AudioPacketizer::packet(const uint8_t* pcm) {
    size_t bytes = kRtpFramesPerPacket * 2 * static_cast<size_t>(ch_);
    std::vector<uint8_t> p(12 + bytes);
    p[0] = 0x80; p[1] = 100;
    p[2] = static_cast<uint8_t>(seq_ >> 8); p[3] = static_cast<uint8_t>(seq_);
    for (int k = 0; k < 4; ++k) p[4 + k] = static_cast<uint8_t>(ts_ >> (8 * (3 - k)));
    // SSRC bytes 8..11 stay 0
    if (pcm) for (size_t i = 0; i < bytes; i += 2) { p[12 + i] = pcm[i + 1]; p[12 + i + 1] = pcm[i]; }  // LE -> BE
    ++seq_; ts_ += static_cast<uint32_t>(kRtpFramesPerPacket);
    return p;
}

std::vector<std::vector<uint8_t>> AudioPacketizer::feed(const uint8_t* d, size_t n) {
    pend_.insert(pend_.end(), d, d + n);
    size_t bytes = kRtpFramesPerPacket * 2 * static_cast<size_t>(ch_), pos = 0;
    std::vector<std::vector<uint8_t>> out;
    while (pend_.size() - pos >= bytes) { out.push_back(packet(pend_.data() + pos)); pos += bytes; }
    pend_.erase(pend_.begin(), pend_.begin() + static_cast<std::ptrdiff_t>(pos));
    return out;
}

std::vector<uint8_t> AudioPacketizer::silence() { return packet(nullptr); }

// ---------------- timing ----------------
uint64_t ntpFromUnixMicros(uint64_t us) {
    uint64_t sec = us / 1000000 + 2208988800ULL;
    uint64_t frac = ((us % 1000000) << 32) / 1000000;
    return (sec << 32) | frac;
}

std::vector<uint8_t> timingReply(const uint8_t* req, size_t n, uint64_t now) {
    if (n < 32 || req[0] != 0x80 || req[1] != 0xD2) return {};
    std::vector<uint8_t> o(32, 0);
    o[0] = 0x80; o[1] = 0xD3; o[2] = 0x00; o[3] = 0x07;
    std::memcpy(&o[8], &req[24], 8);                         // t1 = request origin (last stamp)
    for (int k = 0; k < 8; ++k) { o[16 + k] = static_cast<uint8_t>(now >> (8 * (7 - k))); o[24 + k] = o[16 + k]; }
    return o;
}

// ---------------- input ----------------
std::optional<TouchEvent> TouchTracker::onReport(const uint8_t* r, size_t n) {
    if (n != 5) return std::nullopt;
    bool tip = r[0] & 1;
    int rx = r[1] | (r[2] << 8), ry = r[3] | (r[4] << 8);
    rx = std::min(rx, 32767); ry = std::min(ry, 32767);
    int x = static_cast<int>(static_cast<int64_t>(rx) * (w_ - 1) / 32767);
    int y = static_cast<int>(static_cast<int64_t>(ry) * (h_ - 1) / 32767);
    TouchEvent e{TouchEvent::Kind::Move, x, y};
    if (!down_ && tip) { down_ = true; e.kind = TouchEvent::Kind::Down; return e; }
    if (down_ && tip) return e;
    if (down_ && !tip) { down_ = false; e.kind = TouchEvent::Kind::Up; return e; }
    return std::nullopt;
}

}  // namespace s10::airplay
