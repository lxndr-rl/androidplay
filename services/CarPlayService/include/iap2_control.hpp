// iAP2 control-session message codec (phone/device side).
//
// EVIDENCE: framing from https://github.com/sbuysse/OrangeCar docs/protocol.md section 3.5
// (GPL-3.0, pushed 2026-10-10; facts re-implemented clean-room, no code copied).
// Measured by that project on ONE head unit (Jaguar F-PACE / Harman). Not validated here.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace s10::iap2 {

// Message:  40 40 | LL LL (total, from the 40 40) | MM MM (id) | params
// Param:    LL LL (incl. 4-byte header) | II II (id) | data
struct Param {
    uint16_t id = 0;
    std::vector<uint8_t> data;
};

struct Message {
    uint16_t id = 0;
    std::vector<Param> params;
};

// Message ids used on the phone side of identification/authentication.
constexpr uint16_t kStartIdentification = 0x1D00;
constexpr uint16_t kIdentificationInformation = 0x1D01;
constexpr uint16_t kIdentificationAccepted = 0x1D02;
constexpr uint16_t kRequestAuthCertificate = 0xAA00;
constexpr uint16_t kAuthCertificate = 0xAA01;
constexpr uint16_t kRequestAuthChallengeResponse = 0xAA02;
constexpr uint16_t kAuthResponse = 0xAA03;
constexpr uint16_t kAuthSucceeded = 0xAA05;

Param strParam(uint16_t id, const std::string& s);   // UTF-8 + NUL
Param u8Param(uint16_t id, uint8_t v);
Param u16Param(uint16_t id, uint16_t v);             // big endian
Param u64Param(uint16_t id, uint64_t v);             // big endian
Param groupParam(uint16_t id, const std::vector<Param>& children);  // data = encoded child params
// Decode a group param's children (false on malformed).
bool parseGroup(const Param& g, std::vector<Param>& out);

// Returns empty on overflow (> 65535).
std::vector<uint8_t> encodeMessage(const Message& m);

enum class MsgStatus { Ok, NeedMore, BadMagic, BadLength, BadParam };
struct MsgResult { MsgStatus status; size_t consumed = 0; Message msg; };
MsgResult decodeMessage(const uint8_t* buf, size_t len);

// Reassembles messages that span several link packets.
class ControlAssembler {
public:
    std::vector<Message> feed(const uint8_t* data, size_t len);
    size_t droppedBytes() const { return dropped_; }
private:
    std::vector<uint8_t> buf_;
    size_t dropped_ = 0;
};

// Accessory-declared "messages received from the device" list (param 0x0007).
// Only these may be sent device->accessory. Empty/missing list blocks nothing.
std::vector<uint16_t> parseMessageList(const Param& p);
bool accepts(const std::vector<uint16_t>& list, uint16_t id);

}  // namespace s10::iap2
