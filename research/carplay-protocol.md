# CarPlay protocol and feasibility: S10 as the CarPlay *source* over wired USB

Author: Subagent B. Date of research: 2026-11-14. No phone or radio was attached; nothing here was tested on hardware.

Evidence levels: **verified** (I read the primary artifact myself and it states this), **code-backed** (an open-source implementation or kernel code implies it; not hardware-tested by us), **hypothesis** (plausible, secondary or inferred), **blocked** (cannot be established without Apple NDA material or hardware), **unknown**.

Fetch notes: several pages returned 403/404/521 (lore.kernel.org patch, a CSDN blog, medium article, `github.com/okcar-os/android`). Claims that rest only on search-result snippets are marked as such.

## 1. Bottom line

1. **No open and legitimate path exists** for a non-Apple device to act as a CarPlay source. Apple's MFi program licenses *accessory* makers (radio side, with an Apple authentication coprocessor). I found no program that licenses an "iPhone-role" implementation. Apple DTS says the MFi specs are under NDA (Apple Developer Forums, Nov 2024, thread 768124).
2. **No open-source Android/Linux phone-side CarPlay sender exists** in anything I found. Every open CarPlay project is a *receiver* (head-unit side) fed by a Carlinkit dongle, or a research dump of dongle firmware.
3. The sender-side authentication burden may be lighter than assumed (the MFi chip sits in the *radio*, and the phone verifies it), but this is **hypothesis**, not verified, and it does not remove the larger problems: undocumented iAP2/CarPlay messages, the USB role swap, and producing the CarPlay UI at all.
4. Feasibility verdict: **blocked on evidence, not just on effort.** An open implementation needs reverse engineering (traffic captures from a real iPhone plus real radio, or analysis of the Carlinkit firmware) that I cannot do here. Legal exposure is unresolved (see section 6).

## 2. How wired CarPlay works at USB level (what is known)

| # | Claim | Level | Source |
|---|-------|-------|--------|
| 1 | Wired CarPlay data goes over USB; everything is wrapped in IP except the iAP2 control protocol. iAP2 over USB is part of the wired and wireless flow. | verified (via search summary of Apple WWDC17 session 717 PDF; I did not open the PDF myself) | https://devstreaming-cdn.apple.com/videos/wwdc/2017/717tmz5rl6c2t2k/717/717_developing_wireless_carplay_systems.pdf |
| 2 | Radio starts as USB host, detects an Apple device supporting iAP2, requests a USB **role swap**; the Apple device then becomes host, radio becomes device, and an iAP2 session starts. | code-backed | QNX head-unit docs (RoleSwap_AppleDevice, Probe_iAP2, device `iap::2`): https://www.qnx.com/developers/docs/6.6.0.update/com.qnx.doc.dev_pub.ref_guide/topic/usblauncher_config_supported_applications.html (vendor doc, read 2026-11-14). The same 5-step sequence appears as a question (not an answer) in https://developer.apple.com/forums/thread/732990 (Jun 2023). |
| 3 | Apple VID is 0x05AC; QNX matches PIDs 0x1200 to 0x12FF for the swap. | verified (QNX doc) | same QNX page |
| 4 | iAP2-over-USB can be carried on a USB HID interface (the MFi accessory side), first bytes `FF 55 02 00` in one accessory developer's trace. Apple would not explain further (NDA). | verified as a statement of the poster; not a spec | https://developer.apple.com/forums/thread/768124 |
| 5 | A Linux kernel patch defines vendor request 0x51 on PID 0x12a8 for a "CarPlay driver"; a Chinese blog says a vendor OUT request (wValue 1) triggers the swap, stall means "stay in iPod mode", then iAP2 carries cluster/GPS and NCM carries video, audio, touch. | hypothesis (snippets only; both pages failed to fetch) | https://lore.kernel.org/lkml/1521015535.3717.26.camel@mhfsdcap03/t/ ; https://blog.csdn.net/zoosenpin/article/details/87439254 |
| 6 | Apple's private NCM interface on iPhone 0x05ac:0x12a8 exists (interfaces 2 and 4); revealed by a vendor control transfer bRequest 82, wIndex 3. This is the **developer-tools** (iOS 17 RemoteXPC) path, not shown to be CarPlay. | verified (read patch text) | https://lkml.iu.edu/hypermail/linux/kernel/2406.0/07870.html (Ole Andre Ravnas, 2024-06-07) |
| 7 | The screen/audio/touch stream is AirPlay-derived; auth uses iAP2 with the MFi chip. | hypothesis/secondary | https://9to5mac.com/2024/11/07/security-bite-mechanics-of-apple-carplay/ (TROOPERS summary; no USB detail) |
| 8 | Exact USB descriptors (interface class/subclass/protocol of the iAP2 interface, endpoint layout, whether the radio expects a specific configuration number) | **unknown** | Not found in any public source. Needs a descriptor dump of a real iPhone while connected to a CarPlay radio. |
| 9 | iAP2 link-layer packet: 9-byte header `FF 5A len(BE16) ctrl seq ack session hdrChk`, optional payload plus 1 checksum byte, checksum = (0x100 - byte sum) & 0xFF; link-sync payload with version 2 "required for CarPlay". | code-backed, single unofficial source | https://gist.github.com/JJTech0130/a878496479afedb93dd02399551e3aec (JJTech0130, 2026-07-05, no license). It talks to a real iPhone over `com.apple.carkit.service` via usbmuxd, i.e. it plays the **radio** role. Implemented in `services/CarPlayService/`. |
| 10 | Which physical mechanism lets the S10's USB-C port (dwc3 dual-role) perform the swap that a Lightning iPhone does by proprietary means | **unknown**, passed to Agent C (`research/usb-feasibility.md`) | n/a |

