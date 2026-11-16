# Duende validation

Date of this record: 2026-11-14. Only what was actually run is marked. No phone and no CarPlay radio have been available, so every criterion that needs hardware is **NOT TESTED**.

## How to run what exists

| What | Command | Level |
|---|---|---|
| All host test suites (CarPlayService via CMake + ASan/UBSan, GadgetManager, script syntax, launcher core) | `scripts/run-tests.sh` (`--strict` fails on skipped suites) | 1 |
| Launcher APK build + signature check | `packages/CarLauncher/build.sh apk` | 1 (build only) |
| Host requirements for the ROM build | `scripts/verify.sh` | 0 |
| Device snapshot (read-only, adb) | `scripts/diagnostics.sh [-s SERIAL]` | 2-3 |
| USB gadget/UDC/Type-C probe | `scripts/usb-probe.sh` | 3 |

`diagnostics.sh` checks the model (`SM-G975F*`/`SM-G975N*`) and warns for anything else; device-specific procedures stay blocked until it passes.

## Test levels (from the project brief)

| Level | Scope | State 2026-11-14 |
|---|---|---|
| 1 Software | unit tests, static analysis, serialization | **Run:** 6/6 suites pass (see below). No static analyzer beyond `-Wall -Wextra` and sanitizers. |
| 2 Android | services start, permissions, SELinux, capture, encode, audio, input injection | PARTIAL, emulator only (see below). Not the S10+, not LineageOS. |
| 3 USB | enumeration, descriptors, endpoints, reconnect | NOT TESTED (host-side tests use a fake configfs tree only) |
| 4 CarPlay | radio detection, auth, session, video, audio, controls | NOT TESTED. Mocks/fake receivers written by us prove internal consistency only. |
| 5 Real hardware | S10+, radio, cable, power, thermals, soak | NOT TESTED |

## Level-1 results (`scripts/run-tests.sh`, 2026-11-14)

| Suite | Result | Notes |
|---|---|---|
| carplay-configure / build (CMake 3.22.1 from the Android SDK, g++ 13, ASan+UBSan) | PASS | |
| carplay-tests (ctest): iap2_link, iap2_session, iap2_driver, airplay, airplay_client | PASS | known-answer vectors from the OrangeCar doc (captured SYN, golden AES key/IV), Python plistlib interop, openssl CLI keystream |
| gadgetmanager (`make test`, 46 checks) | PASS | fake configfs directory, not a real UDC |
| scripts-syntax (`bash -n`) | PASS | |
| launcher-core (plain Java, JVM 17) | PASS | reconnect backoff, config validation, tile ordering |

Not covered: the Android shell classes of the launcher (built, never executed), the FFS ep0 loop, anything needing a kernel.

## Emulator run (2026-11-14, Level 2, NOT the target device)

Hardware/OS: Android Emulator, AVD `Medium_Phone_API_36.1` (x86_64, API 36 Google APIs Play Store image, KVM), headless. This says nothing about the S10+, LineageOS, SELinux policy of the ROM, or USB.

| Check | Result | Evidence |
|---|---|---|
| First install crashed: `startForeground(connectedDevice)` threw SecurityException on API 36 (needs one of CHANGE_NETWORK_STATE, CHANGE_WIFI_MULTICAST_STATE, ...) | **bug found and fixed** (both added to the manifest) | logcat AndroidRuntime stack, then `dumpsys`: `isForeground=true types=0x10` |
| APK installs, `LauncherActivity` starts, `ConnectionService` stays foreground, no AndroidRuntime errors | PASS | pid present, no errors in logcat |
| Default start shows `CONFIG mode / IDLE`, tiles in landscape, own package hidden | PASS | screenshot (not stored in repo) |
| `mode=vehicle`, `apps.pinned=settings,chrome`, `apps.hidden=youtube` via `run-as` | PASS: banner `VEHICLE mode / WAIT_USB`, Settings and Chrome first, YouTube absent | screenshot |
| Invalid config (`mode=turbo`, unknown key, bad number) | PASS: no crash, falls back to CONFIG mode, banner shows `config errors: 3` | screenshot |
| Tapping a tile launches that app | PASS (Calendar launched; ActivityTaskManager START in logcat). First tap at wrong coordinates (portrait/landscape mix-up) hit nothing. | logcat |
| `diagnostics.sh` device path | PASS on emulator: collected 13 files; model `sdk_gphone64_x86_64` correctly produced the "not SM-G975F/N" warning | output |
| Becomes the default HOME app | NOT TESTED (HOME key went to the stock launcher) | – |
| USB attach/detach events driving the policy | NOT TESTED (USB_STATE is a protected broadcast; no emulated USB) | – |

