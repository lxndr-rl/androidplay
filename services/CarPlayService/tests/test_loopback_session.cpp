// End-to-end over REAL sockets on 127.0.0.1: Client + SessionRunner against a fake car (own thread, own sockets).
// Video and audio come from real emulator captures (fixtures). Proves the socket glue, ordering across channels,
// event-channel round trips and cross-thread behaviour. NOT a USB/NCM link and NOT a real head unit.
#include "fake_car.hpp"
#include "session_runner.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main(int argc, char** argv) {
    const char* vpath = argc > 1 ? argv[1] : "tests/fixtures/emulator_settings_1200x512.h264";
    const char* apath = argc > 2 ? argv[2] : "tests/fixtures/emulator_tone440_48k_stereo_1s.pcm";
    auto h264 = readFile(vpath); auto pcm = readFile(apath);
    if (h264.empty() || pcm.empty()) { std::printf("fixtures missing\n"); return 2; }
    std::vector<std::vector<uint8_t>> aus; { std::vector<uint8_t> cur;
        for (auto& n : splitAnnexB(h264.data(), h264.size())) { cur.insert(cur.end(), {0, 0, 0, 1}); cur.insert(cur.end(), n.begin(), n.end()); int t = n[0] & 0x1F; if (t == 1 || t == 5) { aus.push_back(cur); cur.clear(); } } }

    FakeCar car;
    std::thread carThread([&] { car.run(); });

    SessionRunner runner;
    int timingPort = runner.bindTiming();
    CHECK(timingPort > 0);
    ClientConfig cfg; cfg.uuid = "U1"; cfg.correlationUuid = "U2"; cfg.timingPort = timingPort;
    cfg.randomConnId = [] { return int64_t(987654321987654321LL); };
    auto t0 = std::chrono::steady_clock::now();
    auto nowUs = [&] { return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count()); };
    cfg.nowNtp = [&] { return ntpFromUnixMicros(1700000000000000ULL + nowUs()); };
    std::vector<TouchEvent> touches; int keyReqs = 0;
    ClientCallbacks cb; cb.onTouch = [&](const TouchEvent& e) { touches.push_back(e); }; cb.onKeyFrameRequested = [&] { ++keyReqs; };
    Client client(cfg, cb);

    CHECK(runner.start(client, "127.0.0.1", car.ctlPort));
    auto nowMs = [&] { return nowUs() / 1000; };
    for (int i = 0; i < 300 && client.state() != Client::State::Streaming && !runner.failed(); ++i) runner.step(10, nowMs());
    CHECK(!runner.failed());
    if (runner.failed()) std::printf("runner error: %s\n", runner.error().c_str());
    CHECK(client.state() == Client::State::Streaming);

    // Stream ~2.6 s: one AU per 33 ms (looping the fixture), 10 ms audio chunks (looping the 1 s capture).
    size_t ai = 0, pi = 0; uint64_t nextVideo = nowMs(), nextAudio = nowMs(), end = nowMs() + 2600;
    size_t sentAus = 0;
    while (nowMs() < end && !runner.failed()) {
        uint64_t t = nowMs();
        if (t >= nextVideo && sentAus < aus.size() * 2) { runner.pushVideo(aus[ai % aus.size()].data(), aus[ai % aus.size()].size()); ++ai; ++sentAus; nextVideo += 33; }
        if (t >= nextAudio) { const size_t c = 480 * 4; if (pi + c > pcm.size()) pi = 0; runner.pushAudio(pcm.data() + pi, c); pi += c; nextAudio += 10; }
        runner.step(2, t);
    }
    CHECK(!runner.failed());
    for (int i = 0; i < 30; ++i) runner.step(10, nowMs());     // drain replies / late modesChanged
    car.stop = true; carThread.join();

    // ---- assertions on what the fake car saw over the sockets ----
    const std::vector<std::string> head = {"SETUP /setup", "GET /info", "RECORD /", "POST /command:modesChanged:2/1", "POST /command:modesChanged:1/1", "SETUP /:110", "SETUP /:100"};
    CHECK(car.rx.seq.size() >= head.size() && std::equal(head.begin(), head.end(), car.rx.seq.begin()));
    int feedbacks = 0; bool borrowAnnounced = false;
    for (auto& s : car.rx.seq) { if (s == "POST /feedback") ++feedbacks; if (s == "POST /command:modesChanged:1/2") borrowAnnounced = true; }
    CHECK(feedbacks >= 2);
    CHECK(borrowAnnounced);                                    // changeModes(borrow audio) -> fresh modesChanged on control channel
    CHECK(car.keepAlives >= 2);                                // UDP idle keepalive
    CHECK(car.timingReplies == 1);                             // our UDP timing responder answered the car
    CHECK(car.audioPkts > 100);                                // ~2.6 s of audio at 136 pkt/s (minus startup/borrow)
    CHECK(car.rx.plainFrames.size() >= 40);
    int configs = 0; for (int op : car.rx.ops) if (op == 1) ++configs;
    CHECK(configs >= 2);                                       // forceKeyFrame -> config re-sent with next IDR
    CHECK(car.rx.avcC.size() > 10 && car.rx.w == 1200.f && car.rx.h == 512.f);
    bool decryptOk = !car.rx.plainFrames.empty();
    for (auto& f : car.rx.plainFrames) decryptOk = decryptOk && f.size() > 4 && (f[4] & 0x1F) >= 1 && (f[4] & 0x1F) <= 8;   // first NAL header sane
    CHECK(decryptOk);                                          // keystream stayed in sync across socket reads
    CHECK(car.eventReplies.size() == 4);
    int okReplies = 0; for (auto& m : car.eventReplies) if (m.status == 200) ++okReplies;
    CHECK(okReplies == 4);
    CHECK(touches.size() == 2 && touches[0].kind == TouchEvent::Kind::Down && touches[1].kind == TouchEvent::Kind::Up && touches[0].x == 1199 && touches[0].y == 511);
    CHECK(keyReqs >= 2);
    std::printf("frames=%zu configs=%d audio=%d feedback=%d keepalive=%d ops=%zu\n", car.rx.plainFrames.size(), configs, car.audioPkts, feedbacks, car.keepAlives, car.rx.ops.size());
    if (fails) { std::printf("%d FAILED\n", fails); return 1; }
    std::printf("ALL PASSED\n");
    return 0;
}
