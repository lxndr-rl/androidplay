# Hardware bring-up checklist

Everything here is **to be done**; no step has been executed. Order matters: stop at the first failed gate and fix it. Each gate names the acceptance criterion (AC) and the artefact to record under `docs/evidence/<AC>/`.

## Gate 0: before touching anything

1. Attach the phone over adb, run `scripts/diagnostics.sh`; `identity.txt` must show `SM-G975F*` or `SM-G975N*` (record `ro.boot.bootloader`).
2. Back up everything you need; unlocking the bootloader wipes the phone (wiki) and has warranty consequences on Samsung devices (not verified here).
3. Damaged touchscreen: plan input before you start. Hardware keys drive Download mode and Lineage Recovery (wiki key combos in `research/lineageos.md`); the Android setup wizard and the OEM-unlock toggle normally need touch. Options to evaluate, none tested: USB-C hub with a mouse (OTG), `scrcpy` over adb *if* adb is authorised, a `userdebug` build that skips adb authorisation (weakens security: lab use only).

## Gate 1: ROM (AC-01, AC-02, AC-12)

1. Decide the build path (ROM-004): official nightly + layering, or build elsewhere (64 GB RAM / 400 GB disk).
2. Follow the LineageOS wiki install page for `beyond2lte` exactly (stock Android 12 base, recovery via Download mode, sideload). Write down every deviation.
3. Record a rollback path first: stock firmware package for your exact model/region, and a known-good recovery image.
4. After boot: `diagnostics.sh` again; `getenforce` must say Enforcing.

## Gate 2: USB gadget on the bench, no radio (AC-03 prerequisites)

1. `scripts/usb-probe.sh`: list UDCs, configfs state, Type-C roles.
2. Run `usb/GadgetManager` against the real configfs with a **PC as host**; `lsusb -v` must show the expected descriptors (VID 05AC, 4 configurations as documented). Capture with `usbmon`.
3. Check whether the Android USB HAL reclaims the gadget (`--takeover g1` path).

## Gate 3: learn from a real iPhone (AC-03/04 unknowns)

Using your **own** iPhone and radio and a USB analyzer or a Linux host in the middle (usbmon / Wireshark):
1. Capture enumeration, the `C0 53` and `40 51` requests, the bus re-enumeration, iAP2 SYN and the identification messages.
2. Diff against `research/carplay-protocol.md` and the `FfsInterface` placeholders in `usb/GadgetManager` (class/subclass/protocol, endpoints, configuration number are unknown today).
3. Do not extract or reuse Apple keys; do not capture other people's devices.

## Gate 4: power (before connecting the S10 to a powered radio port)

1. With a meter or USB power logger, check what VBUS does when the phone is host-role (research/usb-feasibility.md: OTG boost can back-feed the radio).
2. Only proceed if VBUS behaviour is safe for both ends; otherwise stop and redesign (PD DR_Swap / kernel change).

## Gate 5: radio session (AC-03..AC-07)

1. Connect; record `dmesg`, `logcat`, usbmon.
2. Expected sequence if the reference document holds: role switch, iAP2 SYN, identification, CDC-NCM, mDNS, AirPlay SETUP... Compare each step with `tests/test_loopback_session` logs.
3. AC-05/06/07 only after the screen shows on the radio, audio plays, touch moves things.

## Gate 6: vehicle use and soak (AC-09..AC-11)

Screen-off operation, unplug/replug, thermal and battery logs over hours, always with `diagnostics.sh` snapshots. Never disable thermal or battery protections.
