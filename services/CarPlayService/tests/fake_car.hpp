// Fake CarPlay head unit with real sockets and its own poll loop (see fake_receiver.hpp for the protocol logic).
// Shared by test_loopback_session (127.0.0.1, in-process thread) and fake_car_server (separate process).
#pragma once
#include "fake_receiver.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <fstream>
#include <iterator>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static bool g_bindAny = false;   // fake_car_server: listen on all interfaces so an emulator can reach it via 10.0.2.2
static int listenOn(int type, int& port) {
    int fd = ::socket(AF_INET, type, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(g_bindAny ? INADDR_ANY : INADDR_LOOPBACK); a.sin_port = 0;
    ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    if (type == SOCK_STREAM) ::listen(fd, 4);
    socklen_t l = sizeof a; ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &l);
    port = ntohs(a.sin_port);
    return fd;
}
static std::vector<uint8_t> readFile(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void sendAll(int fd, const std::vector<uint8_t>& b) { size_t o = 0; while (o < b.size()) { ssize_t n = ::send(fd, b.data() + o, b.size() - o, MSG_NOSIGNAL); if (n <= 0) return; o += size_t(n); } }

struct FakeCar {
    FakeReceiver rx;
    int ctlL, evtL, scrL, audU, kaU, timU;
    int ctlPort, evtPort, scrPort, audPort, kaPort, timPort;
    std::atomic<bool> stop{false};
    // results (read after join)
    int audioPkts = 0, keepAlives = 0, timingReplies = 0;
    std::vector<RtspMessage> eventReplies;
    bool scriptSent = false;

    FakeCar() {
        ctlL = listenOn(SOCK_STREAM, ctlPort); evtL = listenOn(SOCK_STREAM, evtPort); scrL = listenOn(SOCK_STREAM, scrPort);
        audU = listenOn(SOCK_DGRAM, audPort); kaU = listenOn(SOCK_DGRAM, kaPort); timU = listenOn(SOCK_DGRAM, timPort);
        rx.eventPort = evtPort; rx.screenPort = scrPort; rx.audioPort = audPort; rx.keepAlivePort = kaPort;
    }
    ~FakeCar() { for (int fd : {ctlL, evtL, scrL, audU, kaU, timU}) ::close(fd); }

    static std::vector<uint8_t> cmd(const Value& body, int cseq) { return buildRequest("POST", "/command", cseq, s10::bplist::write(body)); }

    void run() {
        int ctl = -1, evt = -1, scr = -1;
        RtspParser evParser;
        uint8_t buf[65536];
        while (!stop) {
            pollfd p[8]; int n = 0;
            auto add = [&](int fd) { p[n] = {fd, POLLIN, 0}; return n++; };
            int iCtlL = ctl < 0 ? add(ctlL) : -1, iCtl = ctl >= 0 ? add(ctl) : -1;
            int iEvtL = evt < 0 ? add(evtL) : -1, iEvt = evt >= 0 ? add(evt) : -1;
            int iScrL = scr < 0 ? add(scrL) : -1, iScr = scr >= 0 ? add(scr) : -1;
            int iAud = add(audU), iKa = add(kaU), iTim = add(timU);
            if (::poll(p, static_cast<nfds_t>(n), 20) <= 0) continue;
            auto ready = [&](int i) { return i >= 0 && (p[i].revents & (POLLIN | POLLHUP)); };
            if (ready(iCtlL)) ctl = ::accept(ctlL, nullptr, nullptr);
            if (ready(iEvtL)) evt = ::accept(evtL, nullptr, nullptr);
            if (ready(iScrL)) scr = ::accept(scrL, nullptr, nullptr);
            if (ready(iCtl)) {
                ssize_t k = ::recv(ctl, buf, sizeof buf, 0);
                if (k <= 0) { ::close(ctl); ctl = -1; } else { auto rep = rx.onControl(buf, size_t(k)); if (!rep.empty()) sendAll(ctl, rep); }
            }
            if (ready(iEvt)) {
                ssize_t k = ::recv(evt, buf, sizeof buf, 0);
                if (k <= 0) { ::close(evt); evt = -1; } else for (auto& m : evParser.feed(buf, size_t(k))) eventReplies.push_back(std::move(m));
            }
            if (ready(iScr)) {
                ssize_t k = ::recv(scr, buf, sizeof buf, 0);
                if (k <= 0) { ::close(scr); scr = -1; } else rx.onScreen(std::vector<uint8_t>(buf, buf + k));
            }
            if (ready(iAud)) { ssize_t k = ::recv(audU, buf, sizeof buf, 0); if (k > 0) ++audioPkts; }
            if (ready(iKa)) { ssize_t k = ::recv(kaU, buf, sizeof buf, 0); if (k == 4) ++keepAlives; }
            if (ready(iTim)) {
                ssize_t k = ::recv(timU, buf, sizeof buf, 0);
                if (k == 32 && buf[1] == 0xD3) ++timingReplies;
            }
            // Once the screen channel is up: timing request, then a script on the event channel.
            if (scr >= 0 && evt >= 0 && !scriptSent && rx.clientTimingPort > 0) {
                scriptSent = true;
                std::vector<uint8_t> t(32, 0); t[0] = 0x80; t[1] = 0xD2; t[3] = 7; t[24] = 0x11;
                sockaddr_in to{}; to.sin_family = AF_INET; to.sin_addr.s_addr = htonl(INADDR_LOOPBACK); to.sin_port = htons(uint16_t(rx.clientTimingPort));
                ::sendto(timU, t.data(), t.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
                Value down = Value::Dict(); down.set("type", Value::Str("hidSendReport")).set("uuid", Value::Str("T")).set("hidReport", Value::Data({1, 0xFF, 0x7F, 0xFF, 0x7F}));
                Value up = Value::Dict(); up.set("type", Value::Str("hidSendReport")).set("uuid", Value::Str("T")).set("hidReport", Value::Data({0, 0xFF, 0x7F, 0xFF, 0x7F}));
                Value fk = Value::Dict(); fk.set("type", Value::Str("forceKeyFrame"));
                Value res = Value::Dict(); res.set("resourceID", Value::Int(2)).set("transferType", Value::Int(3));
                Value pr = Value::Dict(); pr.set("resources", Value::Array().push(res));
                Value cm = Value::Dict(); cm.set("type", Value::Str("changeModes")).set("params", pr);
                sendAll(evt, cmd(down, 1)); sendAll(evt, cmd(up, 2)); sendAll(evt, cmd(fk, 3)); sendAll(evt, cmd(cm, 4));
            }
        }
        for (int fd : {ctl, evt, scr}) if (fd >= 0) ::close(fd);
    }
};

