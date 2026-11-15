// SessionRunner over IPv6: dial a TCP listener on [::1], check the first control bytes, and check that a bad host
// and a bad scope are refused. The AirPlay peer here is only a socket: this does NOT test an AirPlay receiver.
#include "session_runner.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace s10::airplay;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
    int l = ::socket(AF_INET6, SOCK_STREAM, 0);
    sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_addr = in6addr_loopback; a.sin6_port = 0;
    if (::bind(l, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { std::printf("no IPv6 loopback here, skipping\n"); return 0; }
    socklen_t al = sizeof a; ::getsockname(l, reinterpret_cast<sockaddr*>(&a), &al); ::listen(l, 2);
    int port = ntohs(a.sin6_port);

    SessionRunner r;
    int tp = r.bindTiming(true);
    CHECK(tp > 0);
    ClientConfig cfg; cfg.uuid = "U1"; cfg.correlationUuid = "U2"; cfg.timingPort = tp;
    cfg.randomConnId = [] { return int64_t(1234567); };
    cfg.nowNtp = [] { return uint64_t(1) << 32; };
    Client c(cfg);
    CHECK(r.start(c, "::1", port));
    CHECK(!r.failed());
    int s = ::accept(l, nullptr, nullptr);
    CHECK(s >= 0);
    char buf[256] = {0}; ssize_t n = ::recv(s, buf, sizeof buf - 1, 0);
    CHECK(n > 4);
    std::printf("first control bytes: %.*s\n", (int)(n > 30 ? 30 : n), buf);
    CHECK(!std::strncmp(buf, "GET ", 4) || !std::strncmp(buf, "SETUP", 5) || !std::strncmp(buf, "POST", 4));
    ::close(s); ::close(l);

    SessionRunner bad; Client c2(cfg);
    CHECK(!bad.start(c2, "not-an-address", 7000));
    SessionRunner bad2; Client c3(cfg);
    CHECK(!bad2.start(c3, "fe80::1%nosuchif0", 7000));
    std::printf("test_runner_ipv6: %d failures\n", fails);
    return fails ? 1 : 0;
}
