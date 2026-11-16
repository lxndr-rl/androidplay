# Alternatives and equivalents to OkcarOS

Agent G, 2026-11-14. All metadata from `gh api repos/<owner>/<repo>` today unless marked. Tags: [V] verified, [S] secondary, [U] unverified.
Roles: SENDER = acts as the iPhone/phone toward a CarPlay head unit (what Duende needs). RECEIVER = acts as the car/head unit. Transport: USB (wired) or wireless (BT + Wi-Fi).
Maintenance: "active" = pushed within 60 days of 2026-11-14.
Nothing here was built, run or tested by us. Compatibility with any car is only what each project claims.

## A. Sender projects (an Android or Linux device acting as the phone)

### A1. OkCarLink (Enthlinn/okcarlink): top candidate
- URL: https://github.com/Enthlinn/okcarlink . Licence MIT [V]. Created 2026-11-04, last push 2026-11-11, releases v1.0.0 (2026-11-06) to v1.0.2 (2026-11-10) plus an `aosp-deps` release asset (2026-11-04). 1.2 MB. Active, but only 10 days old [V].
- Role: SENDER. Transport: both. Wireless = iAP2 over RFCOMM + AirPlay. Wired = FunctionFS gadget presenting an Apple device (usbmux + NCM), lockdown server, iAP2 over USB [V, from README and `docs/en/architecture.md`].
- Contents: KernelSU/Magisk module; Java daemon `com.okcar.link` (iAP2, AirPlay-2 pairing, RTSP, H.264/HEVC capture, virtual display, uinput touch); two native binaries (`okcar-uinputd`, `okcar-ffs-usbmux`, C with BoringSSL). The daemon runs as a root `app_process` with hidden APIs. Docs are bilingual and describe the protocol layers [V].
- Claims: tested on a Redmi Note 12 Turbo with a real head unit's DiPlay app, wired and wireless [V as claim; U for any OEM car]. The wired lockdown identity is generated on-device (self-signed), so the repo ships no key material [V from docs]. Whether an OEM head unit accepts this is **not shown** by the repo; the claimed test target is a receiver app, not an OEM unit [U].
- Limitations: CI builds need a "dependency bundle" (framework jars + prebuilt natives) that is a binary release asset, not source. Code is hidden-API-heavy and Xiaomi-aware (`MiuiSynergyMode.java`). It needs a kernel whose USB gadget can run as a device and that supports FunctionFS. Internal names (`okcar-*`) suggest a relation to OkcarOS internal tooling, provenance unknown. Written by an author using a different handle (commits by FlowerKnows-coder, repo owner Enthlinn), 4 repos only [V].
- Reuse potential: **high** for protocol reference and daemon structure, subject to a provenance and licence check, and to a clean-room review of whether the code is derived from OkcarOS-private sources. I have not read the daemon source beyond file names and the two docs.

### A2. carjack / Slipstream (cfraz89/carjack)
- URL: https://github.com/cfraz89/carjack . Licence Apache-2.0 OR MIT (two licence files; API says Apache-2.0) [V]. Created 2026-05-13, last push 2026-11-14 (today). Releases slipstream-v0.1.0 (2026-10-24), v0.2.0 (2026-10-25), v0.3.0 (2026-11-14) [V]. Active.
- Role: SENDER. Transport: **wireless only** (BT iAP2 + AirPlay) [V from README]. Android app "Slipstream" needs Shizuku on a stock phone; a Linux app also exists.
- Contents: Rust `libcarjack` (BT iAP2, CarPlay session, AirPlay streaming), `iap2-proto`, Wireshark dissector `iap2.lua`, docs with investigation notes, Android app built with `Android.bp`. Claims clean-room work from packet captures [V as claim]. AirPlay pairing and streaming come from `cfraz89/catplay` [V].
- Verified claims: known to work on Pixel 8 and Pixel 10 with a CFMOTO 1000MT-X dash; other phones and head units untried [V as claim, not tested by us].
- Limitations: `catplay` is a repo with **no licence** (fork flag true, pushed 2026-10-17), and carjack depends on it, so carjack's effective licence is conditional [V]. Wireless-only, so it does not satisfy the wired requirement. Uses Sentry (`sentry.xml` present) [V].
- Reuse potential: **high** (protocol clean-room reference, iAP2 framing, virtual-display launcher model, Apache/MIT); resolve catplay's licence before copying any dependency.

