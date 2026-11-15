// carplay_phone: phone-side CarPlay session tool for hardware bring-up, run on the phone while it is USB host of the
// radio and eth0 (CDC NCM) is up. Steps: mDNS-discover the radio's _airplay._tcp over IFACE (IPv6 link-local),
// `GET /info` on a throwaway connection (logged), then the AirPlay Client sequence (CP-007) through SessionRunner
// (IPv6), then stream a looped H.264 fixture and a tone (synthetic, NOT the phone screen yet).
// usage: carplay_phone IFACE SECONDS H264_FIXTURE PCM_FIXTURE
// STATUS: first run against a real radio; nothing is verified until a log says so.
#include "session_runner.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <net/if.h>
#include <netinet/in.h>
#include <string>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <vector>
using namespace s10::airplay;
typedef std::vector<uint8_t> B;

static double mono() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static double T0;
#define LOG(...) do { printf("%.3f ", mono() - T0); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)
static void dumpText(const char* tag, const uint8_t* d, size_t n) {
    printf("%.3f %s len=%zu text='", mono() - T0, tag, n);
    for (size_t i = 0; i < n && i < 600; i++) putchar(d[i] >= 32 && d[i] < 127 ? d[i] : (d[i] == '\n' ? '|' : '.'));
    printf("'%s\n", n > 600 ? " ..." : ""); fflush(stdout);
}
static B readFile(const char* p) { std::ifstream f(p, std::ios::binary); return B((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static uint16_t rd16(const B& b, size_t i) { return i + 1 < b.size() ? (b[i] << 8 | b[i + 1]) : 0; }
static std::string nameAt(const B& b, size_t& i) {
    std::string out; size_t pos = i; bool jumped = false; size_t endpos = 0;
    for (int g = 0; g < 64 && pos < b.size(); g++) {
        uint8_t l = b[pos];
        if (l == 0) { pos++; break; }
        if ((l & 0xC0) == 0xC0) { if (pos + 1 >= b.size()) break; size_t p = ((l & 0x3F) << 8) | b[pos + 1]; if (!jumped) endpos = pos + 2; jumped = true; pos = p; continue; }
        pos++; if (pos + l > b.size()) break; out.append((const char*)&b[pos], l); out += '.'; pos += l;
    }
    i = jumped ? endpos : pos; return out;
}
// returns SRV port (0 if none) and the sender address of the response that carried it
static int discover(const char* ifn, double secs, sockaddr_in6& radio) {
    unsigned idx = if_nametoindex(ifn); if (!idx) return 0;
    int s = socket(AF_INET6, SOCK_DGRAM, 0); int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_port = htons(5353); a.sin6_addr = in6addr_any; bind(s, (sockaddr*)&a, sizeof a);
    ipv6_mreq m{}; inet_pton(AF_INET6, "ff02::fb", &m.ipv6mr_multiaddr); m.ipv6mr_interface = idx; setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP, &m, sizeof m);
    setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &idx, sizeof idx); int hops = 255; setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof hops);
    sockaddr_in6 dst{}; dst.sin6_family = AF_INET6; dst.sin6_port = htons(5353); inet_pton(AF_INET6, "ff02::fb", &dst.sin6_addr); dst.sin6_scope_id = idx;
    B q = {0,0, 0,0, 0,1, 0,0, 0,0, 0,0}; const char* nm = "_airplay._tcp.local"; std::string n = nm; size_t i = 0;
    while (i < n.size()) { size_t j = n.find('.', i); if (j == std::string::npos) j = n.size(); q.push_back((uint8_t)(j - i)); q.insert(q.end(), n.begin() + i, n.begin() + j); i = j + 1; }
    q.push_back(0); q.insert(q.end(), {0,12, 0x80,1});
    double end = mono() + secs, nextq = 0; int port = 0;
    while (mono() < end && !port) {
        if (mono() >= nextq) { sendto(s, q.data(), q.size(), 0, (sockaddr*)&dst, sizeof dst); nextq = mono() + 2; }
        fd_set f; FD_ZERO(&f); FD_SET(s, &f); timeval tv = {0, 300000};
        if (select(s + 1, &f, 0, 0, &tv) > 0) {
            uint8_t buf[2048]; sockaddr_in6 from{}; socklen_t fl = sizeof from; ssize_t k = recvfrom(s, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
            if (k < 12 || !(buf[2] & 0x80)) continue;
            B b(buf, buf + k); size_t p = 12; int qd = rd16(b, 4), an = rd16(b, 6) + rd16(b, 8) + rd16(b, 10);
            for (int x = 0; x < qd; x++) { nameAt(b, p); p += 4; }
            for (int x = 0; x < an && p + 10 <= b.size(); x++) {
                std::string rn = nameAt(b, p); uint16_t type = rd16(b, p), len = rd16(b, p + 8); size_t off = p + 10; p += 10 + len;
                if (type == 33 && len >= 6) { port = rd16(b, off + 4); radio = from; LOG("mDNS SRV %s port=%d", rn.c_str(), port); }
                else if (type == 16) { std::string t; size_t o = off; while (o < off + len) { uint8_t l = b[o++]; t += "[" + std::string((const char*)&b[o], l) + "]"; o += l; } LOG("mDNS TXT %s %s", rn.c_str(), t.c_str()); }
            }
        }
    }
    close(s); return port;
}
// Live video from the projection app: TCP 127.0.0.1:PORT, frames [len u32 BE][Annex-B AU]. 'K' goes back on a key-frame request.
struct Live {
    int lis = -1, cli = -1; B buf; size_t aus = 0;
    bool open(int port) {
        lis = socket(AF_INET, SOCK_STREAM, 0); int one = 1; setsockopt(lis, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(lis, (sockaddr*)&a, sizeof a) != 0 || listen(lis, 1) != 0) { LOG("live listen failed errno=%d", errno); return false; }
        fcntl(lis, F_SETFL, O_NONBLOCK); LOG("live video listener on 127.0.0.1:%d", port); return true;
    }
    // returns complete access units that arrived
    std::vector<B> poll() {
        std::vector<B> out;
        if (cli < 0 && lis >= 0) { int c = accept(lis, nullptr, nullptr); if (c >= 0) { cli = c; fcntl(cli, F_SETFL, O_NONBLOCK); LOG("live video source connected"); } }
        if (cli >= 0) {
            uint8_t tmp[65536]; for (;;) { ssize_t n = recv(cli, tmp, sizeof tmp, 0); if (n > 0) buf.insert(buf.end(), tmp, tmp + n); else if (n == 0) { LOG("live video source closed"); close(cli); cli = -1; break; } else break; }
            while (buf.size() >= 4) { uint32_t l = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]; if (l > (8u << 20)) { buf.clear(); break; } if (buf.size() < 4 + (size_t)l) break; out.emplace_back(buf.begin() + 4, buf.begin() + 4 + l); buf.erase(buf.begin(), buf.begin() + 4 + l); aus++; }
        }
        return out;
    }
    void keyframe() { if (cli >= 0) { char k = 'K'; send(cli, &k, 1, MSG_NOSIGNAL); } }
    bool active() const { return cli >= 0; }
};

