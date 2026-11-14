# S10CarOS

Project to turn a Samsung Galaxy S10+ (`beyond2lte`) into an Android "AI box" for wired-CarPlay radios, on LineageOS 23.2.

**Status (2026-11-14): software components exist and are tested on the host and on an Android emulator. Nothing has been run on an S10+ or against a real CarPlay radio. The ROM was never built. Do not describe this as working.**

- What exists and how far each part was tested: [docs/architecture.md](docs/architecture.md)
- Acceptance criteria AC-01..12 (all untested on target) and test logs: [docs/validation.md](docs/validation.md)
- What is verified compatible: [docs/compatibility.md](docs/compatibility.md)
- Install plan (no flashing commands, safety gates): [docs/installation.md](docs/installation.md)
- First-hardware checklist: [docs/hardware-bringup.md](docs/hardware-bringup.md)
- Troubleshooting: [docs/troubleshooting.md](docs/troubleshooting.md)
- Research with sources and evidence levels: `research/`
- Agents, contracts, tasks: [AGENTS.md](AGENTS.md), [TASKS.md](TASKS.md), [STATUS.md](STATUS.md)

Run everything that can run without hardware: `scripts/run-tests.sh` (add `--strict` to fail on skipped suites).

Main blockers: no phone or radio attached so far; role-switch/VBUS behaviour on the S10 Type-C stack unknown; protocol facts come from one third-party document measured on one car; ROM-level privileges for screen capture, touch injection and audio capture not written; build host too small for a full ROM build.
