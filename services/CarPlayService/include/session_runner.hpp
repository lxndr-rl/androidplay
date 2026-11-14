// POSIX socket glue for the AirPlay Client: executes its Actions on real TCP/UDP sockets (single-threaded poll loop).
// The Client itself stays transport-agnostic; this is the thin layer an Android native service would reuse
// (there, the car side is the CDC-NCM interface and the host/port come from the NCM neighbour table).
//
// Tested only against a fake car on 127.0.0.1 (tests/test_loopback_session.cpp). Not a real USB/NCM link.
#pragma once
#include "airplay_client.hpp"
#include <cstdint>
#include <string>

namespace s10::airplay {

class SessionRunner {
public:
    SessionRunner() = default;
    ~SessionRunner();
    SessionRunner(const SessionRunner&) = delete;
    SessionRunner& operator=(const SessionRunner&) = delete;

    // Binds our UDP timing responder (ephemeral port) BEFORE the Client is built: its SETUP carries timingPort.
    int bindTiming();
    // Dial the car's RTSP control port, then start the Client and execute its first actions.
    bool start(Client& client, const std::string& host, int controlPort);
    // One iteration: wait up to timeoutMs for traffic, feed the Client, answer timing requests, run 1 s housekeeping.
    bool step(int timeoutMs, uint64_t nowMs);
    // Encoder / capture side.
    bool pushVideo(const uint8_t* annexB, size_t n);
    bool pushAudio(const uint8_t* pcmLe, size_t n);

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
    int ctl_ = -1, evt_ = -1, scr_ = -1, aud_ = -1, udpOut_ = -1, tim_ = -1;
    int audioPort_ = 0;
    bool failed_ = false;
    std::string error_;
};

}  // namespace s10::airplay
