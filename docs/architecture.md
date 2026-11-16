# Duende architecture (state of 2026-11-14)

Legend: **[T-emu]** exercised on the Android emulator or host, **[T-host]** host unit/loopback tests only, **[none]** written but never run, **[gap]** not written.

## Goal path

```
Galaxy S10+  (USB-C)  ==== USB ====  CarPlay head unit (USB host)
  ConfigFS/FFS gadget "iPhone"  --> role switch --> S10 becomes USB host, radio becomes USB device  [gap: needs HW]
  iAP2 over bulk (identification, auth bursts)  --> CDC-NCM IPv6 link-local --> AirPlay session (RTSP, video, audio, touch)
```

## Components

| Component | Where | Language | State |
|---|---|---|---|
| iAP2 link + control codec, session state machine, identification driver | `services/CarPlayService/src/iap2_*` | C++17 | [T-host] vs a simulated accessory and one documented capture |
| AirPlay client: bplist, RTSP, sequencer, AES-CTR video, RTP audio, timing, touch scaling | `services/CarPlayService/src/airplay*.cpp` | C++17 | [T-host] fake receiver; [T-emu] over emulator NAT |
| Portable SHA-512 / AES-CTR | `crypto_portable.cpp` | C++17 | [T-host] vs OpenSSL |
| SessionRunner (POSIX sockets) | `session_runner.cpp` | C++17 | [T-host] loopback, [T-emu] |
| JNI bridge `libs10carplay.so` | `services/CarPlayService/android/` | C++/JNI | [T-emu] x86_64; arm64 compiled only |
| Projection (VirtualDisplay + H.264 MediaCodec) | `services/ProjectionService/` | Java | [T-emu], software encoder |
| Touch injector (app_process, INJECT_EVENTS) | `services/InputBridge/` | Java | [T-emu] as shell uid |
| System audio capture (REMOTE_SUBMIX) | `services/AudioBridge/` | Java | [T-emu] as shell uid |
| Audio pacing | `airplay_audio.cpp` | C++17 | [T-host] with real capture |
| USB gadget manager (ConfigFS, FFS blobs, role-switch handler) | `usb/GadgetManager/` | C++17 | [T-host] fake configfs tree only |
| Gadget ep0 serve loop, iAP2 over bulk endpoints, CDC-NCM, mDNS, IPv6 dialling | - | - | [gap] |
| Launcher, connection policy, config | `packages/CarLauncher/` | Java | [T-emu] launcher; policy [T-host] |
| ROM build (LineageOS 23.2, local manifest) | `manifests/`, `scripts/` | shell/XML | [none] (host too small) |
| SELinux policy, init rc | `usb/GadgetManager/android/` | .te/.rc | [none] never compiled |

## Process and privilege model (target)

- `s10gadgetd` (native, root-ish, owns ConfigFS/FFS): sets the gadget up, answers role-switch control requests, hands byte-stream fds to the sender.
- `ProjectionService` + `libs10carplay.so`: foreground service; must run as a **platform/system** component in the ROM, because (a) mirroring the real screen without a consent dialog, (b) `INJECT_EVENTS` and (c) `CAPTURE_AUDIO_OUTPUT`/REMOTE_SUBMIX are not available to ordinary apps. Today these run as the adb shell uid in tests.
- Launcher is an ordinary app (HOME). Config is `config.properties` (`ModeConfig`); vendor-partition location undecided.

## Trust boundaries and rules

- The radio is untrusted input: all RTSP/bplist/iAP2 parsers are bounds-checked and fuzz-ish tested (truncation/corruption loops), run under ASan/UBSan.
- The MFi certificate and challenge response sent by the radio are **not verified** (we are the verifier role and hold no Apple secret). A verifying policy can hook `onCertificate`/`onAuthResponse`.
- No extracted Apple keys, no MFi bypass. Identity strings (Apple VID, "iPhone") are an open legal question (`LEGAL-001`).
- SELinux must stay enforcing; adb must not be exposed over the network in vehicle mode.

## Known design risks

1. Role switch on the S10 Type-C stack while the radio supplies VBUS (back-feed risk) is unproven (`research/usb-feasibility.md`).
2. All protocol facts come from one third-party document measured on one head unit (Jaguar F-PACE / Harman).
3. Capture/inject privileges require ROM work that has not started.
