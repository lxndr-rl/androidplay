# TASKS.md

Status: TODO / RUNNING / DONE(evidence) / BLOCKED. Initial seed; agents append evidence.

| ID | Owner | Objective | Deps | Status | Evidence | Next |
|----|-------|-----------|------|--------|----------|------|
| ROM-001 | A | Verify LineageOS branch for beyond2lte (version, maintenance, kernel, blobs) | – | RUNNING | research/lineageos.md | review |
| ROM-002 | A | Verify real device model (needs phone on adb) | phone | BLOCKED (no device attached) | `adb devices` empty 2026-11-14 | user connects phone |
| ROM-003 | A | Install build deps; assess disk/RAM for AOSP | ROM-001 | TODO | host: 39GB RAM, 189GB free | scripts/setup.sh |
| USB-001 | C | Feasibility: Exynos 9820 gadget/ConfigFS/FunctionFS for CarPlay USB role | – | RUNNING | research/usb-feasibility.md | review |
| USB-002 | C | Gadget script + host-side descriptor tests | USB-001 | TODO | – | – |
| CP-001 | B | Survey CarPlay-over-USB impls; role (accessory vs host), auth (MFi) | – | RUNNING | research/carplay-protocol.md | review |
| CP-002 | B | Decide legit auth path | CP-001 | TODO | – | – |
| CP-003 | B | Reusable service core + serialization tests | CP-001 | TODO | – | – |
| RES-001 | G | Recover OkcarOS sources/inventory | – | RUNNING | research/okcaros-recovery.md | review |
| MEDIA-001 | D | VirtualDisplay+MediaCodec H.264 service | ROM-001 | TODO | – | – |
| UX-001 | E | Headless CarLauncher | – | TODO | – | – |
| QA-001 | F | Test plan + diagnostics.sh | – | TODO | – | – |