Consequence for architecture: toward the radio the S10 must **first appear as a USB device with Apple-style descriptors** (radio is host), answer the role-swap request, and then **become USB host** to the radio. Both halves are unvalidated.

## 3. What the radio checks (authentication)

| Claim | Level | Source |
|-------|-------|--------|
| Certified CarPlay equipment must contain an Apple authentication chip (MFi coprocessor); carmakers pay to integrate it. | verified (secondary article) | 9to5mac link above |
| The chip's private key is non-extractable without physical access; extraction is a research area. (Out of scope: we do **not** extract or reuse keys.) | secondary | same |
| Apple Platform Security: authentication IC exists "to help ensure only approved MFi accessories get full access" to the device. Direction = **accessory proves itself to the Apple device**. | verified | https://support.apple.com/guide/security/sec70a4f377d/web |
| The phone-side (iPhone role) is not itself challenged by an MFi chip: "typically only the iPod authenticates the host" and a gadget that is not authenticated by the host works. | code-backed for iAP1/iPod audio only (ipod-gadget README); for iAP2/CarPlay it is a search-summary claim, **hypothesis** | https://github.com/oandrew/ipod-gadget (MIT, last push 2025-08-15) |
| Carlinkit-style units work in unmodified wired-CarPlay cars without being Apple devices. (Indirect support for "radio does not demand an Apple-signed credential from the source".) | verified as market behaviour, mechanism **unknown**; the USENIX VehicleSec 2025 paper (Won et al.) compared such an adapter's connection against an iPhone, I could read only the abstract | https://www.usenix.org/conference/vehiclesec25/presentation/won |
| Whether the radio performs any other check of the source (AirPlay-level pairing/encryption, device-identity strings, FairPlay-like steps) | **blocked/unknown** | MFi NDA |
| Whether the Carlinkit dongles carry an MFi chip (needed for their *iPhone-facing* side, not for ours) | hypothesis; spec listings name "MFI 3959/3989", no teardown found | manuals.plus listings (low quality) |

Key distinction: the S10 has **no upstream iPhone**, so the MFi chip that dongles need toward a real iPhone is irrelevant to us. What matters is only what the radio demands of its source.

