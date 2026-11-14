// AirPlay/CarPlay client sequencer, phone side. Transport-agnostic: the caller owns sockets/clock/threads and
// moves bytes in and out; the sequencer returns Actions. Implements the ordered sequence of
// sbuysse/OrangeCar docs/protocol.md 6.2-6.3 (legacy path, et=1). Tested only against an in-process fake
// receiver written from the same document: it proves internal consistency, NOT compatibility with a real car.
#pragma once
#include "airplay.hpp"
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace s10::airplay {

struct Action {
    enum class Kind {
        SendControl,        // bytes -> RTSP control TCP (we dialled the car's :7000)
        OpenEvent,          // dial car:port (event channel); afterwards route its bytes to onEventBytes
        OpenScreen,         // dial car:port (TCP_NODELAY) for the screen data channel
        OpenAudio,          // car UDP data port for RTP audio
        SendEvent,          // bytes -> event channel TCP
        SendScreen,         // bytes -> screen data TCP
        SendAudio,          // RTP packet -> UDP audio port
        SendUdpKeepAlive,   // 4 zero bytes -> keepAlivePort (UDP)
        Failed,             // session cannot continue; `note` says why
    } kind;
    int port = 0;
    std::vector<uint8_t> bytes;
    std::string note;
};

struct ClientConfig {
    std::string deviceId = "AA:BB:CC:DD:EE:FF";
    std::string uuid, correlationUuid;               // fresh random uppercase UUIDs from the caller
    int timingPort = 0;                              // our UDP timing responder port
    std::function<int64_t()> randomConnId;           // required: random in 1..2^63-1
    std::function<uint64_t()> nowNtp;                // required: NTP 32.32 (see ntpFromUnixMicros)
    bool enableAudio = true;
    bool initialScreenSync = true;                   // doc: built, UNVERIFIED on a car
};

struct ClientCallbacks {
    std::function<void(const TouchEvent&)> onTouch;                              // from 5-byte reports (legacy layout)
    std::function<void(const std::string& uuid, const std::vector<uint8_t>& report)> onHid;   // every HID report
    std::function<void()> onKeyFrameRequested;
};

class Client {
public:
    enum class State { Idle, Running, Streaming, Failed };

    Client(ClientConfig cfg, ClientCallbacks cb = {});

    std::vector<Action> start();                                          // queue SETUP /setup
    std::vector<Action> onControlBytes(const uint8_t* d, size_t n);       // replies from the car
    std::vector<Action> onEventBytes(const uint8_t* d, size_t n);         // car's requests (POST /command)
    std::vector<Action> pushVideo(const uint8_t* annexB, size_t n);       // one access unit from the encoder
    std::vector<Action> pushAudioPcm(const uint8_t* pcmLe, size_t n);     // 48 kHz 16-bit stereo LE
    std::vector<Action> tick(uint64_t nowMs);                             // call ~every 100 ms; 1 s housekeeping
    std::vector<uint8_t> onTimingPacket(const uint8_t* d, size_t n) const;// UDP timing request -> reply (empty if bad)

    State state() const { return state_; }
    bool needKeyFrame() const { return needKey_; }                        // encoder should emit an IDR
    const std::optional<InfoReply>& info() const { return info_; }
    const ModesState& modes() const { return modes_; }
    const std::string& failure() const { return failure_; }

private:
    struct Step {
        std::string method, path;
        std::vector<uint8_t> body;
        std::function<void(const RtspMessage&, std::vector<Action>&)> onReply;
    };
    void enqueue(Step s) { queue_.push_back(std::move(s)); }
    void sendNext(std::vector<Action>& out);
    void fail(const std::string& why, std::vector<Action>& out);
    void queueModes() { enqueue(Step{"POST", "/command", bplist::write(modesChanged(modes_)), okOnly()}); }
    std::function<void(const RtspMessage&, std::vector<Action>&)> okOnly();
    void handleEventRequest(const RtspMessage& m, std::vector<Action>& out);
    std::vector<uint8_t> modesParamsBody(int status);

    ClientConfig cfg_;
    ClientCallbacks cb_;
    State state_ = State::Idle;
    std::string failure_;
    int cseq_ = 0;
    std::deque<Step> queue_;
    std::optional<Step> inflight_;
    RtspParser ctl_, evt_;
    std::optional<InfoReply> info_;
    SetupReply setup_;
    ModesState modes_;
    int64_t connId_ = 0;
    std::unique_ptr<StreamCipher> cipher_;
    std::vector<uint8_t> sps_, pps_;
    bool sentConfig_ = false, needKey_ = false;
    uint64_t startNtp_ = 0, lastTickMs_ = 0;
    bool haveTick_ = false;
    std::unique_ptr<AudioPacketizer> audio_;
    std::unique_ptr<TouchTracker> touchOwner_;
};

}  // namespace s10::airplay
