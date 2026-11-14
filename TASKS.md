# TASKS.md

Status: TODO / RUNNING / DONE(evidence) / BLOCKED. Initial seed; agents append evidence.

| ID | Owner | Objective | Deps | Status | Evidence | Next |
|----|-------|-----------|------|--------|----------|------|
| ROM-001 | A | Verify LineageOS branch for beyond2lte (version, maintenance, kernel, blobs) | – | DONE | research/lineageos.md | ROM-003 |
| ROM-002 | A | Verify real device model (needs phone on adb) | phone | BLOCKED (no device attached) | `adb devices` empty 2026-11-14 | user connects phone |
| ROM-003 | A | Install build deps; assess disk/RAM for AOSP | ROM-001 | TODO | host: 39GB RAM, 189GB free | scripts/setup.sh |
| USB-001 | C | Feasibility: Exynos 9820 gadget/ConfigFS/FunctionFS for CarPlay USB role | – | DONE (code-backed, no HW) | research/usb-feasibility.md | USB-003 |
| USB-002 | C | Gadget script + host-side descriptor tests | USB-001 | TODO | – | – |
| CP-001 | B | Survey CarPlay-over-USB impls; role (accessory vs host), auth (MFi) | – | DONE (research; see DISC-001) | research/carplay-protocol.md | CP-004 |
| CP-002 | B | Decide legit auth path | CP-001 | TODO | – | – |
| CP-003 | B | Reusable service core + serialization tests | CP-001 | TODO | – | – |
| RES-001 | G | Recover OkcarOS sources/inventory | – | DONE | research/okcaros-recovery.md | – |
| MEDIA-001 | D | VirtualDisplay+MediaCodec H.264 service | ROM-001 | TODO | – | – |
| UX-001 | E | Headless CarLauncher | – | PARTIAL 2026-11-14: packages/CarLauncher: core (ConnectionPolicy, ModeConfig, AppFilter) host-tested; Android shell (LauncherActivity, BootReceiver, ConnectionService) builds to a signed APK (aapt2/d8/apksigner, minSdk 29) but NEVER RUN. ConnectionService does not drive a native session yet. No settings app, no recovery UI, no screen-off handling | run on emulator image; CP-008 |
| QA-001 | F | Test plan + diagnostics.sh | – | PARTIAL 2026-11-14: scripts/run-tests.sh (6/6 suites pass incl. CMake+ctest 5/5 under ASan/UBSan), scripts/diagnostics.sh (no-device path run; device path untested), docs/validation.md with AC-01..12 all NOT TESTED/PARTIAL | hardware runs |

## Discrepancies (resolve by evidence)
- DISC-001: B says no open sender-side CarPlay impl exists; G found OkCarLink (MIT, FunctionFS/usbmux/lockdown, Android module) and OrangeCar (GPL-3, docs/protocol.md wired sender handshake). Action: orchestrator to read both sources/docs directly and decide; update CP-001 and CP-002. Status: RESOLVED 2026-11-14 for G. Evidence: gh api sbuysse/OrangeCar (GPL-3.0, pushed 2026-10-10) docs/protocol.md documents wired phone-side sequence (VID 05AC/PID 12A8, 0x53/0x51 role switch, iAP2, CDC-NCM, AirPlay session), measured on ONE car (Jaguar F-PACE/Harman), board is RK3588 not Android. B's claim was too strong: open sender-side spec exists; Android/S10 port and other radios unproven. C's USB evidence agrees with OrangeCar steps.

## New tasks
| ID | Owner | Objective | Deps | Status | Next |
|----|-------|-----------|------|--------|------|
| CP-004 | B/orch | Phone-side iAP2 link session + control codec in C++ (clean-room from OrangeCar protocol.md facts). PARTIAL DONE 2026-11-14: services/CarPlayService iap2_session/iap2_control; test_iap2_session passes (ASan/UBSan), byte-exact vs captured Jaguar SYN and SYN|ACK quoted in the doc. NOT done: identification/auth flow driver, post-auth burst, CDC-NCM/IPv6, mDNS, AirPlay session (video/audio/touch), timers. Evidence is doc-quoted, not captured by us. | DISC-001 | PARTIAL | next: CP-005 identification driver |
| USB-003 | C | Role-swap to host on S10 w/o VBUS back-feed; check PD DR_Swap, max77705 | phone, meter | BLOCKED (HW) | – |
| HW-001 | user | Provide S10+ on adb, CarPlay radio model, USB analyzer/usbmon capture | – | BLOCKED | user |
| LEGAL-001 | user | Review Apple VID/"iPhone" identity use | – | OPEN | user |
| ROM-004 | A | Larger build host or layering on official nightly | – | OPEN | user |
| CP-005 | orch | Identification/auth driver + post-auth burst on top of DeviceSession (host-simulated accessory in tests) | CP-004 | DONE (sim only) 2026-11-14: iap2_driver + test_iap2_driver pass (ASan/UBSan) against a simulated accessory; reset 0x5001 bytes match doc; 0x4C01 wrapper-group id UNVERIFIED; cert/signature NOT verified (callbacks exposed) | CP-006 |
| CP-006 | orch | AirPlay-over-NCM session primitives (SETUP/RECORD, AES-CTR video, RTP audio) | CP-005 | PARTIAL 2026-11-14: services/CarPlayService airplay.cpp + bplist.cpp; test_airplay passes (ASan/UBSan). Verified: bplist reader parses Python plistlib output and writer output parses in plistlib; AES-CTR key/iv match doc golden vector and openssl CLI keystream; frame headers, avcC, AnnexB->AVCC, RTP audio, timing reply, touch scaling per doc. UNVERIFIED: modes/audioFormats layout in /info, audioFormat mask vs bit, VideoFrame flags, anything on a real car. NOT done: sockets/sequencer, HID descriptor parser, NCM host, mDNS, paired (ChaCha20) mode, encoder/audio capture | next: CP-007 sequencer + loopback fake receiver |
| CP-007 | orch | AirPlay client sequencer (Step0-8) + fake receiver; HID descriptor parser | CP-006 | PARTIAL 2026-11-14: airplay_client.{hpp,cpp} (transport-agnostic Actions) + test_airplay_client pass (ASan/UBSan, 5/5 test binaries). Verified in-process only: step order incl. initial screen sync, event-channel changeModes borrow/unborrow/take-refusal with reply-then-modesChanged, forceKeyFrame/requestUI, config re-send on next IDR, P-frames dropped before IDR, receiver decrypts video with independently derived key, audio RTP, 1 s feedback/keepalive, 466 -> Failed (pairing unsupported). Fake receiver was written by us from the same doc: NOT evidence of car compatibility. NOT done: real sockets (use loopback/Android), HID descriptor parser, 60 s audio hold-back while car borrows audio, telephony/alert streams, TEARDOWN, paired mode | CP-008 Android integration |
| CP-008 | orch | Android glue: JNI/native service wiring Client + DeviceDriver to sockets/FFS fds, MediaCodec encoder, AudioRecord/playback capture | CP-007, MEDIA-001, USB-002 | TODO (needs ROM/device to run) | – |