### A3. CarDeck (shaonialife/cardeck and forks)
- URL: https://github.com/shaonialife/cardeck . Licence MIT [V]. Created 2026-10-18, last push 2026-10-24, 165 stars. At least 10 forks with the same description (fw867, zz85, wyf1463630616 and others). wyf1463630616's fork was pushed 2026-11-14 [V].
- Role: SENDER. Transport: wireless only. Its README states USB is excluded because an app cannot control the USB gadget [V].
- Contents: Android 14+ app that requires Shizuku (virtual display, touch injection, window state); iAP2 sender session, AirPlay pairing/H.264/HID return. It lists Audi MIB3 as the real-car validation (2026-10-11) [claim].
- Claims, treat as unverified [U]: that sender needs no Apple identity because "iAP2 authentication is unidirectional" and cites Oligo Security (DEF CON 2025). I did not verify the Oligo reference, and AGENTS.md forbids any MFi-bypass work. Keep as protocol background only.
- Reuse potential: **medium** (MIT, readable protocol notes `docs/PROTOCOL-NOTES.md`; Kotlin app architecture). Note: it describes **DiPlay** and **xcertplay** as its test bench (receivers). Both are GPL-3.0 as far as the DiPlay repo shows (see B).

### A4. Pi2Carplay (Fotsbeats/Pi2Carplay): wired, Raspberry Pi 5 Android
- URL: https://github.com/Fotsbeats/Pi2Carplay . Licence GPL-2.0 for project-authored `src/`+`scripts/` per `LICENSE-SCOPE.md` [V]. Created 2026-10-28, last push 2026-10-30, releases v0.1.0 (2026-10-28), v0.2.0 (2026-10-30) with a flashable `.img.gz` [V]. 286 MB (mostly `src/kernel`, 58k files).
- Role: SENDER. Transport: **wired USB only** (Pi 5 USB-C to the car's CarPlay data port) [V].
- Contents: Android launcher, capture/encoder, audio, calling (Bluetooth HCI), sidebar and rail overlays, bridge adapter C code and tests, a kernel tree for Raspberry Pi 5, build scripts [V].
- **Blocking limitation**: the "protocol SDK" (the actual CarPlay stack) is an **ARM32 binary-only dependency**: the repo says its implementation source is not included, and docs refer to a "supplied binary" with a SHA-256 and `carplay_reverse_*` symbols, which implies a blob taken from a commercial AI box (supplier unknown) [V from docs, origin inference U]. `LICENSE-SCOPE.md` also states: the Android base is KonstaKANG AOSP16 for Pi 5 with CC BY-NC-SA 4.0 components; "Distribution review is still pending before any public upload" even though the repo and releases are public [V].
- Reuse potential: **low for code reuse** (SDK is a blob of unknown provenance; NC-SA base). **High for design reference**: display negotiation (`src/bridge/DISPLAY-NEGOTIATION.md`), safe-area handling, split screen, call audio, with its open GPL-2.0 adapter code. The two other sources it relies on were not examined.

### A5. OrangeCar (sbuysse/orangecar): wired, Orange Pi 5 Plus
- URL: https://github.com/sbuysse/orangecar . Licence GPL-3.0 [V]. Created and last pushed 2026-10-10, 2.8 MB, 10 stars [V]. No releases checked.
- Role: SENDER. Transport: **wired USB** (USB role switch to host after the car's request, iAP2 over USB, CDC-NCM tunnel with IPv6 link-local, mDNS, AirPlay session) [V from `docs/protocol.md`].
- Contents: Rust workspace (`carplay/src` implements the byte-level steps, `agent`, `app`), a web dashboard, `THIRD_PARTY.md`. `docs/protocol.md` is the best public step-by-step spec of the wired sender handshake I found: USB descriptor phase, role switch, iAP2 link and identification, certificate and challenge messages, NCM, mDNS, AirPlay SETUP/RECORD, timers [V].
- Claims: runs in a Jaguar F-PACE (Harman InControl, legacy receiver, 1200x512) in daily use; one car only [V as claim]. States "we accept blindly, no MFi chip" in its step list [V text]. Treat the authentication claim as the project's own, [U] for us.
- Reuse potential: **high as a specification** (documentation under GPL-3.0 can be cited; code is GPL-3.0). Strong match for Agent B (protocol) and Agent C (role switching on dwc3). Linux/Orange Pi based, not Android, so reuse in an Android ROM would be a port or a clean-room reimplementation.

### A6. CarDroid (pavunato/CarDroid): flagged
- URL: https://github.com/pavunato/CarDroid . **No licence** [V]. Created 2026-10-26, last push 2026-11-08, releases v0.1.0 to v0.1.3 [V]. Role SENDER, wireless (BT then Wi-Fi) [V].
- Contents: Kotlin Android app (`com.cardroid.app`): launcher, two-panel dashboard, Shizuku shell display helper, iAP2 and AirPlay implementation, Telegram support group, Firebase Crashlytics. Docs: PROTOCOL.md, CAPTURE.md, etc. [V].
- Flag: code search shows classes named `OkcarMfiSapImpl`, `OkcarBypassProvider` and `com.okcar.tool.encrypter.*`. I did not open these files. AGENTS.md forbids MFi-bypass and extracted keys; **exclude CarDroid from reuse** and from further reading until the orchestrator decides.
- Reuse potential: none until licence and provenance are known.

### A7. OkcarOS itself
See `okcaros-recovery.md`. Role SENDER, wired USB (the Apple-device gadget). Source: partial and now unreachable at origin; engine binary-only.

## B. Receiver projects (head-unit or dongle role; test benches and protocol references for us, not Duende code)

| Project | URL | Licence | Last push | Notes |
|---|---|---|---|---|
| DiPlay | https://github.com/shihabal3amri/DiPlay | GPL-3.0 | 2026-11-12 | Independent CarPlay receiver for compatible Android head units, wired and wireless; used as a test bed by CarDeck and OkCarLink [V; contents not inspected] |
| DiPlay-MG4 | https://github.com/fatihdonmezdev/DiPlay-MG4 | GPL-3.0 | 2026-11-10 | MG4 adaptation of DiPlay [V] |
| AndroidPlay | https://github.com/Roylyl/AndroidPlay | GPL-3.0 (README says GPLv3 / AGPLv3 mix) | 2026-10-17 | Wireless CarPlay **receiver** app for Android phones and head units (hotspot + BT). Sister repos WinPlay, MacPlay (USB or Wi-Fi), LinuxPlay, all GPL-3.0, pushed Oct 2026 [V]. Useful as a receiver to test our sender against |
| PlayThing | https://github.com/tatujan/PlayThing | GPL-3.0 | 2026-10-23 | Android 9 on a Spotify Car Thing as a wired CarPlay receiver [V from search description] |
| AllPlay | https://github.com/UnmetDecade/AllPlay | GPL-3.0 | 2026-11-14 | CarPlay receiver (wireless + wired), multi-protocol [V from search description] |
| carcast-public | https://github.com/rPlayAI/carcast-public | NOASSERTION | 2026-11-13 | Receiver for macOS/Windows/iOS/iPadOS [V from description; licence not stated] |
| react-carplay | https://github.com/rhysmorgan134/react-carplay | MIT | 2026-01-14 | Carlinkit dongle client UI (Electron/React); 897 stars [V] |
| node-CarPlay | https://github.com/rhysmorgan134/node-CarPlay | MIT | 2025-06-08 | Node.js USB driver for Carlinkit dongles; documents the dongle message protocol (not the iPhone-to-car protocol). Forks: cphacks/node-CarPlay (2026-08-26) [V] |
| pycarplay | https://github.com/electric-monk/pycarplay | MIT | 2020-12-06 | First public reverse-engineering of the Autobox dongle protocol [V]. Unmaintained |
| carplay-receiver | https://github.com/harrylepotter/carplay-receiver | MIT | 2020-11-29 | Linux receiver for Autobox dongles [V]. Unmaintained |
| carplay-client | https://github.com/rayphee/carplay-client | **none** | 2022-05-20 | Rust client for Autobox dongles [V]. No licence |
| FastCarPlay | https://github.com/niellun/FastCarPlay | GPL-3.0 | 2026-03-30 | Linux CarPlay/Android Auto receiver for Carlinkit dongles on Pi [V] |
| WirelessCarPlay (Pi) | https://github.com/45clouds/WirelessCarPlay | MIT | 2024-06-24 | Pi that relays an iPhone's BT/Wi-Fi into the car's wired CarPlay USB port. Not Android as a source [V; 1090 stars] |

The Carlinkit/Autobox protocol docs (pycarplay, node-CarPlay) describe the **dongle-to-host** framing (USB bulk, 16-byte header, magic 0x55AA55AA), not the iPhone-to-car protocol. They are not directly usable for sending CarPlay from a phone; they are useful background because the head-unit AI-box market uses them [V that the repos exist; framing detail from memory, [U]].

## C. Android Auto, screen-mirroring and generic building blocks

| Project | URL | Licence | Last push | Role / notes |
|---|---|---|---|---|
| scrcpy | https://github.com/Genymobile/scrcpy | Apache-2.0 | 2026-11-12 | Virtual-display and input-injection reference (`--new-display`); Android 11+ [V]. 154k stars, very active. Its server uses hidden APIs through `app_process`; the same technique as OkCarLink/CarDeck |
| OpenAuto | https://github.com/f1xpl/openauto | none (API) | 2024-12-12 | Android Auto head-unit emulator (RECEIVER, wired and wireless). The maintained fork https://github.com/opencardev/openauto is GPL-3.0 (pushed 2026-02-10) [V] |
| aasdk | https://github.com/opencardev/aasdk | none (API) | 2026-06-11 | Android Auto protocol library for emulators. Licence not shown by API; upstream aasdk was GPL-3 [U] |
| AACS | https://github.com/tomasz-grobelny/AACS | GPL-3.0 | 2024-02-29 | Android Auto **server** for Linux (acts as the phone toward an AA car): a SENDER equivalent for Android Auto, USB gadget based [V] |
| OpenAA | https://github.com/manum45/OpenAA | GPL-3.0 | 2026-08-16 | Experimental Android app that talks to an AA head unit (SENDER, AA only). README lists okcaros [V] |
| WirelessAndroidAutoDongle | https://github.com/nisargjhaveri/WirelessAndroidAutoDongle | MIT | 2025-07-27 | Pi that relays wireless AA over wired USB [V] |
| openDsh/dash | https://github.com/openDsh/dash | GPL-3.0 | 2024-10-25 | Qt head-unit UI with Android Auto [V] |
| CarProjection | https://github.com/aa112901/CarProjection | none | 2024-05-08 | Baidu CarLife sender from Android. Same "phone as sender" idea for a different protocol. Chinese head units only [V] |

## D. Not found or not verifiable

- "Pi2Carplay" as a separate older project: only Fotsbeats/Pi2Carplay (Oct 2026) found. `f-io/pi-carplay` returned 404 [V].
- "CarJack/Slipstream": the same project (A2). CarDeck is A3. Nothing else under these names [V].
- Dedicated "Android as CarPlay source via ConfigFS in a LineageOS tree" other than OkcarOS: none found. OkCarLink wired mode is the closest userspace equivalent.
- GitLab, Codeberg, Telegram, Discord: no searchable primary OkcarOS presence found [U].
- XDA: the OkcarOS thread was reachable only through a search snippet.

## E. Comparison for Duende requirements

| Need (S10+, wired, LineageOS) | OkcarOS | OkCarLink | carjack | CarDeck | Pi2Carplay | OrangeCar |
|---|---|---|---|---|---|---|
| Wired USB sender | yes (kernel + closed app) | yes (FunctionFS, userspace) | no | no | yes (binary SDK) | yes (Linux, Rust) |
| Source for the protocol stack | **no** | yes (MIT) | yes (Apache/MIT, catplay unlicensed) | yes (MIT) | **no** (ARM32 blob) | yes (GPL-3.0) |
| Runs on Android | yes | yes (root module) | yes (Shizuku) | yes (Shizuku) | yes (Pi image) | no |
| Exynos 9820 evidence | init/sepolicy only | none (needs device-mode UDC + FFS) | n/a | n/a | n/a | n/a |
| Age / activity | frozen Apr 2024, org removed | 10 days old | 6 months, active | 4 weeks | 2 weeks | 5 weeks |
| Real-car evidence (claimed) | many reports | receiver app only | CFMOTO dash | Audi MIB3 | unspecified vehicle | Jaguar F-PACE |

All "real-car" entries are project claims; none was verified by us.

## D. How the existing "Android box" style projects handle the wired link (orchestrator read, 2026-11-14)
Sources: `research/okcaros-recovery.md` (4.3) and `docs/en/architecture.md` of https://github.com/Enthlinn/okcarlink (MIT, fetched today; sections 1, 7.3). I read the docs only, not the daemon source. Tags: [V] = stated in those docs, [U] = not verified.

Two different wired models exist; they are **not the same thing**:

| | Model 1: car runs the phone-side handshake (OrangeCar, `research/carplay-protocol.md`) | Model 2: head unit speaks usbmux to an "iPhone" (OkCarLink wired mode, OkcarOS) |
|---|---|---|
| Who is USB host | head unit first, then a **role swap** makes the phone the host (vendor requests C0 53 / 40 51) | head unit stays host; the phone is a **device in every mode** [V: "plays the device role in every mode"] |
| Phone presents | VID 05AC / PID 12A8, iAP2 over USB | Apple device: usbmux interface + NCM; host sends 16-byte usbmux frames, version handshake, TCP-over-usbmux to the lockdown port [V] |
| Authentication seen | iAP2 identification/auth (phone is the verifier) | lockdown `StartSession` with TLS (phone-generated self-signed identity), then `StartService com.apple.carkit.service` returning a **raw iAP2 byte pipe** [V] |
| Measured on | one Jaguar/Harman unit (QNX) | a head-unit DiPlay app (claim; OEM units not shown) [U] |
| Kernel needs | dwc3 role switch (unmeasured on S10+: PC refused DR_Swap) | none beyond FunctionFS: FFS + NCM from userspace, kernel unmodified [V] |

Consequences for Duende (analysis, [U] until the radio is captured):
- Generic Android head units (Allwinner and similar) usually run a vendor CarPlay app on Android that talks to an iPhone through an **Android-hosted usbmux/lockdown stack**; if that holds for the user's radio, Model 2 applies and the role swap is not needed. This is a hypothesis; the radio's CarPlay stack is unidentified.
- Our measured FFS path (bulk loopback verified on the S10+) is the transport Model 2 needs. Missing: usbmux framing, lockdown (plist over mux, TLS server), `com.apple.carkit.service`, iAP2 on the pipe, NCM.
- Both models still need the Apple VID/PID (hosts match by it): LEGAL-001 stays open and is the user's call.
- OkCarLink is flagged in section A1 (provenance unknown, internal `okcar-*` names). Use as a specification to cross-check, write our own code, do not copy.
- OkcarOS's kernel route (`f_okcar_*` + `/dev/okcar` mode switch in dwc3 glue) is the heavier alternative; not needed if Model 2 is right.
