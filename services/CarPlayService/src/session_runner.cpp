#include "session_runner.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace s10::airplay {

static void closeFd(int& fd) { if (fd >= 0) { ::close(fd); fd = -1; } }

SessionRunner::~SessionRunner() { closeAll(); }

void SessionRunner::closeAll() {
    closeFd(ctl_); closeFd(evt_); closeFd(scr_); closeFd(aud_); closeFd(udpOut_); closeFd(tim_); closeFd(mic_);
}

bool SessionRunner::fail(const std::string& why) {
    if (!failed_) { failed_ = true; error_ = why; }
    return false;
}

int SessionRunner::bindTiming(bool v6) {
    closeFd(tim_);
    if (v6) {
        tim_ = ::socket(AF_INET6, SOCK_DGRAM, 0);
        if (tim_ < 0) return -1;
        int off = 0; ::setsockopt(tim_, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
        sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_addr = in6addr_any; a.sin6_port = 0;
        socklen_t l = sizeof a;
        if (::bind(tim_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::getsockname(tim_, reinterpret_cast<sockaddr*>(&a), &l) != 0) { closeFd(tim_); return -1; }
        return ntohs(a.sin6_port);
    }
    tim_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (tim_ < 0) return -1;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = 0;
    socklen_t l = sizeof a;
    if (::bind(tim_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::getsockname(tim_, reinterpret_cast<sockaddr*>(&a), &l) != 0) { closeFd(tim_); return -1; }
    return ntohs(a.sin_port);
}

int SessionRunner::bindMic(bool v6) {
    closeFd(mic_);
    int fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int port = -1;
    if (v6) {
        int off = 0; ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
        sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_addr = in6addr_any; socklen_t l = sizeof a;
        if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &l) == 0) port = ntohs(a.sin6_port);
    } else {
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); socklen_t l = sizeof a;
        if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &l) == 0) port = ntohs(a.sin_port);
    }
    if (port < 0) { ::close(fd); return -1; }
    mic_ = fd;
    return port;
}

bool SessionRunner::setTarget(const std::string& host) {
    std::memset(&target_, 0, sizeof target_);
    if (host.find(':') == std::string::npos) {
        sockaddr_in a{}; a.sin_family = AF_INET;
        if (::inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) return false;
        std::memcpy(&target_, &a, sizeof a); targetLen_ = sizeof a; family_ = AF_INET; return true;
    }
    std::string addr = host; unsigned scope = 0;
    size_t pc = host.find('%');
    if (pc != std::string::npos) { addr = host.substr(0, pc); scope = ::if_nametoindex(host.c_str() + pc + 1); if (!scope) return false; }
    sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_scope_id = scope;
    if (::inet_pton(AF_INET6, addr.c_str(), &a.sin6_addr) != 1) return false;
    std::memcpy(&target_, &a, sizeof a); targetLen_ = sizeof a; family_ = AF_INET6; return true;
}

static void setPort(sockaddr_storage& s, int family, int port) {
    if (family == AF_INET) reinterpret_cast<sockaddr_in*>(&s)->sin_port = htons(static_cast<uint16_t>(port));
    else reinterpret_cast<sockaddr_in6*>(&s)->sin6_port = htons(static_cast<uint16_t>(port));
}

bool SessionRunner::dialTcp(int port, bool nodelay, int& fdOut) {
    int fd = ::socket(family_, SOCK_STREAM, 0);
    if (fd < 0) return fail("socket: " + std::string(std::strerror(errno)));
    sockaddr_storage a = target_; setPort(a, family_, port);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), targetLen_) != 0) { int e = errno; ::close(fd); return fail("connect :" + std::to_string(port) + ": " + std::strerror(e)); }
    if (nodelay) { int one = 1; ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one); }
    timeval tv{2, 0};   // a write blocked > 2 s means the car stalled (doc 6.4)
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    fdOut = fd;
    return true;
}

