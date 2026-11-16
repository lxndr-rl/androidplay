# Compatibility (2026-11-14)

Only facts with evidence. "Unknown" is the honest default.

## Device

| Item | Status | Evidence |
|---|---|---|
| SM-G975F / SM-G975F/DS / SM-G975N (`beyond2lte`, Exynos 9820) | supported by official LineageOS 23.2 | wiki + download API, research/lineageos.md (read, not flashed) |
| Snapdragon S10+ variants (SM-G975U/W/0) | **not supported** by these procedures | not investigated; do not use |
| Your phone's real model | **UNVERIFIED** (no device was ever attached) | run `scripts/diagnostics.sh` |

## Android/LineageOS

| Item | Status |
|---|---|
| lineage-23.2 (Android 16), kernel 4.14.356 | official; nightlies weekly (latest seen 2026-11-14) |
| lineage-24.0 | exists as a branch; no vendor blobs, no builds: unusable |
| USB gadget in kernel | ConfigFS, FunctionFS, ACM/NCM/RNDIS/ACC enabled; HID, UAC, ECM not set (code reading only) |
| Samsung firmware base | stock Android 12 required by the wiki |

## Head units (CarPlay over USB)

| Head unit | Result |
|---|---|
| Jaguar F-PACE InControl Touch Pro (Harman, AirPlay srcvers 280.33.12.1) | the *protocol document* we rely on was measured on it, by a third party, with a board (not an Android phone) |
| Any other radio | **UNKNOWN**. Newer radios may require things the document marks as unobserved (encryption/pairing, 466 answers); the sender does not implement paired (ChaCha20) mode. |

## Android versions tested for the Duende components

| Component | Tested on |
|---|---|
| Launcher, projection, injector, capture, JNI sender | Android Emulator, API 36, x86_64 (Google APIs Play Store image) |
| Everything above on the S10+ / LineageOS 23.2 | not tested |
| Hidden-API reflection (InputManagerGlobal, AudioRecord.Builder.setContext) | API 36 only; may break elsewhere |

## Build host

| Need | This workstation | Verdict |
|---|---|---|
| RAM for LineageOS 21+ | 39 GB | below the wiki's 64 GB |
| Disk | 189 GB free | below 400 GB |
| Option | build elsewhere, or layer on the official nightly | open (ROM-004) |
