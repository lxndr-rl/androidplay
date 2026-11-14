// Standalone fake head unit for device/emulator tests: listens on all interfaces, prints the control port, runs for
// N seconds (or until stdin closes), then prints a JSON summary of what it received. Test fixture only.
//   fake_car_server SECONDS        e.g. an emulator app dials 10.0.2.2:<ctl port>
#include "fake_car.hpp"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
    int seconds = argc > 1 ? std::atoi(argv[1]) : 20;
    g_bindAny = true;
    FakeCar car;
    std::printf("CONTROL_PORT=%d\n", car.ctlPort);
    std::fflush(stdout);
    std::thread t([&] { car.run(); });
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    car.stop = true;
    t.join();
    int feedbacks = 0, configs = 0, okReplies = 0;
    for (auto& s : car.rx.seq) if (s == "POST /feedback") ++feedbacks;
    for (int op : car.rx.ops) if (op == 1) ++configs;
    for (auto& m : car.eventReplies) if (m.status == 200) ++okReplies;
    bool nalsOk = !car.rx.plainFrames.empty();
    for (auto& f : car.rx.plainFrames) nalsOk = nalsOk && f.size() > 4 && (f[4] & 0x1F) >= 1 && (f[4] & 0x1F) <= 8;
    std::string seq;
    for (size_t i = 0; i < car.rx.seq.size() && i < 8; ++i) seq += (i ? " | " : "") + car.rx.seq[i];
    std::printf("SUMMARY {\"frames\":%zu,\"configs\":%d,\"width\":%d,\"height\":%d,\"decrypt_ok\":%s,\"audio_packets\":%d,"
                "\"feedback\":%d,\"keepalive_udp\":%d,\"timing_replies\":%d,\"event_replies_ok\":%d,\"seq_head\":\"%s\"}\n",
                car.rx.plainFrames.size(), configs, int(car.rx.w), int(car.rx.h), nalsOk ? "true" : "false", car.audioPkts,
                feedbacks, car.keepAlives, car.timingReplies, okReplies, seq.c_str());
    return 0;
}
