// airplay_probe: find the radio's AirPlay service over the NCM link (mDNS on IFACE), connect to its RTSP port on the
// IPv6 link-local address and send `GET /info`. Prints everything. Hardware probe; not the client.
// usage: airplay_probe IFACE SECONDS
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <vector>

static double now() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
typedef std::vector<uint8_t> B;
static uint16_t rd16(const B& b, size_t i) { return i + 1 < b.size() ? (b[i] << 8 | b[i + 1]) : 0; }
static std::string name_at(const B& b, size_t& i, int depth = 0) {  // advances i past the in-place name
    std::string out; size_t pos = i; bool jumped = false; size_t endpos = 0;
    for (int guard = 0; guard < 64 && pos < b.size(); guard++) {
        uint8_t l = b[pos];
        if (l == 0) { pos++; break; }
        if ((l & 0xC0) == 0xC0) { if (pos + 1 >= b.size()) break; size_t p = ((l & 0x3F) << 8) | b[pos + 1]; if (!jumped) endpos = pos + 2; jumped = true; pos = p; continue; }
        pos++; if (pos + l > b.size()) break;
        out.append((const char*)&b[pos], l); out += '.'; pos += l;
    }
    i = jumped ? endpos : pos; (void)depth; return out;
}
struct Rec { std::string name; uint16_t type, rdlen; size_t rdoff; };
static std::vector<Rec> records(const B& b) {
    std::vector<Rec> r; if (b.size() < 12) return r;
    size_t i = 12; int qd = rd16(b, 4), an = rd16(b, 6) + rd16(b, 8) + rd16(b, 10);
    for (int k = 0; k < qd; k++) { name_at(b, i); i += 4; }
    for (int k = 0; k < an && i + 10 <= b.size(); k++) {
        Rec x; x.name = name_at(b, i); x.type = rd16(b, i); x.rdlen = rd16(b, i + 8); x.rdoff = i + 10; i += 10 + x.rdlen; r.push_back(x);
    }
    return r;
}
static B query(const std::string& name) {
    B q = {0,0, 0,0, 0,1, 0,0, 0,0, 0,0}; size_t i = 0;
    while (i < name.size()) { size_t j = name.find('.', i); if (j == std::string::npos) j = name.size(); q.push_back((uint8_t)(j - i)); q.insert(q.end(), name.begin() + i, name.begin() + j); i = j + 1; }
    q.push_back(0); q.insert(q.end(), {0,12, 0x80,1}); return q;
}
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    unsigned idx = if_nametoindex(argv[1]); int secs = atoi(argv[2]); if (!idx) { fprintf(stderr, "no iface\n"); return 1; }
    int s = socket(AF_INET6, SOCK_DGRAM, 0); int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_port = htons(5353); a.sin6_addr = in6addr_any; bind(s, (sockaddr*)&a, sizeof a);
    ipv6_mreq m{}; inet_pton(AF_INET6, "ff02::fb", &m.ipv6mr_multiaddr); m.ipv6mr_interface = idx; setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP, &m, sizeof m);
    setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &idx, sizeof idx); int hops = 255; setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof hops);
    sockaddr_in6 dst{}; dst.sin6_family = AF_INET6; dst.sin6_port = htons(5353); inet_pton(AF_INET6, "ff02::fb", &dst.sin6_addr); dst.sin6_scope_id = idx;
    double t0 = now(), nextq = 0; int port = 0; sockaddr_in6 radio{}; bool have = false;
    while (now() - t0 < secs && !have) {
        if (now() >= nextq) { B q = query("_airplay._tcp.local"); sendto(s, q.data(), q.size(), 0, (sockaddr*)&dst, sizeof dst); nextq = now() + 2; }
        fd_set f; FD_ZERO(&f); FD_SET(s, &f); timeval tv = {0, 300000};
        if (select(s + 1, &f, 0, 0, &tv) > 0) {
            uint8_t buf[2048]; sockaddr_in6 from{}; socklen_t fl = sizeof from; ssize_t n = recvfrom(s, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
            if (n < 12 || !(buf[2] & 0x80)) continue;
            B b(buf, buf + n);
            for (auto& r : records(b)) {
                char ip[64]; inet_ntop(AF_INET6, &from.sin6_addr, ip, sizeof ip);
                if (r.type == 33 && r.rdlen >= 6) { size_t o = r.rdoff + 4; std::string tgt = name_at(b, o); printf("%.2f SRV name=%s prio/weight=%u/%u port=%u target=%s from=%s\n", now() - t0, r.name.c_str(), rd16(b, r.rdoff), rd16(b, r.rdoff + 2), rd16(b, r.rdoff + 4), tgt.c_str(), ip); if (!port) { port = rd16(b, r.rdoff + 4); radio = from; radio.sin6_port = htons(port); radio.sin6_scope_id = idx; have = true; } }
                else if (r.type == 16) { printf("%.2f TXT name=%s:", now() - t0, r.name.c_str()); size_t o = r.rdoff; while (o < r.rdoff + r.rdlen) { uint8_t l = b[o++]; printf(" [%.*s]", l, (const char*)&b[o]); o += l; } printf("\n"); }
                else if (r.type == 28) { char a6[64]; inet_ntop(AF_INET6, &b[r.rdoff], a6, sizeof a6); printf("%.2f AAAA %s %s\n", now() - t0, r.name.c_str(), a6); }
                else if (r.type == 12) { size_t o = r.rdoff; printf("%.2f PTR %s -> %s\n", now() - t0, r.name.c_str(), name_at(b, o).c_str()); }
            }
            fflush(stdout);
        }
    }
    if (!have) { printf("NO SRV within %d s\n", secs); return 0; }
    char ip[64]; inet_ntop(AF_INET6, &radio.sin6_addr, ip, sizeof ip); printf("%.2f CONNECT [%s%%%s]:%d\n", now() - t0, ip, argv[1], port); fflush(stdout);
    int c = socket(AF_INET6, SOCK_STREAM, 0); timeval to = {5, 0}; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to); setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
    if (connect(c, (sockaddr*)&radio, sizeof radio) != 0) { printf("%.2f connect failed errno=%d (%s)\n", now() - t0, errno, strerror(errno)); return 0; }
    printf("%.2f CONNECTED\n", now() - t0);
    const char* reqs[] = {"GET /info RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: AirPlay/280.3\r\nX-Apple-ProtocolVersion: 1\r\n\r\n"};
    send(c, reqs[0], strlen(reqs[0]), 0); printf("%.2f SENT GET /info (RTSP/1.0)\n", now() - t0); fflush(stdout);
    uint8_t rb[65536]; ssize_t total = 0; double end = now() + 8;
    while (now() < end) { ssize_t n = recv(c, rb + total, sizeof rb - total, 0); if (n <= 0) break; total += n; if (total >= (ssize_t)sizeof rb) break; }
    printf("%.2f RECV %zd bytes\n", now() - t0, total);
    size_t hdr = 0; for (ssize_t i = 0; i + 3 < total; i++) if (!memcmp(rb + i, "\r\n\r\n", 4)) { hdr = i + 4; break; }
    printf("--- header (%zu bytes)\n", hdr); fwrite(rb, 1, hdr ? hdr : (size_t)total, stdout);
    printf("\n--- body hex (first 160)\n"); for (ssize_t i = hdr; i < total && i < (ssize_t)hdr + 160; i++) printf("%02x ", rb[i]); printf("\n");
    printf("--- body text\n"); for (ssize_t i = hdr; i < total; i++) putchar(rb[i] >= 32 && rb[i] < 127 ? rb[i] : '.'); printf("\nDONE\n");
    return 0;
}
