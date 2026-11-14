#include "iap2_driver.hpp"
#include <algorithm>

namespace s10::iap2 {

Driver::Driver(DriverConfig cfg, DriverCallbacks cb) : cfg_(std::move(cfg)), cb_(std::move(cb)) {}

static bool contains(const std::vector<uint8_t>& d, const char* needle) {
    std::string n(needle);
    return std::search(d.begin(), d.end(), n.begin(), n.end()) != d.end();
}

std::vector<std::vector<uint8_t>> Driver::onBytes(const uint8_t* d, size_t n) {
    s_.onBytes(d, n);
    step();
    return s_.takeTx();
}

std::vector<std::vector<uint8_t>> Driver::retransmit() {
    s_.retransmitPending();
    return s_.takeTx();
}

void Driver::step() {
    if (s_.state() == DeviceSession::State::Closed) { phase_ = Phase::Failed; return; }
    if (!started_ && s_.state() == DeviceSession::State::Established) {
        started_ = true;
        phase_ = Phase::Identifying;
        reply(kStartIdentification);  // not gated: handshake
    }
    for (auto& m : s_.takeMessages()) handle(m);
}

void Driver::handle(const Message& m) {
    switch (m.id) {
    case kIdentificationInformation: {
        for (const auto& p : m.params) {
            if (p.id == 0x0007) accept_ = parseMessageList(p);
            if (p.id == 0x0016) {  // location information component: declared NMEA sentences
                declaresGga_ = contains(p.data, "GPGGA");
                declaresRmc_ = contains(p.data, "GPRMC");
                declaresGsv_ = contains(p.data, "GPGSV");
                declaresPascd_ = contains(p.data, "PASCD");
            }
        }
        s_.setAccessoryAcceptList(accept_);
        s_.sendMessage(Message{kIdentificationAccepted, {}}, true);
        phase_ = Phase::Authenticating;
        s_.sendMessage(Message{kRequestAuthCertificate, {}}, true);
        break;
    }
    case kAuthCertificate: {
        if (!m.params.empty() && cb_.onCertificate) cb_.onCertificate(m.params[0].data);
        challenge_.assign(32, 0);
        if (cfg_.random) cfg_.random(challenge_.data(), challenge_.size());
        Param c; c.id = 0; c.data = challenge_;
        s_.sendMessage(Message{kRequestAuthChallengeResponse, {c}}, true);
        break;
    }
    case kAuthResponse: {
        if (!m.params.empty() && cb_.onAuthResponse) cb_.onAuthResponse(challenge_, m.params[0].data);
        s_.sendMessage(Message{kAuthSucceeded, {}}, true);
        phase_ = Phase::Ready;
        postAuthBurst();
        break;
    }
    case 0xAE00:  // StartPowerUpdates: 1000 mA max current, will charge (doc 3.5)
        reply(0xAE01, {u16Param(0, 1000)});
        reply(0xAE01, {u8Param(1, 1)});
        break;
    case 0x5000:  // StartNowPlayingUpdates: "reset" = playback group{status 0, app "", bundle ""}
        reply(0x5001, {groupParam(1, {u8Param(0, 0), strParam(7, ""), strParam(0x10, "")})});
        break;
    case 0x4C00:  // StartMediaLibraryInformation. Wrapper group id UNVERIFIED (doc names only children).
        reply(0x4C01, {groupParam(0, {strParam(0, "iPhone"), strParam(1, "1"), u8Param(2, 0)})});
        break;
    case 0x4E03:  // StartBluetoothConnectionUpdates
        reply(0x4E04, {u16Param(0, 1), groupParam(1, {})});
        break;
    case 0xFFFB:
        for (const auto& p : m.params)
            if (p.id == 0 && cb_.onLocation) cb_.onLocation(std::string(p.data.begin(), p.data.end()));
        break;
    case 0x4E01: case 0xAE03:  // informational, no answer needed
        break;
    default:
        if (cb_.onUnhandled) cb_.onUnhandled(m);
    }
}

void Driver::postAuthBurst() {
    // Each message only if the accessory listed it in 0x0007 (gate on by default).
    reply(0x4E09, {strParam(0, cfg_.deviceName)});
    reply(0x4E0A, {strParam(0, cfg_.language)});
    uint64_t t = cfg_.unixTime ? cfg_.unixTime() : 0;
    reply(0x4E0B, {u64Param(0, t), u16Param(1, 0), u8Param(2, 0)});
    std::vector<Param> loc;
    if (declaresGga_) loc.push_back(Param{1, {}});
    if (declaresRmc_) loc.push_back(Param{2, {}});
    if (declaresGsv_) loc.push_back(Param{3, {}});
    if (declaresPascd_) loc.push_back(Param{4, {}});
    // If the car declared no location component, send nothing (the reference only sends what is declared).
    if (!loc.empty()) reply(0xFFFA, loc);
}

}  // namespace s10::iap2
