# Duende

Turn an Android phone into a wired-CarPlay **source** for your car radio: the radio thinks it is talking to an iPhone, and
shows an Android UI instead (launcher, Waze, Spotify, video, maps), with touch, audio and a dark car-friendly home screen.
The phone's own screen stays off while it streams.

Developed on a Samsung Galaxy S10+ (SM-G975F, LineageOS 23.2) against a Zlink5 head unit. The design is not tied to
that phone: the device-specific parts are isolated (USB role switch, panel control) so other models can be added.

## Status (2026-11-15)

Verified on real hardware (details and logs-derived evidence in [docs/hardware-evidence.md](docs/hardware-evidence.md)):

- Zlink5 radio (Allwinner, v5.2.44), second USB port: phone becomes USB host, iAP2 identification and authentication,
  AirPlay session over the USB-NCM link, H.264 video, audio, touch. Reconnects after unplug/replug.
- "Armed" mode: when away from the home Wi-Fi the phone presents itself as the head unit expects *before* it is plugged in;
  plugging it into a PC falls back to normal ADB without a reboot.
- Launcher with weather, speed (phone GPS), now playing, signal/Wi-Fi/battery and an app row; edge-swipe gestures
  (back / home / recents).

Not verified, do not assume:

- Any radio other than that Zlink5 (a Chevrolet MyLink-type unit was probed, never reached a session).
- Waze / Google Maps / Spotify inside the projected display (installed, not tested there).
- Gestures on the real radio; media-button mapping (the Zlink exposes only a touch HID).
- No boot-time autostart: the mode manager is started manually after a phone reboot.

## Layout

| Path | What |
|---|---|
| `usb/GadgetManager` | ConfigFS / FunctionFS gadget composer and the control-request probe (`ffs_probe`) |
| `services/CarPlayService` | iAP2, AirPlay client/sender, usbmux, session runner (C++ with host tests) |
| `services/Projector`, `services/ProjectionService` | virtual display, H.264 encoder, panel policy, audio tap |
| `services/InputBridge` | touch injector, gesture filter |
| `packages/CarLauncher` | home screen, splash, Back/Home capsule |
| `scripts/device/s10carosd.sh` | on-phone mode manager (ADB / CarPlay, armed state) |
| `tests/hardware` | bench and radio test scripts |
| `docs`, `research` | architecture, mode manager, evidence, protocol notes, legal notes |

Tests that need no hardware: `scripts/run-tests.sh`.

## Legal and safety

Wired CarPlay requires Apple's MFi authentication; this project **does not** bypass it or ship Apple keys. Presenting Apple
USB ids is an opt-in experiment for the owner's own radio and is kept out of any release image (see
[research/licensing.md](research/licensing.md) and `LEGAL-001` in [TASKS.md](TASKS.md)). "CarPlay" and "iPhone" are
Apple trademarks; this project is not affiliated with Apple. Nothing here touches `/system`, `/vendor` or boot partitions.

Internal identifiers (`com.s10caros.*`, `s10carosd`) are from the working name and will be renamed.
