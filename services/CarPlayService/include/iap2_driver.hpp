// Phone-side iAP2 identification + authentication + post-auth driver.
//
// EVIDENCE: sbuysse/OrangeCar docs/protocol.md section 3.5 (GPL-3.0; clean-room), measured on
// ONE head unit. Message/param ids that the doc does not give are marked UNVERIFIED in the .cpp.
//
// SECURITY NOTE: as the phone we are the *verifier* of the accessory's MFi certificate. This
// driver does NOT verify the certificate/signature (same as the reference); it exposes them via
// Callbacks so a verifying policy can be added. No Apple secret is needed or used.
#pragma once
#include "iap2_session.hpp"
#include <cstdint>
#include <functional>
#include <string>

namespace s10::iap2 {

struct DriverConfig {
    std::string deviceName = "iPhone";   // DeviceInformationUpdate (0x4E09)
    std::string language = "en";
    std::function<uint64_t()> unixTime;                    // required for 0x4E0B
    std::function<void(uint8_t* out, size_t n)> random;    // required: 32-byte challenge
};

struct DriverCallbacks {
    std::function<void(const std::vector<uint8_t>& cert)> onCertificate;
    std::function<void(const std::vector<uint8_t>& challenge, const std::vector<uint8_t>& resp)> onAuthResponse;
    std::function<void(const std::string& nmea)> onLocation;          // 0xFFFB
    std::function<void(const Message& m)> onUnhandled;
};

class Driver {
public:
    enum class Phase { Link, Identifying, Authenticating, Ready, Failed };
    Driver(DriverConfig cfg, DriverCallbacks cb = {});

    // Raw bytes from the accessory. Returns wire packets to write (bulk OUT).
    std::vector<std::vector<uint8_t>> onBytes(const uint8_t* d, size_t n);
    std::vector<std::vector<uint8_t>> retransmit();

    Phase phase() const { return phase_; }
    const DeviceSession& session() const { return s_; }
    const std::vector<uint16_t>& accessoryAcceptList() const { return accept_; }

private:
    void step();
    void handle(const Message& m);
    void postAuthBurst();
    void reply(uint16_t id, std::vector<Param> p = {}) { s_.sendMessage(Message{id, std::move(p)}); }

    DriverConfig cfg_;
    DriverCallbacks cb_;
    DeviceSession s_;
    Phase phase_ = Phase::Link;
    bool started_ = false;
    std::vector<uint16_t> accept_;
    bool declaresGga_ = false, declaresRmc_ = false, declaresGsv_ = false, declaresPascd_ = false;
    std::vector<uint8_t> challenge_;
};

}  // namespace s10::iap2
