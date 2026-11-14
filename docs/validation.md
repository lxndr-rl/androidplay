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
| 2 Android | services start, permissions, SELinux, capture, encode, audio, input injection | NOT TESTED (no device, no ROM build) |
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

## Acceptance criteria

Rule: nothing is marked passed by deduction. Fields: state, evidence, date, hardware, logs, open issues.

| ID | Requirement | State | Evidence | Date | Hardware | Open issues |
|----|-------------|-------|----------|------|----------|-------------|
| AC-01 | LineageOS boots on the S10+ | NOT TESTED | – | – | none | ROM never built: host has 39 GB RAM / 189 GB disk vs 64 GB / 400 GB required (research/lineageos.md) |
| AC-02 | Own services start automatically | NOT TESTED | Launcher APK builds and verifies; BootReceiver/ConnectionService never run | – | none | ConnectionService does not yet drive a native session (CP-008) |
| AC-03 | Radio detects device as CarPlay over USB | NOT TESTED | – | – | none | Role switch / VBUS back-feed on the S10 Type-C stack unknown (research/usb-feasibility.md) |
| AC-04 | Session established | NOT TESTED | iAP2 + AirPlay sequencers pass against fake peers only | 2026-11-14 | none | Protocol facts come from one third-party document measured on one car |
| AC-05 | Android video transmitted | NOT TESTED | Frame/AVCC/AES-CTR primitives tested; no encoder, no VirtualDisplay | – | none | MEDIA-001 not started |
| AC-06 | Audio transmitted | NOT TESTED | RTP packetizer tested only | – | none | no capture path |
| AC-07 | Touch input works | NOT TESTED | coordinate scaling + HID report edges unit-tested | – | none | HID descriptor parser missing; no injection into Android |
| AC-08 | Launcher opens apps | NOT TESTED | APK built; tile ordering unit-tested; UI never run | 2026-11-14 | none | could be run on an emulator image (not done) |
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
