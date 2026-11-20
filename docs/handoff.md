# Handoff: where the work stands (written 2026-11-20)

Read this first, then `docs/hardware-evidence.md` (observations 1-31, newest at the bottom). The chat history is NOT available to a new agent.

## Project in one paragraph
AndroidPlay: a Samsung Galaxy S10+ (SM-G975F, LineageOS 23.2 userdebug, serial in `adb devices`) acts as a wired-CarPlay *source* toward car radios. The user plugs it into a radio; the phone shows an Android UI through the radio. The USB mode manager is `scripts/device/androidplayd.sh` (starts at boot from `/vendor/etc/init/androidplay.rc`). Zlink5 radio: works. Chevrolet Captiva Premier 2024 radio and a Kia radio: never worked. Repo: private `lxndr-rl/androidplay`; folder is still `S10CarOS`.

## State of the phone (updated 2026-11-20, after recovery)
- RECOVERED. On 2026-11-18 a deployed `androidplayd.sh` skipped `gadget_manager bind` (the step that links `s10_carplay` into the configuration), which left the phone with no USB at all; autostart ran it at every boot. Recovery that worked: toggle USB debugging on the phone, pair wireless debugging (`adb pair <ip>:<pairing port> <code>`, then `adb connect <ip>:$(getprop service.adb.tls.port)`), deploy over that transport, restart the manager detached (`nohup sh -c "androidplayd.sh stop; sleep 4; APPLE_ID_OK=own_radio_only SOLE=1 androidplayd.sh start" &`) so a USB change cannot cut the session. Keep wireless debugging paired as a back channel whenever the boot path is changed.
- Current script: armed start with ONE bind (`START_ARMED` block: ids + strings, drop `function0` when `SOLE=1`, then `gadget_manager bind`). Bench matrix (`tests/hardware/bench_matrix.sh --no-deploy`) passes S1-S4 on this version, including a cold boot with the cable in. The bench PC runs usbmuxd (udev `39-usbmuxd.rules`), which resets any device wearing Apple ids; S2/S3 therefore judge the first answers only.
- Flags set on the phone for the Chevrolet attempt: `androidplay.car` (armed whatever the Wi-Fi), `androidplay.radio=chevrolet` + `androidplay.learn` (learning mode: one `c0 53` answer per plug episode, baseline first), `androidplay.mic` (radio microphone request, untested). Secure windows are ON by default. Remove `androidplay.learn` and `androidplay.radio` after the Chevrolet session, or when using another radio, so that its answer is not altered.
- Control screen (test aid, 2026-11-20): `scripts/deploy-phone.sh --ctl` enables `services/Control` (root app_process server on 127.0.0.1:47650, token in `.build/phone/androidplay.token`, also packaged in the APK) and the "AndroidPlay Control" activity of the CarLauncher APK; the manager reads `androidplay.cmd` (arm|adb) from it. The control server is currently ENABLED on the phone. The system notification from `cmd notification post` never showed on this ROM (no NotificationRecord is created); do not rely on it.
- Always: pull logs first (`scripts/pull-evidence.sh <label>`), never reboot before that; `scripts/preflight.sh` before a trip; the "AndroidPlay" notification (posted with `cmd notification post`) is meant to say ARMED, but I have not seen it on the screen, only the status file `androidplayd.status`.

## What is verified (bench, PC as host) and what is not
- Verified: with the cable already in at boot, the manager arms (Apple ids, one bind), recognises a PC (its SET_CONFIGURATION 0) and returns to ADB by itself (bench matrix, 2026-11-20).
- Verified: a radio that probes Apple first gets the baseline answer `01 00 00 00` to `c0 53` on the first probe.
- Measured 2026-11-18 (obs. 31 and below): repeated UDC unbind/bind or `soft_connect` cycles make the host fail to enumerate the phone (`device descriptor read/64, error -71`, low-speed fallback, `unable to enumerate`): 1 good enumeration in 10 UDC cycles, 0 in 10 soft_connect cycles. So **every extra disconnect is a risk**; the design goal is the fewest USB connects per plug. The "enumeration watchdog" (re-pulse when the UDC sits in `default`) was tried and removed because re-pulsing itself breaks enumeration.
- Seen once: UDC stuck in `default` for 17 minutes with the manager believing it was in ADB (PC saw nothing until the cable was replugged). The link log (`link:` lines in `androidplayd.log`) now records partner/data-role/udc-state/bsess/usb_online on every change.
- NOT verified anywhere: the Captiva/Chevrolet/Kia protocol after `c0 53`; the single-bind start with a real radio; the "ARMED / NOT armed" notification on screen (`cmd notification post`); learning mode on a radio; the radio microphone; secure windows on a radio; gestures (Back/Home/Recents) on a radio; Waze/Maps/Spotify audio and video through a radio.

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
