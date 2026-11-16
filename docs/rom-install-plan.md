# ROM install plan (NOT executed; superseded for day-to-day use by installation.md)

This page deliberately contains no flashing commands of its own. The only authoritative source used is the official LineageOS install page for `beyond2lte` (https://wiki.lineageos.org/devices/beyond2lte/install/), summarised in `research/lineageos.md`. Re-read the live page before acting; it changes.

## Safety gates (all must pass)

1. `scripts/diagnostics.sh` shows the model is SM-G975F, SM-G975F/DS or SM-G975N. Anything else: stop.
2. Data you care about is backed up (bootloader unlock wipes the phone).
3. A rollback exists: stock firmware for the exact model and region, plus the stock Android 12 base the wiki requires.
4. You (not an agent or script) run every flashing step. Nothing in this repository flashes a device; `build.sh` only builds, behind `ANDROIDPLAY_CONFIRM_BUILD=1`.

## Two ways to get AndroidPlay

| Path | What it needs | Status |
|---|---|---|
| A. Official LineageOS 23.2 nightly + AndroidPlay layered on top (apps/services; privileged parts as a system overlay) | the official zip (about 1.2 GB), recovery, the wiki procedure | the layer does not exist yet: privileged installation of ProjectionService/gadget daemon/SELinux is unwritten |
| B. Build the ROM from source with the local manifest | 64 GB RAM / 400 GB disk machine, `scripts/setup.sh`, `scripts/verify.sh`, `scripts/build.sh` | never run; this workstation is too small |

## AndroidPlay configuration (after boot)

`config.properties` (see `ModeConfig`): `mode=config|vehicle`, `reconnect.initial_ms`, `reconnect.max_ms`, `stable_ms`, `apps.pinned`, `apps.hidden`. Default is config mode. Invalid files fall back to safe defaults and show an error count on the launcher banner.

## Recovery

- Keep the recovery image and stock firmware from gate 3 offline.
- Download mode and recovery are reached with hardware keys (combinations in `research/lineageos.md`); they do not need the touchscreen.
- If a build misbehaves: boot recovery, reinstall the last known-good nightly (or stock) with the wiki procedure.
