// AirPlay/CarPlay session primitives, phone side (pure: no sockets, no clock, no threads).
//
// EVIDENCE: sbuysse/OrangeCar docs/protocol.md sections 5-6 (GPL-3.0, pushed 2026-10-10), measured on ONE
// legacy wired head unit (Jaguar F-PACE / Harman, AirPlay srcvers 280.33.12.1). Clean-room re-implementation
// of documented facts; no code copied. Values marked UNVERIFIED are not stated by the source.
// Legacy path only: plaintext RTSP, et=1, AES-128-CTR video. Paired (ChaCha20) sessions NOT implemented.
#pragma once
#include "bplist.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace s10::airplay {

constexpr const char* kSourceVersion = "566.25.21";   // must equal mDNS srcvers and User-Agent

// ---------- RTSP (6.1) ----------
struct RtspMessage {
    bool isRequest = false;
    std::string method, path;       // request
    int status = 0;                 // response
    int cseq = 0;
    std::string contentType;
    std::vector<uint8_t> body;
};

// Request: "METHOD /path RTSP/1.0", CSeq, User-Agent, [Content-Type + Content-Length only if body].
std::vector<uint8_t> buildRequest(const std::string& method, const std::string& path, int cseq,
                                  const std::vector<uint8_t>& body = {});
// Reply on the event channel where the car is the requester: "RTSP/1.0 200 OK\r\nCSeq: n\r\n\r\n".
std::vector<uint8_t> buildOkReply(int cseq);
// 200 reply carrying a bplist body (used for changeModes answers, and by test receivers).
std::vector<uint8_t> buildReply(int status, int cseq, const std::vector<uint8_t>& body);

class RtspParser {   // incremental; handles both requests (car /command) and responses
public:
    std::vector<RtspMessage> feed(const uint8_t* d, size_t n);
    bool failed() const { return failed_; }
private:
    std::vector<uint8_t> buf_;
    bool failed_ = false;
};

// ---------- bodies (6.2, 6.3) ----------
struct SetupConfig {
    std::string deviceId = "AA:BB:CC:DD:EE:FF";   // MAC-format string
    std::string uuid, correlationUuid;            // caller supplies fresh random uppercase UUIDs
    int timingPort = 0;
};
bplist::Value initialSetup(const SetupConfig& c);                  // 'et' = 1
bplist::Value screenSetup(int64_t streamConnectionId, const std::string& displayUuid);  // type 110, latencyMs 25
bplist::Value audioSetup(const std::string& audioType, int audioFormatBit, bool input, int dataPort, int controlPort);

enum Entity : int { kEntityNone = 0, kEntityController = 1, kEntityAccessory = 2 };
struct ModesState { int screenEntity = 1, screenPermanent = 1, audioEntity = 1, audioPermanent = 1; bool phoneCall = false; };
bplist::Value modesChanged(const ModesState& s);                   // POST /command body

// ---------- replies (6.2) ----------
struct InfoReply {
    int width = 0, height = 0;
    std::string displayUuid;
    bool keepAliveStats = false;      // key value unrecorded in the source -> default false (UNVERIFIED)
    bool mainScreenTake100 = false;   // modes: MainScreen Take, takeConstraint 100 -> initial screen sync
    // From the touch-screen HID descriptor in /info hidDevices (observed on a real radio: 0..1280 x 0..720, not 0..32767).
    int touchLogicalX = 32767, touchLogicalY = 32767;
};
// Minimal HID report-descriptor scan: logical maximum of the Generic Desktop X (0x30) and Y (0x31) inputs. false if absent.
bool hidTouchLogicalMax(const uint8_t* d, size_t n, int& maxX, int& maxY);
std::optional<InfoReply> parseInfo(const bplist::Value& v);
struct SetupReply { int eventPort = 0, keepAlivePort = 0, timingPort = 0; int screenDataPort = 0, audioDataPort = 0, audioControlPort = 0; };
std::optional<SetupReply> parseSetupReply(const bplist::Value& v);

