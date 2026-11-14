// iAP2 link-layer packet codec (protocol-agnostic, no Android deps).
//
// EVIDENCE: layout, constants and checksum are taken from ONE unofficial source:
//   https://gist.github.com/JJTech0130/a878496479afedb93dd02399551e3aec
//   (carkit_iap2.py, JJTech0130, 2026-07-05, no license stated; layout re-derived
//   by reading the code, not copied).  Apple's iAP2 spec is MFi-NDA and was NOT
//   consulted.  NOT validated against a real iPhone or head unit.
//   UNKNOWN (not implemented): link-sync negotiation semantics, retransmission,
//   session payload formats, control-message framing (0x40 0x40 ...), CarPlay
//   message IDs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace s10::iap2 {

constexpr uint8_t kStart0 = 0xFF;
constexpr uint8_t kStart1 = 0x5A;
constexpr size_t kHeaderLen = 9;

// Control-byte values observed in the gist (bit meaning beyond these is UNKNOWN).
constexpr uint8_t kCtrlSyn = 0x80;
constexpr uint8_t kCtrlAck = 0x40;

struct Packet {
    uint8_t control = 0;
    uint8_t seq = 0;
    uint8_t ack = 0;
    uint8_t session = 0;
    std::vector<uint8_t> payload;
};

// Two's-complement byte-sum checksum: (0x100 - sum) & 0xFF.
uint8_t checksum(const uint8_t* data, size_t len);

// Serialize. Length field (BE16) = 9 + (payload.empty() ? 0 : payload.size()+1).
// Returns empty vector if the payload is too large for the 16-bit length.
std::vector<uint8_t> encode(const Packet& p);

enum class DecodeStatus { Ok, NeedMore, BadStart, BadLength, BadHeaderChecksum, BadPayloadChecksum };

struct DecodeResult {
    DecodeStatus status;
    size_t consumed = 0;   // bytes of input consumed when Ok
    Packet packet;
};

// Decode one packet from the front of buf. Never reads past len.
DecodeResult decode(const uint8_t* buf, size_t len);

// Incremental stream parser: feed arbitrary chunks, collect packets; resyncs on
// garbage by scanning for the FF 5A start bytes.
class StreamParser {
public:
    std::vector<Packet> feed(const uint8_t* data, size_t len);
    size_t droppedBytes() const { return dropped_; }
private:
    std::vector<uint8_t> buf_;
    size_t dropped_ = 0;
};

}  // namespace s10::iap2
