// Phone-side iAP2 link session state machine (pure: bytes in, bytes out; no I/O, no clock).
//
// EVIDENCE: sbuysse/OrangeCar docs/protocol.md sections 3.1-3.4 (GPL-3.0; clean-room,
// measured on one car). Retransmission timers are NOT implemented: the caller owns the
// clock and may call retransmitPending(). EAK/RST/SLP handling minimal (RST closes).
#pragma once
#include "iap2_control.hpp"
#include "iap2_link.hpp"
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace s10::iap2 {

constexpr uint8_t kCtrlEak = 0x20;
constexpr uint8_t kCtrlRst = 0x10;

struct LinkParams {
    uint8_t version = 1;
    uint8_t maxOutstanding = 5;
    uint16_t maxRxLen = 4096;
    uint16_t retransmitMs = 500;
    uint16_t cumAckMs = 176;
    uint8_t maxRetrans = 30;
    uint8_t maxCumAcks = 10;
    uint8_t controlSession = 1;    // from SYN session table (type 0)
    uint8_t fileSession = 2;       // type 1 (0 if absent)
    std::vector<uint8_t> rawSessions;  // untouched session table bytes
};

// Parse the SYN payload (>= 10 bytes + 3-byte session entries).
std::optional<LinkParams> parseSyn(const std::vector<uint8_t>& payload);
// Build the SYN|ACK payload: echo car's values, counter-propose maxOutstanding=0x7F, maxRxLen=0xFFFF.
std::vector<uint8_t> buildSynAckPayload(const LinkParams& car);

class DeviceSession {
public:
    enum class State { WaitSyn, WaitAck, Established, Closed };

    // Feed raw bytes from the accessory (bulk IN). Outgoing wire bytes are appended
    // to takeTx(); in-order control messages to takeMessages().
    void onBytes(const uint8_t* data, size_t len);

    // Queue a control message (one DATA packet outstanding at a time). Returns false if not established,
    // or if a gate list is set and the accessory did not list the id in its 0x0007 list.
    bool sendMessage(const Message& m);

    void setAccessoryAcceptList(std::vector<uint16_t> list) { accept_ = std::move(list); gated_ = true; }

    // Re-send the unacked packet (caller drives timers). Returns false if none pending.
    bool retransmitPending();

    std::vector<std::vector<uint8_t>> takeTx();
    std::vector<Message> takeMessages();

    State state() const { return state_; }
    const LinkParams& params() const { return params_; }
    size_t droppedBytes() const { return dropped_; }
    bool hasPending() const { return pending_.has_value(); }

private:
    void handlePacket(const Packet& p);
    void emit(Packet p);
    void sendAck();
    void pump();

    State state_ = State::WaitSyn;
    LinkParams params_;
    std::vector<uint8_t> rx_;
    std::vector<std::vector<uint8_t>> tx_;
    std::vector<Message> msgs_;
    ControlAssembler asm_;
    std::vector<uint16_t> accept_;
    bool gated_ = false;
    uint8_t txSeq_ = 0;          // seq of last DATA we sent (our SYN|ACK used 0)
    uint8_t lastRxSeq_ = 0;      // last in-order seq received from accessory
    std::deque<std::vector<uint8_t>> outq_;   // encoded control messages awaiting a send slot
    std::optional<Packet> pending_;   // unacked DATA
    size_t dropped_ = 0;
};

}  // namespace s10::iap2
