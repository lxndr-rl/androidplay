# Installation guide

**English** · [Español](installation.es.md)

How to install AndroidPlay on a phone and connect it to a wired-CarPlay radio. This is the day-to-day guide; the plan for
flashing a phone with LineageOS is a separate page: [rom-install-plan.md](rom-install-plan.md).

> **Honesty note.** Everything below was done by the author on one phone (Samsung Galaxy S10+, SM-G975F) and one radio
> (Zlink5 head unit). Steps marked *(tested)* worked there. Anything else (other phones, other radios) is untested.

## 0. What you need

| | |
|---|---|
| **Phone** | An Android phone with `adb root` whose USB controller can act as both host and gadget (only the tested one is known to work). Tested: SM-G975F with LineageOS 23.2 nightly **userdebug** (`adb root` works). A normal user build has no `adb root`, so the scripts refuse to run. Unlocked bootloader and the LineageOS install itself are out of scope here: see [rom-install-plan.md](rom-install-plan.md). |
| **Radio** | A head unit with *wired* CarPlay on a USB-A port. Tested: Zlink5 (Allwinner, v5.2.44) on its **second** USB port. |
| **Cable** | USB-A (radio) to USB-C (phone). A plain data cable is enough; no Power Delivery needed. |
| **PC (Linux)** | Android SDK (platform + build-tools: `aapt2`, `d8`, `zipalign`, `apksigner`), Android NDK r26+, JDK 17+, `adb`. `ffmpeg` is only needed to look at test captures. |

## 1. Get the code and run the tests

```bash
git clone https://github.com/lxndr-rl/androidplay.git && cd androidplay
scripts/run-tests.sh          # host-only tests, no phone needed; expect pass=7 fail=0
```

## 2. Build everything for the phone

```bash
scripts/build-phone.sh        # native arm64 tools, dex files and the two APKs -> .build/phone/
```

If it cannot find the NDK, set `ANDROID_NDK_ROOT` (and `ANDROID_SDK_ROOT` for the SDK).

## 3. Deploy to the phone

Connect the phone to the PC, enable USB debugging, check `adb devices`, then:

```bash
scripts/deploy-phone.sh
```

This pushes the tools to `/data/local/tmp`, installs the launcher (`dev.lxndr.launcher`) and the projection service
(`dev.lxndr.projection`), and grants the launcher location, phone state and notification-listener access (speed card,
signal bars, now playing). It does **not** start anything that changes USB behaviour.

## 4. Tell the phone where "home" is

The phone behaves as a normal ADB phone while it is connected to your home Wi-Fi, and *arms* itself for the radio
everywhere else. The network name stays in a file on the phone and is never committed:

```bash
adb shell 'echo -n "YOUR_WIFI_NAME" > /data/local/tmp/androidplay.home_ssid'
```

Without that file the phone never arms by itself. To force a state: `touch /data/local/tmp/androidplay.car` (always armed)
or `androidplay.home` (always ADB).

## 5. Apps (optional)

The launcher shows what is installed: user apps plus a few system ones. Waze, Google Maps and Spotify are not bundled.
*(tested)* The author installed them with [Aurora Store](https://f-droid.org/packages/com.aurora.store/) (anonymous login,
APK from F-Droid, hash and signature checked). Without Google Play Services, Maps may run limited. Sign in to each app once
on the phone screen before going to the car. Pin or hide apps in `config.properties` (`apps.pinned`, `apps.hidden`, package
names, comma separated) inside the launcher's private files directory.

## 6. Start the USB mode manager

Presenting Apple USB ids is an **opt-in experiment for your own radio** (`LEGAL-001` in [TASKS.md](../TASKS.md)): the
manager refuses to start without the explicit variable below. It is never part of a release image.

```bash
APPLE_ID_OK=own_radio_only scripts/deploy-phone.sh --start
adb shell /data/local/tmp/androidplayd.sh status        # state=adb ...
```

