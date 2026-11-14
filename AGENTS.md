# AGENTS.md — S10CarOS multi-agent contract

Orchestrator: **S10CarOS Orchestrator** (main agent). Integrates, reviews, resolves discrepancies by evidence (never by consensus).

Rules for all agents: no invented compat, no claimed untested results, cite URL + date + license for every finding, no flashing/bootloader actions, no MFi bypass/extracted keys. Work only in your own directories; hand off via versioned files.

| ID | Agent | Dirs owned | Phase | Status |
|----|-------|-----------|-------|--------|
| A | LineageOS & ROM | `research/lineageos.md`, `manifests/`, `device/`, `scripts/setup.sh build.sh verify.sh` | 1 | running (research) |
| B | CarPlay protocol & RE | `research/carplay-protocol.md`, `services/CarPlayService/` | 1 | running (research) |
| C | Kernel & USB gadget | `research/usb-feasibility.md`, `usb/`, `kernel/` | 1 | running (research) |
| D | Display/Audio/Input | `services/{ProjectionService,VideoEncoder,AudioBridge,InputBridge}/` | 2 | pending |
| E | Automotive UX/headless | `packages/` | 2 | pending |
| F | QA/Security | `tests/`, `docs/validation.md`, `scripts/diagnostics.sh` | 2 | pending |
| G | OSS recovery/research | `research/{okcaros-recovery,alternatives,source-inventory,licensing}.md` | 1 | running (research) |

## Integration contracts (draft, v0)
- **USB↔CarPlay**: CarPlayService consumes byte-stream endpoints (bulk IN/OUT fds) from `usb/GadgetManager`; GadgetManager owns ConfigFS/FunctionFS setup.
- **CarPlay↔Media**: ProjectionService emits H.264 Annex-B NAL units + PCM audio frames via local unix sockets; InputBridge receives normalized touch events (x,y in 0..1, action, pointer id).
- **Config**: `java.util.Properties` file (`config.properties`, app-private `files/` dir for now; keys in `ModeConfig`). Changed from JSON on 2026-11-14 so the pure-Java core is host-testable; vendor-partition location still TBD (SELinux).

## Conventions
C/C++17, Rust 2021, Kotlin; 4-space indent; unit-testable core with no Android deps where possible; each task in TASKS.md has evidence link.

## Required tests per agent
Level 1 unit tests mandatory for any code; Level 4/5 only with real radio/phone (see docs/validation.md).