### MEDIA-001 probe on the emulator (2026-11-14, same AVD, software encoder)

`services/ProjectionService` (probe app, `build.sh apk`): app-owned public VirtualDisplay (1200x512, 160 dpi) fed to a MediaCodec H.264 baseline surface encoder; Android Settings launched onto that display with `am start --display`, scrolled with `input -d`.

| Check | Result | Evidence |
|---|---|---|
| Blank display produces no frames | observed: 0 frames in 12 s | expected: surface encoder only emits on content; idle-repeat needs a first frame |
| Settings on the virtual display, 14 s | 141 AUs, 5 key frames, 350 093 B (about 200 kbps, about 10 fps on mostly static content), first IDR 1.7 s after start | `metrics.json` pulled with run-as |
| Stream decodes | `ffprobe`: h264 Constrained Baseline, 1200x512, yuv420p, level 4.1, 141 frames; `ffmpeg` full decode without errors; frame 60 is a correct picture of the Settings app | ffprobe/ffmpeg output |
| Through the AirPlay video path | `test_capture_fixture` (70-frame fixture, ctest): AU split, AVCC length prefixes tile each AU, SPS/PPS on every IDR, avcC built, AES-CTR round trip | `scripts/run-tests.sh` |

Not shown, and not claimed:
- The S10+ hardware encoder (Exynos) was never run; emulator numbers say nothing about its latency, fps, bitrate or thermals.
- This is not the production capture path. A normal app only renders its own/launched content on its own display. Capturing the real phone screen without a consent dialog needs platform privileges in the ROM (a platform-signed service or a pre-granted MediaProjection/`CAPTURE_VIDEO_OUTPUT`-class permission): unresolved, ROM work.
- `requestKeyFrame()` is written but was not exercised; no audio capture; no touch injection; 60 fps not tried.

### MEDIA-003 touch injection on the emulator (2026-11-14, same AVD)

`services/InputBridge`: pure-Java `TouchMapper` (host-tested: drops MOVE/UP without DOWN, second DOWN becomes MOVE, clamping, release on disconnect) + `InjectMain`, a dex run with `app_process` as the adb shell uid that injects MotionEvents onto a given display through hidden `InputManagerGlobal.injectInputEvent`. Script: `tests/integration/emulator_touch.sh` (refuses non-emulators unless `S10CAROS_ALLOW_DEVICE=1`).

| Check | Result | Evidence |
|---|---|---|
| Tap injected at (300,215) onto the virtual display while Settings is shown there | injector reported `sent=2 dropped=0` | stdout |
| Effect visible in the **encoded stream** | PASS: frame before = Settings home; frame after = "Network & internet" page; PSNR 13.9 dB between them | `.build/emulator_touch/{before,after}.png` (viewed) |
| First attempts | failed for test-script reasons (stale metrics file from a previous run; foreground-activity grep not display-specific); fixed by using the stream itself as evidence | – |

Not shown: injection by an unprivileged process (impossible: needs `INJECT_EVENTS`; the ROM must run this as a platform/system component); multi-touch; CarPlay HID report -> injector wiring; hidden-API reflection on other Android versions or on the S10+ LineageOS; latency.

### MEDIA-002 audio on the emulator (2026-11-14, same AVD)

`services/AudioBridge`: `CaptureMain` (app_process as shell uid, fake `com.android.shell` system Context, hidden `AudioRecord.Builder.setContext`, source REMOTE_SUBMIX, 48 kHz stereo 16-bit) + a tone-test APK (fixture, never ship). Host side: `AudioPacer` in CarPlayService (sample-clock pacing at 352 frames/packet, silence after 200 ms starvation, bounded buffer).

