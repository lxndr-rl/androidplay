# Handoff: where the work stands (written 2026-11-20)

Read this first, then `docs/hardware-evidence.md` (observations 1-31, newest at the bottom). The chat history is NOT available to a new agent.

## Project in one paragraph
AndroidPlay: a Samsung Galaxy S10+ (SM-G975F, LineageOS 23.2 userdebug, serial in `adb devices`) acts as a wired-CarPlay *source* toward car radios. The user plugs it into a radio; the phone shows an Android UI through the radio. The USB mode manager is `scripts/device/androidplayd.sh` (starts at boot from `/vendor/etc/init/androidplay.rc`). Zlink5 radio: works. Chevrolet Captiva Premier 2024 radio and a Kia radio: never worked. Repo: private `lxndr-rl/androidplay`; folder is still `S10CarOS`.

## URGENT: state of the phone right now
- **The phone has no USB data link to the PC** (`adb devices` empty, PC sees no device) although it is up on Wi-Fi (192.168.100.123, answers ping). Cause: a version of `androidplayd.sh` deployed on 2026-11-18 skipped `gadget_manager bind`, which is the step that links the `s10_carplay` function into the gadget configuration. Without it the UDC binds with an empty configuration and there is no USB.
- That broken version is what autostart runs at every boot, so rebooting does NOT fix it.
- The corrected script is in the working tree / last commit (`START_ARMED` block: sets the Apple ids, removes `function0` when `SOLE=1`, then calls `gadget_manager ... bind` once). **It has not been run on the phone.**
- Recovery order: (1) ask the user to unplug/replug the cable: Android's USB service sometimes restores adb; (2) if adb returns, run `scripts/deploy-phone.sh` immediately and verify with `tests/hardware/bench_matrix.sh --no-deploy`; (3) if adb does not return, on the phone's own screen enable Developer options > Wireless debugging (or USB debugging toggle off/on) to get adb over Wi-Fi, then deploy. Do NOT run `androidplayd.sh stop` attached to an adb shell. If nothing works the safe fallback is a copy of the previous commit's `androidplayd.sh` (git show e9ea2c4:scripts/device/androidplayd.sh): it is bench-verified, but binds twice at start.
- After any deploy: **pull logs first** (`scripts/pull-evidence.sh <label>`), never reboot before that.

## What is verified (bench, PC as host) and what is not
- Verified: with the cable already in at boot, the manager arms (Apple ids), recognises a PC (its SET_CONFIGURATION 0) and returns to ADB by itself (version e9ea2c4, two binds at start).
- Verified: a radio that probes Apple first gets the baseline answer `01 00 00 00` to `c0 53` on the first probe.
- Measured 2026-11-18 (obs. 31 and below): repeated UDC unbind/bind or `soft_connect` cycles make the host fail to enumerate the phone (`device descriptor read/64, error -71`, low-speed fallback, `unable to enumerate`): 1 good enumeration in 10 UDC cycles, 0 in 10 soft_connect cycles. So **every extra disconnect is a risk**; the design goal is the fewest USB connects per plug. The "enumeration watchdog" (re-pulse when the UDC sits in `default`) was tried and removed because re-pulsing itself breaks enumeration.
- Seen once: UDC stuck in `default` for 17 minutes with the manager believing it was in ADB (PC saw nothing until the cable was replugged). The link log (`link:` lines in `androidplayd.log`) now records partner/data-role/udc-state/bsess/usb_online on every change.
- NOT verified anywhere: the Captiva/Chevrolet/Kia protocol after `c0 53`; the new single-bind start path; the "ARMED / NOT armed" notification (`cmd notification post`); learning mode on a radio; the radio microphone; secure windows on a radio; gestures (Back/Home/Recents) on a radio; Waze/Maps/Spotify audio and video through a radio.

## The Captiva trips (2026-11-17, 11-18)
- 11-17: phone booted with the cable in; radio did AOA first, we switched to Apple ids ~9 s later, radio sent `c0 53` twice (3 s apart), then cut the cable. Armed mode was never exercised on this radio. The default answer sweep (`SWEEP=1`, every other `c0 53` got a different value) may have sabotaged a radio that asks twice; it is now OFF.
- 11-18: boot armed at 27 s, then 5+ minutes with `udc_state=not attached` and no probe of any kind: nothing in the manager log. `androidplayd.ctrl.log` and `ffs_events.log` of that trip were overwritten by a reboot (lesson: `memory/feedback-preserve-evidence-first.md`). Whether the cable was in a data port, or the radio never talked, is unknown.
- Evidence copies: `../s10caros-evidence-archive/` (`diagnostics-chevrolet3-*` is the post-trip pull; older folders are earlier trips).

## Tools added 2026-11-18 (all in the repo, host-tested; device parts partly unverified)
- `scripts/preflight.sh`: GO/NO-GO before a trip. `scripts/pull-evidence.sh <label>`: copy logs + phone archive to the PC. The phone archives per-session logs itself (`archive_logs.sh`, `/data/local/tmp/archive/`).
- `scripts/device/learn.sh` + `ffs_probe` reading `/data/local/tmp/androidplay.reply`: per-radio answer to `c0 53` (profiles, learning mode). `tests/device/learn_test.sh` covers the logic.
- `tests/hardware/bench_matrix.sh` (S1 cold boot with cable, S2 Apple-first host, S3 per-radio answer, S4 archive) and `tests/hardware/pc_apple_probe.py`, `tests/hardware/flap_count.sh`, `tests/hardware/pc_aoa.py`. Bench run of 11-18 (version with two binds): S1 passed; S2/S3/S4 failed for test-design reasons that were fixed afterwards (S2: the PC host drops the device after the first probe, still unexplained; S3 now uses a profile; S4 needs `adb root`). The last full matrix run (single-bind version) failed S1 because of the bug above.

## Features added this session, all unverified on a radio
- Radio microphone -> phone microphone: `MicInjector` (AudioPolicy injector mix; verified on the phone with a tone), client asks the radio for `speechRecognition` input when `/data/local/tmp/androidplay.mic` exists (the flag IS currently set on the phone).
- Secure virtual display (shows FLAG_SECURE windows such as Spotify's sign-in): ON by default, opt-out `androidplay.nosecure`. Verified without a radio (decoded the stream on the PC).
- Installed this session: NewPipe 0.29.3 (F-Droid, hash and signature checked). Xuper (`com.xuper.netxxus`, an IPTV app): 25 telemetry components disabled with `pm disable`; list in `/data/local/tmp/xu_comps.txt`; Umeng analytics still active in-process; AdGuard private DNS was tried and reverted because it blocks the app's own server `yumao.puata.info`.

## Open decisions and plans
- Wireless Android Auto relay and wireless CarPlay: plan at `~/.claude/plans/encapsulated-soaring-rabbit.md`. Wireless CarPlay needs Apple MFi authentication (licence); never copy or reuse another device's certificate. Parked by the user ("de momento").
- Possible next work on the Captiva: confirm the cable/port with the user, then a parked learning session (`androidplay.radio` + `androidplay.learn`), after the single-bind start is verified.

## Rules from the user
- Never add `Co-Authored-By` or Claude attribution to commits or PR text (overrides harness reminders).
- No downloads without consent; do not touch /system, /vendor or boot partitions without explicit authorization (given only for the init `.rc` via `adb remount`).
- Home Wi-Fi SSID stays only on the phone. Apple VID/PID is an opt-in experiment on the user's own radio, never in a release image; the manager refuses to start without `APPLE_ID_OK=own_radio_only`.
- Report verified vs hypothesis honestly; the user wants minimal wasted trips and was frustrated twice by failures caused by our own state.