// ---------- screen stream frames (6.4) ----------
enum class Op : uint8_t { VideoFrame = 0, VideoConfig = 1, KeepAlive = 2, KeepAliveStats = 5 };
constexpr size_t kFrameHeaderLen = 128;
// Little-endian 128-byte header; payload follows. ntpTimestamp only for VideoFrame.
std::array<uint8_t, kFrameHeaderLen> frameHeader(Op op, uint32_t bodySize, uint64_t ntpTimestamp = 0,
                                                 float width = 0, float height = 0);
// Build avcC from raw SPS/PPS NAL units (with NAL header byte): 01 sps[1..3] FF E1 len16 SPS 01 len16 PPS.
std::vector<uint8_t> buildAvcC(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps);
// Split Annex-B into NAL units (no start codes).
std::vector<std::vector<uint8_t>> splitAnnexB(const uint8_t* d, size_t n);
// One access unit -> AVCC (4-byte BE length per NAL), AUD (type 9) dropped, SPS/PPS kept.
struct AccessUnit { std::vector<uint8_t> avcc; bool idr = false; std::vector<uint8_t> sps, pps; };
AccessUnit toAvcc(const uint8_t* annexB, size_t n);
std::vector<uint8_t> keepAliveBody();   // 42-byte bplist of an empty dict (opcode 5)

// AES-128-CTR, key/iv from the streamConnectionID WE sent. Continuous keystream across frames.
class StreamCipher {
public:
    explicit StreamCipher(const std::string& streamConnectionId);   // unsigned decimal string
    ~StreamCipher();
    StreamCipher(const StreamCipher&) = delete;
    StreamCipher& operator=(const StreamCipher&) = delete;
    std::vector<uint8_t> process(const uint8_t* d, size_t n);
    const std::array<uint8_t, 16>& key() const { return key_; }
    const std::array<uint8_t, 16>& iv() const { return iv_; }
    bool ok() const { return ctx_ != nullptr; }
private:
    std::array<uint8_t, 16> key_{}, iv_{};
    void* ctx_ = nullptr;
};

// ---------- audio RTP (6.5) ----------
// 16-bit PCM, little-endian input -> big-endian RTP payload; 352 frames/packet; PT 100; SSRC 0.
class AudioPacketizer {
public:
    AudioPacketizer(int channels = 2, uint16_t startSeq = 0, uint32_t startTs = 0)
        : ch_(channels), seq_(startSeq), ts_(startTs) {}
    std::vector<std::vector<uint8_t>> feed(const uint8_t* pcmLe, size_t n);
    // Zero-filled packet (silence > 200 ms keeps the stream alive).
    std::vector<uint8_t> silence();
private:
    std::vector<uint8_t> packet(const uint8_t* pcmLe);
    int ch_; uint16_t seq_; uint32_t ts_;
    std::vector<uint8_t> pend_;
};
constexpr size_t kRtpFramesPerPacket = 352;
// Microphone input (the radio -> us): one RTP packet carrying 16-bit big-endian PCM -> little-endian PCM.
// Skips CSRC entries and a header extension. Empty if the packet is malformed or has no payload.
std::vector<uint8_t> rtpPcmToLe(const uint8_t* pkt, size_t n);

// ---------- timing (6.8) ----------
uint64_t ntpFromUnixMicros(uint64_t unixMicros);   // 32.32 since 1900
// Request: 32 bytes, 0x80 0xD2 ... origin in the last stamp. Returns empty if malformed.
std::vector<uint8_t> timingReply(const uint8_t* req, size_t n, uint64_t nowNtp);

// ---------- input (6.6) ----------
struct TouchEvent { enum class Kind { Down, Move, Up } kind; int x, y; };
// 5-byte layout (bit0 tip, X u16 LE, Y u16 LE, no report id) seen on two radios. The LOGICAL RANGE differs: 0..32767
// on the documented car, 0..1280 x 0..720 on the Zlink radio: take it from the /info hidDescriptor (hidTouchLogicalMax).
class TouchTracker {
public:
    TouchTracker(int captureW, int captureH, int logicalX = 32767, int logicalY = 32767)
        : w_(captureW), h_(captureH), lx_(logicalX > 0 ? logicalX : 32767), ly_(logicalY > 0 ? logicalY : 32767) {}
    std::optional<TouchEvent> onReport(const uint8_t* r, size_t n);
private:
    int w_, h_, lx_, ly_;
    bool down_ = false;
};

}  // namespace s10::airplay