| Check | Result | Evidence |
|---|---|---|
| Capture 5 s while an app plays 440 Hz | 960 000 bytes = exactly 5.000 s; dominant frequency 440 Hz (Goertzel 141.9 vs 0.0 at 300/600/1000 Hz) | `.build/capture.pcm`, `tests/integration/emulator_audio.sh` |
| Level | RMS about 200 (peak 284) against a source amplitude of 12 000: heavily attenuated by the emulator's media volume. Not investigated. | analysis output |
| 1 s of the real capture through `AudioPacer` + RTP packetizer | `test_audio_fixture`: 136 packets of 1408 B, bit-exact reconstruction of the capture (BE to LE), no silence, 440 Hz preserved | ctest |
| Pacing logic | `test_airplay_audio`: 136 packets per second of live input, bounded latency (drops oldest whole frames), no zeros on short gaps, zeros after 200 ms starvation, sequence numbers continuous | ctest |

Not shown: capture on the S10+ audio HAL; whether REMOTE_SUBMIX works there; capture of other streams (telephony, alert); per-app playback capture without privileges (needs MediaProjection or privileged permission; the ROM must run this as a system component); latency and A/V sync with the video; sending the packets to anything.

### CP-008 host part: whole sender stack over real loopback sockets (2026-11-14)

