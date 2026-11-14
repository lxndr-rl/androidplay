// Device-side usbmux ("MUX over USB bulk") as spoken between a USB host (head unit) and an iOS-like device.
// Wire format taken from the libimobiledevice usbmuxd HOST implementation (src/device.c, GPL-2/3: read as a
// specification only, no code copied). We play the DEVICE: answer VERSION, accept SETUP, accept TCP SYNs for
// listening ports, carry data with ACK/window, close with FIN/RST.
// Status: host-tested only against a mux host written by us from the same source. NOT verified against the
// Zlink5 radio, Chevrolet radio, or any real host.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace s10::usbmux {

constexpr uint32_t kProtoVersion = 0, kProtoControl = 1, kProtoSetup = 2, kProtoTcp = 6;
constexpr uint32_t kMagic = 0xfeedface;
constexpr uint8_t kFin = 0x01, kSyn = 0x02, kRst = 0x04, kAck = 0x10;
constexpr size_t kTcpHdr = 20;
// Host reads bulk-IN in 16384-byte units and treats a transfer of exactly 16384 as "continued". Keep every packet
// we send strictly below that (and see docs/hardware-evidence.md: bulk sizes that are multiples of 512 need a ZLP).
constexpr size_t kMaxPayload = 16000;

struct Packet {
    uint32_t proto = 0;
    bool v2 = false;          // 16-byte header (magic + seq) vs 8-byte (version negotiation only)
    uint16_t tx_seq = 0, rx_seq = 0;
    std::vector<uint8_t> body;  // everything after the mux header
};

// Splits a byte stream into mux packets using the length field. Returns false on a malformed header.
// The header size depends on whether the version handshake completed, so packets are pulled one at a time.
class Framer {
public:
    explicit Framer(const bool* v2) : v2_(v2) {}
    void append(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
    int next(Packet& out);  // 1 = packet produced, 0 = need more bytes, -1 = malformed
private:
    const bool* v2_;
    std::vector<uint8_t> buf_;
};

class Device {
public:
    using Out = std::function<void(const std::vector<uint8_t>&)>;  // one call = one bulk-IN transfer
    struct Handlers {
        std::function<bool(uint16_t port)> accept;                  // may this port be connected to?
        std::function<void(uint16_t port, const uint8_t*, size_t)> data;
        std::function<void(uint16_t port)> closed;
    };
    Device(Out out, Handlers h, std::set<uint16_t> ports) : out_(std::move(out)), h_(std::move(h)), ports_(std::move(ports)), framer_(&v2_) {}

    // Bytes read from bulk-OUT. Returns false on a protocol violation (caller should stop the session).
    bool feed(const uint8_t* p, size_t n);
    // Queue data to the host on the connection to `port`. False if no such connection.
    bool send(uint16_t port, const uint8_t* p, size_t n);
    void close(uint16_t port);
    bool active() const { return setup_; }
    size_t pending(uint16_t port) const;

private:
    struct Conn {
        uint16_t host_port = 0, dev_port = 0;
        uint32_t tx_seq = 0;   // our next sequence number
        uint32_t tx_ack = 0;   // next byte we expect from host
        uint32_t peer_ack = 0; // highest byte of ours the host acked
        uint32_t peer_win = 0;
        bool established = false;
        std::vector<uint8_t> outq;
    };
    void emit(uint32_t proto, const uint8_t* body, size_t n);
    void emit_tcp(const Conn& c, uint8_t flags, const uint8_t* d, size_t n);
    void rst(uint16_t sport, uint16_t dport, uint32_t ack);
    void flush(Conn& c);
    bool on_packet(const Packet& p);
    bool on_tcp(const Packet& p);

    Out out_;
    Handlers h_;
    std::set<uint16_t> ports_;
    bool v2_ = false;
    bool setup_ = false;
    uint16_t tx_seq_ = 0, rx_seq_ = 0xFFFF;
    Framer framer_;
    std::map<uint16_t, Conn> conns_;  // keyed by device-side (dport of host SYN) port
};

}  // namespace s10::usbmux
