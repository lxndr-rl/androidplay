// POSIX socket glue for the AirPlay Client: executes its Actions on real TCP/UDP sockets (single-threaded poll loop).
// The Client itself stays transport-agnostic; this is the thin layer an Android native service would reuse
// (there, the car side is the CDC-NCM interface and the host/port come from the NCM neighbour table).
//
// Tested only against a fake car on 127.0.0.1 (tests/test_loopback_session.cpp). Not a real USB/NCM link.
#pragma once
#include "airplay_client.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <sys/socket.h>

namespace s10::airplay {

class SessionRunner {
public:
    SessionRunner() = default;
    ~SessionRunner();
    SessionRunner(const SessionRunner&) = delete;
    SessionRunner& operator=(const SessionRunner&) = delete;

    // Binds our UDP timing responder (ephemeral port) BEFORE the Client is built: its SETUP carries timingPort.
    // v6=true binds a dual-stack IPv6 socket (needed when the car is an IPv6 link-local peer such as the NCM link).
    int bindTiming(bool v6 = false);
    // Binds the UDP socket that receives the radio's microphone RTP (ephemeral port, returned; -1 on error). Call BEFORE the
    // Client is built when ClientConfig.micPort is wanted. Packets reach ClientCallbacks.onMic through Client::onMicPacket.
    int bindMic(bool v6 = false);
    // Dial the car's RTSP control port, then start the Client and execute its first actions.
    // host: IPv4 dotted quad, or IPv6 literal with optional scope, e.g. "fe80::30c9:b4ff:fe92:809%eth0".
    bool start(Client& client, const std::string& host, int controlPort);
    // One iteration: wait up to timeoutMs for traffic, feed the Client, answer timing requests, run 1 s housekeeping.
    bool step(int timeoutMs, uint64_t nowMs);
    // Encoder / capture side.
    bool pushVideo(const uint8_t* annexB, size_t n);
    bool pushAudio(const uint8_t* pcmLe, size_t n);

    // Optional wire trace: channel name ("ctl-tx","ctl-rx","evt-rx","evt-tx"), bytes. For hardware bring-up logs.
    std::function<void(const char*, const uint8_t*, size_t)> trace;
    bool failed() const { return failed_; }
    const std::string& error() const { return error_; }

private:
    bool run(const std::vector<Action>& actions);
    bool fail(const std::string& why);
    bool dialTcp(int port, bool nodelay, int& fdOut);
    bool sendAll(int fd, const std::vector<uint8_t>& b);
    void closeAll();

    Client* client_ = nullptr;
    std::string host_;
    sockaddr_storage target_{};
    socklen_t targetLen_ = 0;
    int family_ = 0;
    bool setTarget(const std::string& host);
    int ctl_ = -1, evt_ = -1, scr_ = -1, aud_ = -1, udpOut_ = -1, tim_ = -1, mic_ = -1;
    int audioPort_ = 0;
    unsigned micSeen_ = 0;
    bool failed_ = false;
    std::string error_;
};

}  // namespace s10::airplay
