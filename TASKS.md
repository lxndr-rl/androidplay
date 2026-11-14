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
| UX-001 | E | Headless CarLauncher | – | TODO | – | – |
| QA-001 | F | Test plan + diagnostics.sh | – | TODO | – | – |

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
| CP-005 | orch | Identification/auth driver + post-auth burst on top of DeviceSession (host-simulated accessory in tests) | CP-004 | TODO | – |
| CP-006 | orch | AirPlay-over-NCM session (SETUP/RECORD, AES-CTR video, RTP audio) | CP-005 | TODO | – |