bool SessionRunner::sendAll(int fd, const std::vector<uint8_t>& b) {
    size_t off = 0;
    while (off < b.size()) {
        ssize_t n = ::send(fd, b.data() + off, b.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return fail(std::string("send: ") + (n == 0 ? "closed" : std::strerror(errno)));
        off += static_cast<size_t>(n);
    }
    return true;
}

bool SessionRunner::run(const std::vector<Action>& actions) {
    for (const auto& a : actions) {
        switch (a.kind) {
        case Action::Kind::SendControl: if (trace) trace("ctl-tx", a.bytes.data(), a.bytes.size()); if (!sendAll(ctl_, a.bytes)) return false; break;
        case Action::Kind::OpenEvent: if (!dialTcp(a.port, false, evt_)) return false; break;
        case Action::Kind::OpenScreen: if (!dialTcp(a.port, true, scr_)) return false; break;
        case Action::Kind::OpenAudio:
            audioPort_ = a.port;
            if (udpOut_ < 0 && (udpOut_ = ::socket(family_, SOCK_DGRAM, 0)) < 0) return fail("udp socket");
            break;
        case Action::Kind::OpenMic: break;   // our bound UDP socket already receives; nothing to dial
        case Action::Kind::SendEvent: if (trace) trace("evt-tx", a.bytes.data(), a.bytes.size()); if (evt_ < 0 || !sendAll(evt_, a.bytes)) return fail("event channel not open"); break;
        case Action::Kind::SendScreen: if (scr_ < 0 || !sendAll(scr_, a.bytes)) return fail("screen channel not open"); break;
        case Action::Kind::SendAudio:
        case Action::Kind::SendUdpKeepAlive: {
            if (udpOut_ < 0 && (udpOut_ = ::socket(family_, SOCK_DGRAM, 0)) < 0) return fail("udp socket");
            sockaddr_storage to = target_; setPort(to, family_, a.kind == Action::Kind::SendAudio ? audioPort_ : a.port);
            ::sendto(udpOut_, a.bytes.data(), a.bytes.size(), 0, reinterpret_cast<sockaddr*>(&to), targetLen_);   // UDP: best effort
            break;
        }
        case Action::Kind::Failed: return fail(a.note);
        }
    }
    return true;
}

bool SessionRunner::start(Client& client, const std::string& host, int controlPort) {
    client_ = &client; host_ = host;
    if (!setTarget(host)) return fail("bad host " + host);
    if (!dialTcp(controlPort, false, ctl_)) return false;
    return run(client.start());
}

bool SessionRunner::step(int timeoutMs, uint64_t nowMs) {
    if (failed_ || !client_) return false;
    pollfd p[4]; int n = 0, iCtl = -1, iEvt = -1, iTim = -1, iMic = -1;
    if (ctl_ >= 0) { iCtl = n; p[n++] = {ctl_, POLLIN, 0}; }
    if (evt_ >= 0) { iEvt = n; p[n++] = {evt_, POLLIN, 0}; }
    if (tim_ >= 0) { iTim = n; p[n++] = {tim_, POLLIN, 0}; }
    if (mic_ >= 0) { iMic = n; p[n++] = {mic_, POLLIN, 0}; }
    int r = ::poll(p, static_cast<nfds_t>(n), timeoutMs);
    if (r < 0 && errno != EINTR) return fail(std::string("poll: ") + std::strerror(errno));
    uint8_t buf[65536];
    if (r > 0) {
        if (iCtl >= 0 && (p[iCtl].revents & (POLLIN | POLLHUP))) {
            ssize_t k = ::recv(ctl_, buf, sizeof buf, 0);
            if (k <= 0) return fail("control channel closed by the car");
            if (trace) trace("ctl-rx", buf, static_cast<size_t>(k));
            if (!run(client_->onControlBytes(buf, static_cast<size_t>(k)))) return false;
        }
        if (iEvt >= 0 && (p[iEvt].revents & (POLLIN | POLLHUP))) {
            ssize_t k = ::recv(evt_, buf, sizeof buf, 0);
            if (k <= 0) return fail("event channel closed by the car");
            if (trace) trace("evt-rx", buf, static_cast<size_t>(k));
            if (!run(client_->onEventBytes(buf, static_cast<size_t>(k)))) return false;
        }
        if (iTim >= 0 && (p[iTim].revents & POLLIN)) {
            sockaddr_storage from{}; socklen_t fl = sizeof from;
            ssize_t k = ::recvfrom(tim_, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &fl);
            if (k > 0) {
                auto rep = client_->onTimingPacket(buf, static_cast<size_t>(k));
                if (!rep.empty()) ::sendto(tim_, rep.data(), rep.size(), 0, reinterpret_cast<sockaddr*>(&from), fl);
            }
        }
        if (iMic >= 0 && (p[iMic].revents & POLLIN)) {
            for (int burst = 0; burst < 32; ++burst) {       // drain what is queued; the radio sends ~50 packets/s
                ssize_t k = ::recv(mic_, buf, sizeof buf, MSG_DONTWAIT);
                if (k <= 0) break;
                if (trace && micSeen_++ < 5) trace("mic-rx", buf, static_cast<size_t>(std::min<ssize_t>(k, 64)));   // first packets, for the log
                if (!run(client_->onMicPacket(buf, static_cast<size_t>(k)))) return false;
            }
        }
    }
    return run(client_->tick(nowMs));
}

bool SessionRunner::pushVideo(const uint8_t* d, size_t n) { return !failed_ && client_ && run(client_->pushVideo(d, n)); }
bool SessionRunner::pushAudio(const uint8_t* d, size_t n) { return !failed_ && client_ && run(client_->pushAudioPcm(d, n)); }

}  // namespace s10::airplay
