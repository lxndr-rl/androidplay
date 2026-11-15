#include "airplay_client.hpp"

namespace s10::airplay {
using bplist::Value;

Client::Client(ClientConfig cfg, ClientCallbacks cb) : cfg_(std::move(cfg)), cb_(std::move(cb)) {}

void Client::fail(const std::string& why, std::vector<Action>& out) {
    if (state_ == State::Failed) return;
    state_ = State::Failed;
    failure_ = why;
    queue_.clear();
    inflight_.reset();
    out.push_back(Action{Action::Kind::Failed, 0, {}, why});
}

std::function<void(const RtspMessage&, std::vector<Action>&)> Client::okOnly() {
    return [](const RtspMessage&, std::vector<Action>&) {};
}

void Client::sendNext(std::vector<Action>& out) {
    if (inflight_ || queue_.empty() || state_ == State::Failed) return;
    inflight_ = std::move(queue_.front());
    queue_.pop_front();
    out.push_back(Action{Action::Kind::SendControl, 0, buildRequest(inflight_->method, inflight_->path, ++cseq_, inflight_->body), {}});
}

std::vector<Action> Client::start() {
    std::vector<Action> out;
    if (state_ != State::Idle) return out;
    if (!cfg_.randomConnId || !cfg_.nowNtp) { fail("ClientConfig.randomConnId/nowNtp required", out); return out; }
    state_ = State::Running;
    SetupConfig sc{cfg_.deviceId, cfg_.uuid, cfg_.correlationUuid, cfg_.timingPort};

    // Step 1: SETUP /setup (et=1). 4xx would mean the car wants pairing: not supported here.
    enqueue(Step{"SETUP", "/setup", bplist::write(initialSetup(sc)), [this](const RtspMessage& m, std::vector<Action>& out) {
        auto v = bplist::read(m.body.data(), m.body.size());
        auto r = v ? parseSetupReply(*v) : std::nullopt;
        if (!r || r->eventPort == 0) { fail("SETUP /setup reply has no eventPort", out); return; }
        setup_ = *r;

        // Step 2: GET /info (empty-dict body)
        enqueue(Step{"GET", "/info", bplist::write(Value::Dict()), [this](const RtspMessage& m, std::vector<Action>& out) {
            auto v = bplist::read(m.body.data(), m.body.size());
            auto i = v ? parseInfo(*v) : std::nullopt;
            if (!i) { fail("/info reply unusable (no display size)", out); return; }
            info_ = *i;
            touchOwner_ = std::make_unique<TouchTracker>(info_->width, info_->height, info_->touchLogicalX, info_->touchLogicalY);
            // Step 3: open event channel (we dial; the car sends requests on it)
            out.push_back(Action{Action::Kind::OpenEvent, setup_.eventPort, {}, {}});
            // Step 4: RECORD /
            enqueue(Step{"RECORD", "/", {}, [this](const RtspMessage&, std::vector<Action>& out) {
                // Step 5: modesChanged (we own screen+audio). Optional initial sync first (car's view: screen 2/2).
                if (cfg_.initialScreenSync && info_->mainScreenTake100) {
                    ModesState sync; sync.screenEntity = 2; sync.screenPermanent = 2;
                    enqueue(Step{"POST", "/command", bplist::write(modesChanged(sync)), okOnly()});
                }
                queueModes();
                // Step 6: screen SETUP
                connId_ = cfg_.randomConnId();
                if (connId_ <= 0) { fail("randomConnId returned non-positive", out); return; }
                enqueue(Step{"SETUP", "/", bplist::write(screenSetup(connId_, info_->displayUuid)), [this](const RtspMessage& m, std::vector<Action>& out) {
                    auto v = bplist::read(m.body.data(), m.body.size());
                    auto r = v ? parseSetupReply(*v) : std::nullopt;
                    if (!r || r->screenDataPort == 0) { fail("screen SETUP reply has no dataPort", out); return; }
                    cipher_ = std::make_unique<StreamCipher>(std::to_string(static_cast<uint64_t>(connId_)));
                    if (!cipher_->ok()) { fail("cipher init failed", out); return; }
                    startNtp_ = cfg_.nowNtp();
                    state_ = State::Streaming;
                    needKey_ = true;
                    out.push_back(Action{Action::Kind::OpenScreen, r->screenDataPort, {}, {}});
                    // Step 8: audio, only if we still own MainAudio (never SETUP audio while the car owns it).
                    if (cfg_.enableAudio && modes_.audioEntity == kEntityController) {
                        enqueue(Step{"SETUP", "/", bplist::write(audioSetup("media", 0x8000, false, 0, 0)), [this](const RtspMessage& m, std::vector<Action>& out) {
                            auto v = bplist::read(m.body.data(), m.body.size());
                            auto r = v ? parseSetupReply(*v) : std::nullopt;
                            if (!r || r->audioDataPort == 0) { fail("audio SETUP reply has no dataPort", out); return; }
                            audio_ = std::make_unique<AudioPacketizer>(2);
                            out.push_back(Action{Action::Kind::OpenAudio, r->audioDataPort, {}, {}});
                        }});
                    }
                }});
            }});
        }});
    }});
    sendNext(out);
    return out;
}

std::vector<Action> Client::onControlBytes(const uint8_t* d, size_t n) {
    std::vector<Action> out;
    if (state_ == State::Failed) return out;
    for (auto& m : ctl_.feed(d, n)) {
        if (m.isRequest || !inflight_) continue;      // control channel only carries replies
        Step st = std::move(*inflight_);
        inflight_.reset();
        if (m.status < 200 || m.status > 299) {
            fail(st.method + " " + st.path + " -> " + std::to_string(m.status) +
                 (st.path == "/setup" ? " (car may require pairing: unsupported)" : ""), out);
            return out;
        }
        st.onReply(m, out);
        if (state_ == State::Failed) return out;
    }
    if (ctl_.failed()) fail("malformed RTSP on control channel", out);
    sendNext(out);
    return out;
}

std::vector<uint8_t> Client::modesParamsBody(int status) {
    Value v = Value::Dict();
    v.set("status", Value::Int(status));
    if (status == 0) v.set("params", *modesChanged(modes_).get("params"));
    return bplist::write(v);
}

void Client::handleEventRequest(const RtspMessage& m, std::vector<Action>& out) {
    auto reply = [&](std::vector<uint8_t> bytes) { out.push_back(Action{Action::Kind::SendEvent, 0, std::move(bytes), {}}); };
    if (!m.isRequest || m.method != "POST" || m.path != "/command") { reply(buildOkReply(m.cseq)); return; }
    auto v = bplist::read(m.body.data(), m.body.size());
    const Value* type = v ? v->get("type") : nullptr;
    if (!type || type->t != Value::T::Str) { reply(buildOkReply(m.cseq)); return; }

    if (type->s == "changeModes") {
        const Value* params = v->get("params");
        const Value* res = params ? params->get("resources") : nullptr;
        ModesState next = modes_;
        bool refuse = false;
        if (res && res->t == Value::T::Array)
            for (const auto& r : res->a) {
                auto id = r.get("resourceID"), tt = r.get("transferType");
                if (!id || !tt || !id->asInt() || !tt->asInt()) continue;
                int& entity = *id->asInt() == 1 ? next.screenEntity : next.audioEntity;
                if (*id->asInt() != 1 && *id->asInt() != 2) continue;
                switch (*tt->asInt()) {
                case 1: refuse = true; break;                 // Take: refused (not modelled)
                case 2: entity = kEntityController; break;   // Untake
                case 3: entity = kEntityAccessory; break;    // Borrow (permanent stays 1)
                case 4: entity = kEntityController; break;   // Unborrow
                default: break;
                }
            }
        if (refuse) { reply(buildReply(200, m.cseq, modesParamsBody(1))); return; }
        modes_ = next;
        reply(buildReply(200, m.cseq, modesParamsBody(0)));   // reply first...
        queueModes(); needKey_ = true;   // ...then a fresh modesChanged + keyframe
        std::vector<Action> o; sendNext(o); out.insert(out.end(), o.begin(), o.end());
        if (cb_.onKeyFrameRequested) cb_.onKeyFrameRequested();
        return;
    }
    reply(buildOkReply(m.cseq));
    if (type->s == "requestUI") {
        needKey_ = true; queueModes();
        std::vector<Action> o; sendNext(o); out.insert(out.end(), o.begin(), o.end());
        if (cb_.onKeyFrameRequested) cb_.onKeyFrameRequested();
    } else if (type->s == "forceKeyFrame") {
        needKey_ = true;
        if (cb_.onKeyFrameRequested) cb_.onKeyFrameRequested();
    } else if (type->s == "hidSendReport") {
        const Value* uuid = v->get("uuid");
        const Value* rep = v->get("hidReport");
        if (rep && rep->t == Value::T::Data) {
            if (cb_.onHid) cb_.onHid(uuid && uuid->t == Value::T::Str ? uuid->s : std::string(), rep->d);
            if (cb_.onTouch && touchOwner_ && modes_.screenEntity == kEntityController)   // touches ignored while car owns screen
                if (auto e = touchOwner_->onReport(rep->d.data(), rep->d.size())) cb_.onTouch(*e);
        }
    }
}

std::vector<Action> Client::onEventBytes(const uint8_t* d, size_t n) {
    std::vector<Action> out;
    if (state_ == State::Failed) return out;
    for (auto& m : evt_.feed(d, n)) handleEventRequest(m, out);
    if (evt_.failed()) fail("malformed RTSP on event channel", out);
    return out;
}

std::vector<Action> Client::pushVideo(const uint8_t* annexB, size_t n) {
    std::vector<Action> out;
    if (state_ != State::Streaming || !cipher_ || !info_) return out;
    AccessUnit au = toAvcc(annexB, n);
    if (au.avcc.empty()) return out;
    if (!au.sps.empty()) sps_ = au.sps;
    if (!au.pps.empty()) pps_ = au.pps;
    if (!sentConfig_ || needKey_) {
        if (!au.idr) return out;                      // P-frames before the first IDR are dropped
        auto avcC = buildAvcC(sps_, pps_);
        if (avcC.empty()) return out;                 // IDR without parameter sets: wait for one that has them
        auto h = frameHeader(Op::VideoConfig, static_cast<uint32_t>(avcC.size()), 0,
                             static_cast<float>(info_->width), static_cast<float>(info_->height));
        std::vector<uint8_t> b(h.begin(), h.end());
        b.insert(b.end(), avcC.begin(), avcC.end());
        out.push_back(Action{Action::Kind::SendScreen, 0, std::move(b), {}});
        sentConfig_ = true; needKey_ = false;
    }
    auto enc = cipher_->process(au.avcc.data(), au.avcc.size());
    if (enc.size() != au.avcc.size()) { fail("video encrypt failed", out); return out; }
    uint64_t ts = cfg_.nowNtp() - startNtp_;
    auto h = frameHeader(Op::VideoFrame, static_cast<uint32_t>(enc.size()), ts);
    std::vector<uint8_t> b(h.begin(), h.end());
    b.insert(b.end(), enc.begin(), enc.end());
    out.push_back(Action{Action::Kind::SendScreen, 0, std::move(b), {}});
    return out;
}

std::vector<Action> Client::pushAudioPcm(const uint8_t* pcm, size_t n) {
    std::vector<Action> out;
    if (!audio_ || state_ != State::Streaming) return out;
    for (auto& p : audio_->feed(pcm, n)) out.push_back(Action{Action::Kind::SendAudio, 0, std::move(p), {}});
    return out;
}

std::vector<Action> Client::tick(uint64_t nowMs) {
    std::vector<Action> out;
    if (state_ != State::Streaming) return out;
    if (haveTick_ && nowMs - lastTickMs_ < 1000) return out;
    haveTick_ = true; lastTickMs_ = nowMs;
    if (!inflight_ && queue_.empty()) { enqueue(Step{"POST", "/feedback", {}, okOnly()}); sendNext(out); }
    bool stats = info_ && info_->keepAliveStats;
    std::vector<uint8_t> body = stats ? keepAliveBody() : std::vector<uint8_t>{};
    auto h = frameHeader(stats ? Op::KeepAliveStats : Op::KeepAlive, static_cast<uint32_t>(body.size()));
    std::vector<uint8_t> b(h.begin(), h.end());
    b.insert(b.end(), body.begin(), body.end());
    out.push_back(Action{Action::Kind::SendScreen, 0, std::move(b), {}});
    if (setup_.keepAlivePort) out.push_back(Action{Action::Kind::SendUdpKeepAlive, setup_.keepAlivePort, std::vector<uint8_t>(4, 0), {}});
    return out;
}

std::vector<uint8_t> Client::onTimingPacket(const uint8_t* d, size_t n) const {
    return timingReply(d, n, cfg_.nowNtp ? cfg_.nowNtp() : 0);
}

}  // namespace s10::airplay
