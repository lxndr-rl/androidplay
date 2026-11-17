// Shared fake CarPlay head unit (receiver side), written from the OrangeCar protocol doc. Used by the in-process and
// loopback-socket tests. It proves internal consistency of our sender only, never compatibility with a real car.
#pragma once
#include "airplay_client.hpp"
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace s10::airplay;
using s10::bplist::Value;

struct FakeReceiver {
    RtspParser parser;
    std::vector<std::string> seq;                    // "METHOD path[:type]" in arrival order
    bool failSetup = false;                          // answer SETUP /setup with 466
    bool statsKeepAlive = false;
    bool takeScreen = true;
    bool rejectMic = false;                          // answer an INPUT (microphone) SETUP with 400
    std::string micType; int micFormat = 0, micDataPort = 0, micSetups = 0;
    int64_t connId = 0;
    int eventPort = 7001, keepAlivePort = 7002, screenPort = 7100, audioPort = 7200, audioCtlPort = 7201;
    int clientTimingPort = 0;
    std::vector<uint8_t> screenBuf;
    std::vector<uint8_t> avcC; float w = 0, h = 0;
    std::vector<std::vector<uint8_t>> plainFrames;   // decrypted AVCC payloads
    std::unique_ptr<StreamCipher> dec;
    std::vector<int> ops;
    std::vector<uint8_t> audioPkts;
    int audioCount = 0;

    std::vector<uint8_t> onControl(const uint8_t* d, size_t n) {
        std::vector<uint8_t> out;
        for (auto& m : parser.feed(d, n)) {
            std::vector<uint8_t> body;
            int status = 200;
            auto v = m.body.empty() ? std::nullopt : s10::bplist::read(m.body.data(), m.body.size());
            std::string tag = m.method + " " + m.path;
            if (m.path == "/setup") {
                if (v && v->get("timingPort") && v->get("timingPort")->asInt()) clientTimingPort = static_cast<int>(*v->get("timingPort")->asInt());
                if (failSetup) status = 466;
                else {
                    Value r = Value::Dict(); r.set("eventPort", Value::Int(eventPort)).set("keepAlivePort", Value::Int(keepAlivePort)).set("timingPort", Value::Int(7003));
                    body = s10::bplist::write(r);
                }
            } else if (m.path == "/info") {
                Value d0 = Value::Dict(); d0.set("widthPixels", Value::Str("1200")).set("heightPixels", Value::Str("512")).set("uuid", Value::Str("DISP-UUID"));
                Value res = Value::Dict(); res.set("resourceID", Value::Int(1)).set("transferType", Value::Int(1)).set("takeConstraint", Value::Str("100"));
                Value modes = Value::Dict(); modes.set("resources", Value::Array().push(res));
                Value info = Value::Dict(); info.set("displays", Value::Array().push(d0)).set("keepAliveSendStatsAsBody", Value::Bool(statsKeepAlive));
                if (takeScreen) info.set("modes", modes);
                body = s10::bplist::write(info);
            } else if (m.path == "/command" && v) {
                tag += ":" + v->get("type")->s;
                if (v->get("type")->s == "modesChanged") {
                    auto& rs = v->get("params")->get("resources")->a;
                    tag += ":" + std::to_string(rs[0].get("entity")->i) + "/" + std::to_string(rs[1].get("entity")->i);
                }
            } else if (m.path == "/" && m.method == "SETUP" && v) {
                auto& st = v->get("streams")->a[0];
                auto type = st.get("type")->i;
                if (auto* in = st.get("input"); in && in->asBool() && *in->asBool()) {
                    ++micSetups;
                    micType = st.get("audioType")->s; micFormat = static_cast<int>(st.get("audioFormat")->i);
                    if (auto* dp = st.get("dataPort")) micDataPort = static_cast<int>(dp->i);
                    if (rejectMic) status = 400;
                }
                tag += ":" + std::to_string(type);
                Value s = Value::Dict(); s.set("type", Value::Int(type));
                if (type == 110) {
                    connId = st.get("streamConnectionID")->i;
                    dec = std::make_unique<StreamCipher>(std::to_string(static_cast<uint64_t>(connId)));
                    s.set("dataPort", Value::Int(screenPort));
                } else s.set("dataPort", Value::Int(audioPort)).set("controlPort", Value::Int(audioCtlPort));
                Value r = Value::Dict(); r.set("streams", Value::Array().push(s));
                body = s10::bplist::write(r);
            }
            seq.push_back(tag);
            auto rep = buildReply(status, m.cseq, body);
            out.insert(out.end(), rep.begin(), rep.end());
        }
        return out;
    }
    void onScreen(const std::vector<uint8_t>& b) {
        screenBuf.insert(screenBuf.end(), b.begin(), b.end());
        while (screenBuf.size() >= 128) {
            uint32_t len = screenBuf[0] | screenBuf[1] << 8 | screenBuf[2] << 16 | uint32_t(screenBuf[3]) << 24;
            if (screenBuf.size() < 128 + len) break;
            int op = screenBuf[4];
            ops.push_back(op);
            std::vector<uint8_t> payload(screenBuf.begin() + 128, screenBuf.begin() + 128 + len);
            if (op == 1) {
                avcC = payload;
                uint32_t bw = screenBuf[16] | screenBuf[17] << 8 | screenBuf[18] << 16 | uint32_t(screenBuf[19]) << 24;
                uint32_t bh = screenBuf[20] | screenBuf[21] << 8 | screenBuf[22] << 16 | uint32_t(screenBuf[23]) << 24;
                std::memcpy(&w, &bw, 4); std::memcpy(&h, &bh, 4);
            } else if (op == 0 && dec) plainFrames.push_back(dec->process(payload.data(), payload.size()));
            screenBuf.erase(screenBuf.begin(), screenBuf.begin() + 128 + len);
        }
    }
};

