# AndroidPlay

**English** · [Español](README.es.md)

Your Android phone as a wired-CarPlay **source**: plug it into a radio that supports CarPlay over USB and the radio shows an
Android home screen (Waze, Spotify, video, maps) with touch and sound, while the phone's own screen stays off.

```
 phone (Android)                                   car radio (CarPlay by cable)
 ┌──────────────────────────┐   USB-A ⇄ USB-C    ┌──────────────────────────┐
 │ virtual display 1280×720 │ ── H.264 video ──▶ │  CarPlay screen          │
 │ launcher + your apps     │ ── audio ────────▶ │  speakers                │
 │ touch injector           │ ◀── touch ──────── │  touchscreen             │
 └──────────────────────────┘   iAP2 + AirPlay   └──────────────────────────┘
```

Built and tested on a Samsung Galaxy S10+ (SM-G975F, LineageOS 23.2) with a Zlink5 head unit. The device-specific parts
(USB role switch, panel control) are isolated so other models can be added.

## Highlights

- **Radio-side verified:** iAP2 identification and authentication, AirPlay session over USB-NCM, H.264 video, audio and touch.
- **Armed mode:** away from your home Wi-Fi the phone is ready *before* you plug in; at home it is a normal ADB phone.
  Plugging into a PC falls back to ADB without a reboot. Reconnects after unplug and replug.
- **Car home screen:** weather, speed (phone GPS), now playing, signal / Wi-Fi / battery and an app row, with placeholders
  when there is no data. Edge-swipe gestures: back, home, recents.
- **Screen off while streaming**, back on when you disconnect.

## Quick start

```bash
git clone https://github.com/lxndr-rl/androidplay.git && cd androidplay
scripts/build-phone.sh                                   # build (SDK + NDK needed)
scripts/deploy-phone.sh                                  # install on the phone (adb root)
scripts/install-autostart.sh                             # arm itself at every boot (init service via overlayfs)
```

Then unplug from the PC, wait ~10 s and plug into the radio. Full steps, requirements, revert and troubleshooting:
**[docs/installation.md](docs/installation.md)** ([Español](docs/installation.es.md)).

## Status (2026-11-15)

| | |
|---|---|
| Verified on real hardware | Zlink5 (Allwinner, v5.2.44), second USB port: session, video, audio, touch, reconnect, armed mode, PC fallback. Evidence: [docs/hardware-evidence.md](docs/hardware-evidence.md) |
| Not verified | any other radio (a Chevrolet MyLink-type unit never reached a session); Waze / Maps / Spotify inside the projected display; the edge gestures on the radio; media buttons (the Zlink exposes only a touch HID) |
| Limits | needs `adb root` (userdebug build); autostart uses `adb remount` overlayfs (tested on the S10+ only) |

## Repository

| Path | What |
|---|---|
| `usb/GadgetManager` | ConfigFS / FunctionFS gadget composer, control-request probe |
| `services/CarPlayService` | iAP2, AirPlay client, usbmux, session runner (C++, host tests) |
| `services/Projector`, `services/ProjectionService` | virtual display, H.264 encoder, panel policy, audio tap |
| `services/InputBridge` | touch injector and gesture filter |
| `packages/CarLauncher` | home screen, splash, Back / Home capsule |
| `scripts/` | `build-phone.sh`, `deploy-phone.sh`, `run-tests.sh`, `device/androidplayd.sh` (on-phone mode manager) |
| `tests/hardware` | bench and radio test scripts |
| `docs`, `research` | architecture, mode manager, evidence, protocol notes, legal notes |

Tests that need no hardware: `scripts/run-tests.sh`. More: [architecture](docs/architecture.md),
[mode manager](docs/mode-manager.md), [troubleshooting](docs/troubleshooting.md), [tasks](TASKS.md).

## Legal and safety

Wired CarPlay requires Apple's MFi authentication; this project **does not** bypass it or ship Apple keys. Presenting Apple
USB ids is an opt-in experiment for the owner's own radio, refused unless `APPLE_ID_OK=own_radio_only` is set, and kept out
of any release image (see [research/licensing.md](research/licensing.md)). "CarPlay",
"iPhone", "Android" and "Google Play" are trademarks of their owners; this project is not affiliated with Apple or Google.
Nothing here touches `/system`, `/vendor` or boot partitions.
