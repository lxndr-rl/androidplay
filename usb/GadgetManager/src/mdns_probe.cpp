// mdns_probe: send mDNS PTR queries on an interface (IPv6 link-local, ff02::fb:5353) and print every answer.
// Hardware probe for the CarPlay NCM link. usage: mdns_probe IFACE SECONDS [service ...]
// STATUS: prints what the radio answers; nothing is interpreted beyond name strings.
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <net/if.h>
#include <netinet/in.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <vector>

static double now() { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static std::vector<uint8_t> query(const std::string& name) {
    std::vector<uint8_t> q = {0,0, 0,0, 0,1, 0,0, 0,0, 0,0};
    size_t i = 0;
    while (i < name.size()) {
        size_t j = name.find('.', i); if (j == std::string::npos) j = name.size();
        q.push_back((uint8_t)(j - i)); q.insert(q.end(), name.begin() + i, name.begin() + j); i = j + 1;
    }
    q.push_back(0); q.insert(q.end(), {0,12, 0x80,1});  // PTR, class IN with the unicast-response bit
    return q;
}
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: mdns_probe IFACE SECONDS [service...]\n"); return 2; }
    const char* ifn = argv[1]; int secs = atoi(argv[2]);
    std::vector<std::string> svcs;
    for (int i = 3; i < argc; i++) svcs.push_back(argv[i]);
    if (svcs.empty()) svcs = {"_airplay._tcp.local", "_carplay-ctrl._tcp.local", "_raop._tcp.local", "_services._dns-sd._udp.local"};
    unsigned idx = if_nametoindex(ifn);
    if (!idx) { fprintf(stderr, "no iface %s\n", ifn); return 1; }
    int s = socket(AF_INET6, SOCK_DGRAM, 0);
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_port = htons(5353); a.sin6_addr = in6addr_any;
    if (bind(s, (sockaddr*)&a, sizeof a) != 0) { perror("bind 5353"); }
    struct ipv6_mreq m{}; inet_pton(AF_INET6, "ff02::fb", &m.ipv6mr_multiaddr); m.ipv6mr_interface = idx;
    if (setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP, &m, sizeof m) != 0) perror("join ff02::fb");
    setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &idx, sizeof idx);
    int hops = 255; setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof hops);
    struct sockaddr_in6 dst{}; dst.sin6_family = AF_INET6; dst.sin6_port = htons(5353); inet_pton(AF_INET6, "ff02::fb", &dst.sin6_addr); dst.sin6_scope_id = idx;
    double t0 = now(), nextq = 0; int answers = 0;
    while (now() - t0 < secs) {
        if (now() >= nextq) {
            for (auto& n : svcs) { auto q = query(n); ssize_t w = sendto(s, q.data(), q.size(), 0, (sockaddr*)&dst, sizeof dst); printf("%.2f QUERY %s sent=%zd\n", now() - t0, n.c_str(), w); }
            fflush(stdout); nextq = now() + 2.0;
        }
        fd_set fs; FD_ZERO(&fs); FD_SET(s, &fs); struct timeval tv = {0, 300000};
        if (select(s + 1, &fs, nullptr, nullptr, &tv) > 0) {
            uint8_t buf[2048]; struct sockaddr_in6 from{}; socklen_t fl = sizeof from;
            ssize_t n = recvfrom(s, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
            if (n <= 0) continue;
            char ip[INET6_ADDRSTRLEN]; inet_ntop(AF_INET6, &from.sin6_addr, ip, sizeof ip);
            bool resp = n >= 3 && (buf[2] & 0x80);
            printf("%.2f RX from %s len=%zd %s text='", now() - t0, ip, n, resp ? "RESPONSE" : "query");
            for (ssize_t i = 12; i < n; i++) putchar(buf[i] >= 32 && buf[i] < 127 ? buf[i] : '.');
            printf("'\n"); fflush(stdout); if (resp) answers++;
        }
    }
    printf("DONE responses=%d\n", answers);
    return 0;
}