### Legitimate routes (honest list)
1. **MFi Program licence** (https://mfi.apple.com): for accessory/head-unit makers and authentication coprocessors; gives access to the iAP2 and CarPlay specs and the licensed chip. It does not authorise building an iPhone-role device. Would not fit a source-side product. **No path found** for the phone role.
2. **Licensed auth chip or licensed module**: gives you the *radio* side only.
3. **Use a real iPhone** as the source (or a dongle that bridges one): legitimate but is not "S10 as CarPlay source".
4. **Stay on open ground**: Android Auto toward the radio (if it supports it; hypothesis, open phone-side reimplementations such as AARE exist per search snippet) or a non-projection fallback (Bluetooth audio, USB audio, own UI).

We do not suggest bypass, key extraction, or copying Apple credentials.

## 4. Existing projects: comparison matrix

Checked 2026-11-14 with `gh api` unless noted. "Role" = which end of the car link it implements.

| Project | Role | Transport | Open? | License | Last push | Notes / evidence |
|---|---|---|---|---|---|---|
| rhysmorgan134/react-carplay | **Receiver** (head unit app) | USB to Carlinkit dongle | open | MIT | 2026-01-14 | https://github.com/rhysmorgan134/react-carplay ; uses node-carplay |
| gen2-Prius-MFD/node-CarPlay (node-carplay) | Receiver library | USB dongle | open | MIT | 2025-07-24, archived | https://github.com/gen2-Prius-MFD/node-CarPlay |
| electric-monk/pycarplay | Receiver library ("Autobox" dongle) | USB dongle | open | MIT | 2020-12-06 | https://github.com/electric-monk/pycarplay |
| harrylepotter/carplay-receiver | Receiver | dongle | open | MIT | 2020-11-29 | https://github.com/harrylepotter/carplay-receiver |
| f-io/LIVI (successor of pi-carplay) | Receiver (Linux/macOS/Android) | Carlinkit dongle, wired and wireless | open | GPL-3.0 | 2026-11-14 | https://github.com/f-io/LIVI ; `RGreeneRI/pi-carplay` returned 404 |
| niellun/FastCarPlay | Receiver | dongle | open | GPL-3.0 | 2026-03-30 | https://github.com/niellun/FastCarPlay |
| shihabal3amri/DiPlay | Receiver on Android head units | wired/wireless via dongle (per description) | open | GPL-3.0 | 2026-11-12 | https://github.com/shihabal3amri/DiPlay ; a social post claims a phone variant, **unverified** |
| ludwig-v/wireless-carplay-dongle-reverse-engineering | RE of Carlinkit **dongle firmware** (the dongle is the source-emulator toward the car) | n/a | firmware dumps and tools open, firmware itself is binary | GPL-3.0 | 2026-07-23 | https://github.com/ludwig-v/wireless-carplay-dongle-reverse-engineering ; most relevant lead for how the car-facing side is done. I read only the README, not the firmware |
| segfly/carlinkit-modding | RE write-up of a CPC200-U2W Plus | n/a | write-up | none declared | 2024-06-19 | https://github.com/segfly/carlinkit-modding |
| jevinskie/CarlinKit4-...-RE | RE of Carlinkit4 | n/a | one-line README | BSD-2-Clause | 2023-07-12 | https://github.com/jevinskie/CarlinKit4-CarPlay-Wired-to-WiFi-dongle-RE (empty in practice) |
| JJTech0130 `carkit_iap2.py` gist | iAP2 **radio side** talking to a real iPhone | usbmuxd/lockdown | open | none stated | 2026-07-05 | basis of our framing code |
| oandrew/ipod-gadget | **Source-side** (Linux phone-like device as iPod) | USB gadget, HID iAP + UAC1 | open | MIT | 2025-08-15 | iPod audio only, iAP1-era; tested on Nexus 5. Only open precedent for "phone-class Linux acts as Apple accessory-protocol peer" |
| BertoldVdb/WACResearch | MFi chip over I2C (WAC, not iAP2) | I2C | open | MIT | 2021-12-13 | shows the chip is just an I2C part; irrelevant to source role |
| f1xpl/openauto | Android Auto head unit (not CarPlay) | USB/wireless | open | none declared (API) | 2024-12-12 | different ecosystem; receiver |
| OkcarOS (okcar-os/android) | claimed LineageOS 13 ROM for a phone to feed a CarPlay car | unknown | claimed open | unknown | unknown | `github.com/okcar-os/android` returned **404 on 2026-11-14**; only a search snippet describes it, and it does not explain the CarPlay handshake. Agent G owns recovery |
| Carlinkit AI boxes | closed hardware; box "impersonates the phone" per vendor/reviews | USB to car | **binary** | proprietary | n/a | vendor marketing only; mechanism **unknown** |
| Pi2Carplay, CarJack/Slipstream, CarDeck | not found by search; cannot classify | n/a | n/a | n/a | n/a | state "not found", not "do not exist" |
| Tesla/Pi CarPlay hacks (tesla-carplay, wirelesscarplay) | receiver chains from a real iPhone | n/a | open | varies | n/a | not sources of CarPlay |

Takeaway: **0 open sender-side CarPlay implementations**; 1 open sender-side iAP precedent (ipod-gadget, wrong protocol generation).

## 5. Proposed architecture (conditional; all CarPlay-specific boxes are unvalidated)

```
usb/GadgetManager (Agent C)         services/CarPlayService               ProjectionService (Agent D)
 bulk/HID fds, role-swap   <---->   [A] iap2 link codec  (done, tested)    H.264 Annex-B, PCM  (unix sockets)
                                    [B] iAP2 session/ident (UNKNOWN msgs)  InputBridge: normalized touch
                                    [C] IP-over-USB (NCM) + AirPlay-like   
                                        screen/audio/touch (UNKNOWN)
```

Phases, each gated on evidence:
1. **Capture phase (needs hardware, not available now):** real iPhone plus real radio, USB analyzer or a Linux host in the middle (a usbmon capture), to learn descriptors, role-swap request, iAP2 identification, NCM and stream set-up. Without this, every box beyond [A] is guesswork.
2. **Read** the USENIX VehicleSec 2025 paper in full and ludwig-v firmware (the car-facing logic of a working source emulator) to learn what a radio accepts. Unread so far.
3. Only then decide whether to implement [B]/[C].
4. Parallel safe fallback: Android Auto or plain Bluetooth/USB audio if the radio supports it.

CarPlay UI note (general knowledge, **hypothesis**, not source-checked here): the CarPlay interface is rendered by iOS. An Android source would have to synthesise its own video and answer the radio's control messages (focus, night mode, touch). The radio shows whatever video arrives; compatibility with CarPlay-specific radio expectations is unknown.

## 6. Blockers (ranked)

1. **Proprietary, NDA-covered spec** (iAP2 session messages, CarPlay start/identification, stream setup). Public material limited to the link-layer gist. **Blocked.**
2. **What the radio verifies about its source** (see section 3); only market behaviour of dongles hints it can be satisfied. **Unknown.**
3. **USB role swap on the S10's Type-C controller** and Apple-style descriptors. **Unknown**, Agent C.
4. **No hardware** (phone, radio, USB analyzer) for capture; no capture means no verification.
5. **Legal/ToS:** impersonating an Apple device toward a CarPlay radio may breach Apple's MFi terms or trademark rules and is the Carlinkit grey area. Needs legal review before any product claim. (Hypothesis; I am not a lawyer.)
6. Source-side tooling dependent on binary Carlinkit firmware is not open, and not redistributable here.

**Single biggest blocker:** there is no public specification or open implementation of the CarPlay source side, and the only legitimate route to the specs (MFi) licenses the radio/accessory role, not the phone role.

## 7. Delivered code

`services/CarPlayService/`: C++17 iAP2 **link-layer** packet codec (encode, decode, resync stream parser) based solely on the gist in section 2 row 9. Justified because it is protocol-agnostic, tiny, and is the only layer with any public evidence. It is **not** validated against a real device; the test vectors encode the gist's own constants (the 13-byte SYN payload and checksum rule), so they check self-consistency, not correctness against Apple. Unimplemented/unknown: link negotiation, retransmission, control-session payloads, any CarPlay message.

Test run (2026-11-14, g++ 13, `-fsanitize=address,undefined`): `ALL PASSED`.
Build: `cd services/CarPlayService && g++ -std=c++17 -Iinclude src/iap2_link.cpp tests/test_iap2_link.cpp -o /tmp/t && /tmp/t` (or CMake, untested).