`SessionRunner` (POSIX poll loop executing the Client's Actions) against a fake car with its own threads and sockets on 127.0.0.1 (`test_loopback_session`, ctest, ASan/UBSan, 5/5 consecutive runs identical). Inputs are the two real emulator captures (H.264 Settings stream, 440 Hz PCM).

| Observed by the fake car over sockets | Result |
|---|---|
| Control sequence | SETUP /setup, GET /info, RECORD, modesChanged 2/1 (initial sync), modesChanged 1/1, SETUP screen, SETUP audio, in that order |
| Screen channel | 50 frames decrypted with an independently derived key (keystream stayed in sync across TCP reads); config sent twice (re-sent after the car's forceKeyFrame) |
| Audio | 353 RTP packets in 2.6 s (about 136/s) over UDP |
| Housekeeping | 2 feedback requests, 3 UDP keepalives, timing request answered once by our UDP responder |
| Event channel (car -> us) | 4 requests, 4 replies (200); touch Down/Up reached the callback scaled to 1199,511; forceKeyFrame reached the encoder hook; changeModes(borrow audio) followed by a fresh modesChanged 1/2 |

Not shown: any USB/CDC-NCM link, IPv6 link-local dialling, mDNS, a real head unit or any traffic with a real phone; the Android native service (JNI/foreground service) that would run this runner next to the encoder, injector and audio capture; stalled-socket handling (the 2 s send timeout exists but is not exercised); multi-hour behaviour.

### CP-008 Android native sender on the emulator -> fake car on the host (2026-11-14)

`libs10carplay.so` (NDK r26, C++17, built for x86_64 and arm64-v8a; only x86_64 was run) hosts Client + SessionRunner on a native thread inside `ProjectionService`; Java feeds it live H.264 from the VirtualDisplay encoder and a synthetic 440 Hz tone (test fixture, not capture). The fake head unit runs on the host (`fake_car_server`, all interfaces for about 30 s); the emulator dials it at 10.0.2.2. Script: `tests/integration/emulator_carplay.sh`.

| Observed | Result |
|---|---|
| Control sequence at the fake car, over the emulator's network | SETUP /setup, GET /info, RECORD, modesChanged 2/1, 1/1, SETUP screen, SETUP audio, then modesChanged 1/2 after the car's borrow request |
| Live video | 184 frames of a real Settings session decrypted correctly at the car; config 1200x512; 0 frames dropped in the native queue |
| Audio | 2 990 RTP packets in 22 s (about 136/s) |
| Housekeeping | 22 feedbacks, 23 UDP keepalives |
| Car -> phone | 4 event requests, 4 replies; touch down/up arrived in Java as (1199, 511); keyframe request reached the encoder |

Bugs found by this run, all fixed:
1. `socket()` failed inside the app: the manifest lacked `INTERNET` (status line `failed: timing socket`).
2. A Java exception inside a JNI callback left pending made the next JNI call abort the whole process (CheckJNI). Callbacks now clear pending exceptions.
3. The car's first commands arrived before the encoder existed (NPE in `requestKeyFrame`). The pipeline is now started before the car session is dialled, and `requestKeyFrame` tolerates a missing codec.

Also: SHA-512 and AES-128-CTR are now dependency-free (`crypto_portable`), because the NDK has no libcrypto; verified against OpenSSL on 300 digests and 100 CTR streams (counter carry, full 128-bit wrap, random chunking) plus FIPS vectors and the OrangeCar golden key/IV (`test_crypto_xcheck`).

Not shown: anything on the S10+ or with the arm64 build (compiled only); USB/NCM/IPv6/mDNS (the "car" is TCP/UDP over the emulator NAT); the host-to-phone UDP timing request (not routable through the emulator NAT; covered by the loopback test); privileged capture and injection inside this service (touch is only logged; audio is synthetic); a real head unit.

## Acceptance criteria

Rule: nothing is marked passed by deduction. Fields: state, evidence, date, hardware, logs, open issues.

| ID | Requirement | State | Evidence | Date | Hardware | Open issues |
|----|-------------|-------|----------|------|----------|-------------|
| AC-01 | LineageOS boots on the S10+ | NOT TESTED | – | – | none | ROM never built: host has 39 GB RAM / 189 GB disk vs 64 GB / 400 GB required (research/lineageos.md) |
| AC-02 | Own services start automatically | NOT TESTED on target | Emulator: service starts when the launcher opens and stays foreground. BOOT_COMPLETED start not tested | – | none | ConnectionService does not yet drive a native session (CP-008) |
| AC-03 | Radio detects device as CarPlay over USB | NOT TESTED | – | – | none | Role switch / VBUS back-feed on the S10 Type-C stack unknown (research/usb-feasibility.md) |
| AC-04 | Session established | NOT TESTED | iAP2 + AirPlay sequencers pass against fake peers only | 2026-11-14 | none | Protocol facts come from one third-party document measured on one car |
| AC-05 | Android video transmitted | NOT TESTED (to the radio) | Android side: VirtualDisplay + MediaCodec produce a decodable 1200x512 baseline stream on the emulator; stream passes the AirPlay framing/AES-CTR path in a host test | 2026-11-14 | Android Emulator API 36 | nothing sent over USB/network; encoder not run on S10+; phone-screen capture without consent unresolved |
| AC-06 | Audio transmitted | NOT TESTED (to the radio) | Android side: system audio captured (440 Hz verified) on the emulator; capture passes the pacing/RTP path bit-exactly in a host test | 2026-11-14 | Android Emulator API 36 | nothing sent to a radio; S10+ HAL untested; capture needs privileges; no A/V sync measurement |
| AC-07 | Touch input works | NOT TESTED (from the radio) | Android side: injected touch changed the content of a virtual display, seen in the encoded stream (emulator); HID edge/scaling unit-tested | 2026-11-14 | Android Emulator API 36 | no HID descriptor parser; no wiring CarPlay -> injector; injector needs privileges; single touch only |
| AC-08 | Launcher opens apps | PARTIAL (emulator) | tile tap launched Calendar; ordering/hiding verified by screenshot | 2026-11-14 | Android Emulator API 36 | not on S10+/LineageOS; not tested from the head unit touch screen |
| AC-09 | No need to touch the phone screen in normal use | NOT TESTED | – | – | none | needs AC-01..07 |
| AC-10 | Auto-reconnect after unplug | NOT TESTED | ConnectionPolicy backoff logic unit-tested | 2026-11-14 | none | policy only; no real USB events or session |
| AC-11 | Works with phone screen off | NOT TESTED | – | – | none | – |
| AC-12 | Documented install and recovery | PARTIAL | scripts exist; no installation.md / recovery guide; flashing steps unverified | – | none | blocked until the device model is confirmed over adb |

## Security and safety checks to keep passing

- SELinux stays enforcing; the drafted policy (`usb/GadgetManager/android/`) is not compiled against AOSP yet.
- ADB must not be exposed over the network in vehicle mode (launcher config has no such switch; ROM defaults still to be audited).
- No automatic flashing: `build.sh` refuses without `S10CAROS_CONFIRM_BUILD=1`; nothing in the repo flashes a device.
- The launcher never stores credentials. The MFi certificate/signature from the car is passed to callbacks and is not verified (see CP-005).
- Thermal and battery protections are never disabled by any script here.

## Acceptance of hardware tests (to run when hardware exists)

1. `scripts/diagnostics.sh` → confirm model is SM-G975F/SM-G975N and record identity.txt.
2. Measure VBUS with a meter before connecting the phone to a powered radio port (back-feed risk).
3. Capture a real iPhone↔radio session with usbmon/analyzer and diff against `research/carplay-protocol.md` assumptions.
4. Run AC-01..AC-12 in order; record logs under `docs/evidence/<AC-ID>/`.