static std::string randHex(int n) { static const char* h = "0123456789ABCDEF"; std::string s; FILE* f = fopen("/dev/urandom", "rb"); for (int i = 0; i < n; i++) { unsigned char c = 0; if (f && fread(&c, 1, 1, f) != 1) c = 0; s += h[c & 15]; } if (f) fclose(f); return s; }
static std::string uuid() { return randHex(8) + "-" + randHex(4) + "-" + randHex(4) + "-" + randHex(4) + "-" + randHex(12); }

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: carplay_phone IFACE SECONDS H264 PCM [LIVE_PORT]\n"); return 2; }
    int livePort = argc > 5 ? atoi(argv[5]) : 0;
    T0 = mono();
    const char* ifn = argv[1]; double secs = atof(argv[2]);
    B h264 = readFile(argv[3]), pcm = readFile(argv[4]);
    std::vector<B> aus; { B cur; for (auto& n : splitAnnexB(h264.data(), h264.size())) { cur.insert(cur.end(), {0,0,0,1}); cur.insert(cur.end(), n.begin(), n.end()); int t = n[0] & 0x1F; if (t == 1 || t == 5) { aus.push_back(cur); cur.clear(); } } }
    LOG("fixtures: %zu access units, %zu pcm bytes", aus.size(), pcm.size());
    Live live; if (livePort) live.open(livePort);
    sockaddr_in6 radio{}; int port = discover(ifn, 60, radio);
    if (!port) { LOG("no AirPlay service found"); return 1; }
    char ip[64]; inet_ntop(AF_INET6, &radio.sin6_addr, ip, sizeof ip);
    std::string host = std::string(ip) + "%" + ifn; LOG("radio %s port %d", host.c_str(), port);

    {   // one throwaway GET /info so the radio's raw answer is on record
        int c = socket(AF_INET6, SOCK_STREAM, 0); sockaddr_in6 t = radio; t.sin6_port = htons(port); t.sin6_scope_id = if_nametoindex(ifn);
        timeval to = {4, 0}; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
        if (connect(c, (sockaddr*)&t, sizeof t) == 0) {
            const char* rq = "GET /info RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: AirPlay/280.3\r\n\r\n"; send(c, rq, strlen(rq), 0);
            uint8_t rb[32768]; ssize_t tot = 0, n; while ((n = recv(c, rb + tot, sizeof rb - tot, 0)) > 0) { tot += n; if (tot >= (ssize_t)sizeof rb) break; }
            LOG("GET /info -> %zd bytes", tot); dumpText("info-reply", rb, tot);
            FILE* f = fopen("/data/local/tmp/info_reply.bin", "wb"); if (f) { fwrite(rb, 1, tot, f); fclose(f); }
        } else LOG("GET /info connect failed errno=%d (%s)", errno, strerror(errno));
        close(c);
    }

    SessionRunner runner;
    FILE* raw = fopen("/data/local/tmp/session_raw.bin", "ab");   // every control/event byte, tagged: [tag(4) len(4) bytes]
    runner.trace = [raw](const char* ch, const uint8_t* d, size_t n) {
        dumpText(ch, d, n);
        if (raw) { char tag[5] = {0}; strncpy(tag, ch, 4); uint32_t l = (uint32_t)n; fwrite(tag, 1, 4, raw); fwrite(&l, 4, 1, raw); fwrite(d, 1, n, raw); fflush(raw); }
    };
    int tp = runner.bindTiming(true);
    ClientConfig cfg; cfg.uuid = uuid(); cfg.correlationUuid = uuid(); cfg.timingPort = tp;
    cfg.deviceId = "02:" + randHex(2) + ":" + randHex(2) + ":" + randHex(2) + ":" + randHex(2) + ":" + randHex(2);
    cfg.randomConnId = [] { FILE* f = fopen("/dev/urandom", "rb"); uint64_t v = 0; if (f) { size_t r = fread(&v, 8, 1, f); (void)r; fclose(f); } return int64_t((v >> 1) | 1); };
    cfg.nowNtp = [] { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ntpFromUnixMicros((uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000); };
    ClientCallbacks cb; cb.onTouch = [](const TouchEvent& e) { if (e.kind != TouchEvent::Kind::Move) LOG("TOUCH kind=%d x=%d y=%d", (int)e.kind, e.x, e.y); };
    cb.onKeyFrameRequested = [&live] { LOG("radio requested a key frame"); live.keyframe(); };
    cb.onHid = [](const std::string& u, const std::vector<uint8_t>& r) { char h[64] = {0}; for (size_t i = 0; i < r.size() && i < 20; i++) snprintf(h + i * 3, 4, "%02x ", r[i]); LOG("HID report uuid=%s len=%zu bytes=%s", u.c_str(), r.size(), h); };
    Client client(cfg, cb);
    LOG("starting AirPlay client, timingPort=%d deviceId=%s", tp, cfg.deviceId.c_str());
    if (!runner.start(client, host, port)) { LOG("start failed: %s", runner.error().c_str()); return 1; }
    double end = T0 + secs; Client::State last = client.state(); size_t ai = 0, pi = 0; double nextV = 0, nextA = 0; size_t sentAu = 0;
    auto nowMs = [] { return (uint64_t)(mono() * 1000); };
    while (mono() < end && !runner.failed()) {
        runner.step(5, nowMs());
        if (client.state() != last) { last = client.state(); LOG("client state=%d failure='%s'", (int)last, client.failure().c_str()); }
        auto liveAus = live.poll();   // always drain, so frames sent before streaming starts do not pile up
        if (client.state() == Client::State::Streaming) {
            double t = mono();
            for (auto& au : liveAus) { runner.pushVideo(au.data(), au.size()); sentAu++; }
            if (!live.active() && t >= nextV && !aus.empty()) { runner.pushVideo(aus[ai % aus.size()].data(), aus[ai % aus.size()].size()); ai++; sentAu++; nextV = t + 0.033; }
            if (t >= nextA && pcm.size() >= 1920) { const size_t c = 480 * 4; if (pi + c > pcm.size()) pi = 0; runner.pushAudio(pcm.data() + pi, c); pi += c; nextA = t + 0.010; }
        }
    }
    if (runner.failed()) LOG("runner failed: %s", runner.error().c_str());
    if (client.info()) LOG("radio /info parsed: %dx%d displayUuid=%s", client.info()->width, client.info()->height, client.info()->displayUuid.c_str());
    LOG("DONE state=%d access_units_sent=%zu live_aus=%zu", (int)client.state(), sentAu, live.aus);
    return 0;
}