Without autostart (next section) the manager is **not** running after a phone reboot and the phone is a plain ADB phone (the
safe default); run the same command again. Kill switch: `adb shell touch /data/local/tmp/androidplay.off`. To stop and restore the stock USB config:
`adb shell /data/local/tmp/androidplayd.sh stop`. Stopping drops the USB link and can kill an `adb shell` that is still running it half way (the gadget is then left torn down
and the phone stays off the bus until a reboot): run it detached, `adb shell 'nohup /data/local/tmp/androidplayd.sh stop >/dev/null 2>&1 &'`,
and if `adb devices` is empty afterwards unplug and replug the cable once. Never leave the phone stopped before a trip: a stopped
manager means the radio sees a plain Android phone.

## 6b. Autostart at every boot (recommended)

A manager that dies with each reboot means a lost trip: the radio sees a plain Android phone. This installs an `init` service
so the phone arms itself without a PC:

```bash
scripts/install-autostart.sh          # one automatic reboot at the end; undo with --remove
```

How it works *(tested)*: `adb remount` puts an overlayfs over `/vendor` (the real partitions are not written; the changes live in
a scratch area), and `/vendor/etc/init/androidplay.rc` starts the manager after `sys.boot_completed`, restarting it if it dies.
The `.rc` carries the `APPLE_ID_OK=own_radio_only` opt-in of section 6: delete it with `--remove` to revoke. It never blocks boot, and
the kill switch `touch /data/local/tmp/androidplay.off` still works. After the reboot `adb shell /data/local/tmp/androidplayd.sh
status` should answer `state=adb` by itself. Needs a userdebug build with an unlocked bootloader. Not tried on any other phone.

## 7. Connect to the radio *(tested on Zlink5)*

1. Unplug the phone from the PC. Wait about 10 seconds: away from your home Wi-Fi it arms itself.
2. Plug it into the radio's USB port. On the Zlink5 the working one is the **second** port; if the radio says
   "Link fail, try another USB port", use the other port.
3. Expected: "Connecting", then the AndroidPlay splash, then the launcher, with touch and sound. The phone's own screen goes dark
   while it streams.
4. Unplug and plug again: it reconnects.
5. Back to the PC: plug it in; `adb` returns without a reboot. If it does not, replug the cable once.

Gestures on the projected screen *(implemented, not yet tried on the radio)*: swipe in from the left or right edge = Back, swipe up from the bottom edge = Home, a long
swipe up = Recents. The Back / Home capsule is also on screen.

## Uninstall / revert

```bash
adb shell /data/local/tmp/androidplayd.sh stop
adb uninstall dev.lxndr.launcher; adb uninstall dev.lxndr.projection
adb shell 'rm -f /data/local/tmp/androidplay* /data/local/tmp/{gadget_manager,ffs_probe,mdns_probe,carplay_phone,usb_host_iap2,projector.dex,inject.dex,swap_to_host_hook.sh}'
```

Nothing here touches `/system`, `/vendor` or the boot partitions; a reboot returns the phone to stock USB behaviour.

## Troubleshooting

| Symptom | What to try |
|---|---|
| `adb devices` is empty after `stop` or `start` | Unplug and replug the cable once (the PC port can wedge after Apple ids were shown). A reboot (Vol Down + Power ~10 s) always works. |
| `refused: set APPLE_ID_OK=own_radio_only` | Intended; read section 6, then set the variable. |
| Radio shows "Link fail" | Wrong USB port. Try the other one. |
| Radio stays on "Connecting" | The phone was not armed when plugged in. Check `status` says `CARPLAY` (armed), you are off the home Wi-Fi (or created `androidplay.car`), and wait 10 s after unplugging from the PC. |
| Weather says "Sin conexión" | No internet or no location yet. It fills in once there is GPS and data. |
| Speed shows "--" | No GPS fix yet (sky view). |
| Logs for a bug report | `adb pull /data/local/tmp/androidplayd.log`, `androidplayd.ctrl.log`, `swap.log`, `airplay.log`, `iap.log`, `projector.log` |

More: [troubleshooting.md](troubleshooting.md), [mode-manager.md](mode-manager.md), [hardware-evidence.md](hardware-evidence.md).

## Known limits

- Tested on one phone and one radio. A Chevrolet MyLink-type radio reached the first USB handshake but never a session.
- Autostart relies on `adb remount` overlayfs support (tested on the S10+ only); without it the manager is started by hand after a reboot.
- Waze / Maps / Spotify inside the projected display are untested.
