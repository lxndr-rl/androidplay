// JNI bridge: runs the CarPlay sender (Client + SessionRunner) on its own thread inside an Android process.
// Java pushes encoded video / PCM audio in; native calls back for touch and keyframe requests.
// Only the runner thread touches Client/SessionRunner; other threads talk to it through queues.
#include "session_runner.hpp"
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <jni.h>
#include <mutex>
#include <random>
#include <thread>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "S10CarOS-CP", __VA_ARGS__)
using namespace s10::airplay;

namespace {

struct Session {
    JavaVM* vm = nullptr;
    jobject listener = nullptr;
    jmethodID onTouch = nullptr, onKey = nullptr;
    std::thread th;
    std::atomic<bool> stop{false};
    std::mutex m;
    std::deque<std::vector<uint8_t>> video, audio;
    std::string host, status = "idle";
    int port = 0;
    std::atomic<uint64_t> droppedVideo{0}, videoIn{0}, audioIn{0};
};

constexpr size_t kMaxQueuedVideo = 30;   // bounded latency: drop the oldest access unit when the car side stalls
constexpr size_t kMaxQueuedAudio = 50;   // 500 ms of 10 ms chunks

void setStatus(Session* s, const std::string& v) { std::lock_guard<std::mutex> g(s->m); s->status = v; }

// A Java exception left pending makes the NEXT JNI call abort the whole process (CheckJNI), so never leave one.
void clearPending(JNIEnv* env, const char* what) {
    if (env->ExceptionCheck()) {
        LOG("Java exception in %s callback (cleared)", what);
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

void run(Session* s) {
    JNIEnv* env = nullptr;
    s->vm->AttachCurrentThread(&env, nullptr);
    SessionRunner runner;
    int timingPort = runner.bindTiming();
    std::random_device rd;
    auto rnd = [&] { return (static_cast<uint64_t>(rd()) << 32) | rd(); };
    auto uuid = [&] { char b[40]; snprintf(b, sizeof b, "%08X-%04X-4%03X-8%03X-%012llX", rd(), rd() & 0xFFFF, rd() & 0xFFF, rd() & 0xFFF, (unsigned long long)(rnd() & 0xFFFFFFFFFFFFULL)); return std::string(b); };
    auto t0 = std::chrono::steady_clock::now();
    auto nowUs = [&] { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count()); };
    auto wallUs = [&] { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); };

    ClientConfig cfg;
    cfg.uuid = uuid(); cfg.correlationUuid = uuid(); cfg.timingPort = timingPort;
    cfg.randomConnId = [&] { return static_cast<int64_t>(rnd() & 0x7FFFFFFFFFFFFFFFULL) | 1; };
    cfg.nowNtp = [&] { return ntpFromUnixMicros(wallUs()); };
    ClientCallbacks cb;
    cb.onTouch = [&](const TouchEvent& e) {
        env->CallVoidMethod(s->listener, s->onTouch, static_cast<jint>(e.kind), static_cast<jint>(e.x), static_cast<jint>(e.y));
        clearPending(env, "onTouch");
    };
    cb.onKeyFrameRequested = [&] { env->CallVoidMethod(s->listener, s->onKey); clearPending(env, "onKeyFrameRequested"); };
    Client client(cfg, cb);

    setStatus(s, "connecting");
    if (timingPort <= 0 || !runner.start(client, s->host, s->port)) {
        setStatus(s, "failed: " + (timingPort <= 0 ? std::string("timing socket") : runner.error()));
        s->vm->DetachCurrentThread();
        return;
    }
    bool wasStreaming = false;
    while (!s->stop && !runner.failed()) {
        std::vector<std::vector<uint8_t>> v, a;
        {
            std::lock_guard<std::mutex> g(s->m);
            while (!s->video.empty()) { v.push_back(std::move(s->video.front())); s->video.pop_front(); }
            while (!s->audio.empty()) { a.push_back(std::move(s->audio.front())); s->audio.pop_front(); }
        }
        for (auto& x : v) runner.pushVideo(x.data(), x.size());
        for (auto& x : a) runner.pushAudio(x.data(), x.size());
        runner.step(5, nowUs() / 1000);
        if (!wasStreaming && client.state() == Client::State::Streaming) { wasStreaming = true; setStatus(s, "streaming"); LOG("streaming"); }
    }
    if (runner.failed()) { setStatus(s, "failed: " + runner.error()); LOG("failed: %s", runner.error().c_str()); }
    else setStatus(s, "stopped");
    s->vm->DetachCurrentThread();
}

Session* from(jlong h) { return reinterpret_cast<Session*>(h); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_s10caros_projection_CarPlayNative_create(JNIEnv* env, jclass, jobject listener) {
    auto* s = new Session();
    env->GetJavaVM(&s->vm);
    s->listener = env->NewGlobalRef(listener);
    jclass c = env->GetObjectClass(listener);
    s->onTouch = env->GetMethodID(c, "onTouch", "(III)V");
    s->onKey = env->GetMethodID(c, "onKeyFrameRequested", "()V");
    return reinterpret_cast<jlong>(s);
}

JNIEXPORT void JNICALL Java_com_s10caros_projection_CarPlayNative_start(JNIEnv* env, jclass, jlong h, jstring host, jint port) {
    Session* s = from(h);
    const char* c = env->GetStringUTFChars(host, nullptr);
    s->host = c; env->ReleaseStringUTFChars(host, c);
    s->port = port;
    s->th = std::thread(run, s);
}

JNIEXPORT void JNICALL Java_com_s10caros_projection_CarPlayNative_pushVideo(JNIEnv* env, jclass, jlong h, jbyteArray data, jint len) {
    Session* s = from(h);
    std::vector<uint8_t> b(static_cast<size_t>(len));
    env->GetByteArrayRegion(data, 0, len, reinterpret_cast<jbyte*>(b.data()));
    std::lock_guard<std::mutex> g(s->m);
    s->videoIn++;
    if (s->video.size() >= kMaxQueuedVideo) { s->video.pop_front(); s->droppedVideo++; }
    s->video.push_back(std::move(b));
}

JNIEXPORT void JNICALL Java_com_s10caros_projection_CarPlayNative_pushAudio(JNIEnv* env, jclass, jlong h, jbyteArray data, jint len) {
    Session* s = from(h);
    std::vector<uint8_t> b(static_cast<size_t>(len));
    env->GetByteArrayRegion(data, 0, len, reinterpret_cast<jbyte*>(b.data()));
    std::lock_guard<std::mutex> g(s->m);
    s->audioIn++;
    if (s->audio.size() >= kMaxQueuedAudio) s->audio.pop_front();
    s->audio.push_back(std::move(b));
}

JNIEXPORT jstring JNICALL Java_com_s10caros_projection_CarPlayNative_status(JNIEnv* env, jclass, jlong h) {
    Session* s = from(h);
    std::lock_guard<std::mutex> g(s->m);
    std::string t = s->status + " videoIn=" + std::to_string(s->videoIn.load()) + " droppedVideo=" + std::to_string(s->droppedVideo.load()) +
                    " audioIn=" + std::to_string(s->audioIn.load());
    return env->NewStringUTF(t.c_str());
}

JNIEXPORT void JNICALL Java_com_s10caros_projection_CarPlayNative_destroy(JNIEnv* env, jclass, jlong h) {
    Session* s = from(h);
    s->stop = true;
    if (s->th.joinable()) s->th.join();
    env->DeleteGlobalRef(s->listener);
    delete s;
}

}  // extern "C"
