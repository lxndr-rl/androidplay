# S10CarOS validation

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

## Acceptance criteria

Rule: nothing is marked passed by deduction. Fields: state, evidence, date, hardware, logs, open issues.

| ID | Requirement | State | Evidence | Date | Hardware | Open issues |
|----|-------------|-------|----------|------|----------|-------------|
| AC-01 | LineageOS boots on the S10+ | NOT TESTED | – | – | none | ROM never built: host has 39 GB RAM / 189 GB disk vs 64 GB / 400 GB required (research/lineageos.md) |
| AC-02 | Own services start automatically | NOT TESTED on target | Emulator: service starts when the launcher opens and stays foreground. BOOT_COMPLETED start not tested | – | none | ConnectionService does not yet drive a native session (CP-008) |
| AC-03 | Radio detects device as CarPlay over USB | NOT TESTED | – | – | none | Role switch / VBUS back-feed on the S10 Type-C stack unknown (research/usb-feasibility.md) |
| AC-04 | Session established | NOT TESTED | iAP2 + AirPlay sequencers pass against fake peers only | 2026-11-14 | none | Protocol facts come from one third-party document measured on one car |
| AC-05 | Android video transmitted | NOT TESTED | Frame/AVCC/AES-CTR primitives tested; no encoder, no VirtualDisplay | – | none | MEDIA-001 not started |
| AC-06 | Audio transmitted | NOT TESTED | RTP packetizer tested only | – | none | no capture path |
| AC-07 | Touch input works | NOT TESTED | coordinate scaling + HID report edges unit-tested | – | none | HID descriptor parser missing; no injection into Android |
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
