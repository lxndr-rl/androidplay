# Mode manager: iPhone mode without the PC and ADB without a reboot (design, 2026-11-15)

Status: **DESIGN. Nothing in this document is implemented or tested yet.** It is written from the evidence in
`docs/hardware-evidence.md`. Implementation must be done with the phone on the PC (adb alive) and in small, reversible steps.

## Problem
1. Today "iPhone mode" is started by hand from the PC (`radio_c053_probe.sh` pushed and launched with adb root). The product
   cannot depend on the PC.
2. Today iPhone mode removes the adb function (`SOLE=1`) and presents Apple ids. Getting adb back needed a reboot every time
   (the restore path never worked reliably: the phone stayed off the bus until a key-combo reboot).

## What the logs already tell us (usable as detectors)
| Host | What it sends when we show **Apple ids** | What it sends when we show **Samsung ids + adb** |
|---|---|---|
| Development PC (Linux, `apple-mfi-fastcharge` binds 05ac) | `SET_CONFIGURATION 1`, then `SET_CONFIGURATION 0` ~16 ms later, a new cycle every ~1 s, 100 % of cycles, never a vendor request | normal enumeration, adb works |
| Zlink radio | string reads (254-byte), `c0 53` (repeated every 0.3 s), then `40 51` | Android Open Accessory probe: `c0 33`, `40 34` x4, `40 35` about every 14 s |
| Chevrolet radio (user-reported make) | `c0 53` only, no `40 51` (reason unknown) | not observed |
Consequences: a PC can be recognised within about a second while in iPhone mode (`SET_CONFIGURATION 0` pattern); a Zlink radio
is recognised in ADB mode by `c0 33`.

## Proposed behaviour
Two states for the USB gadget, switched by a small root daemon (`s10carosd`):

* **ADB** (Samsung ids, adb function, exactly what the HAL builds): the phone is a normal development device.
* **CARPLAY** (Apple ids + our FunctionFS interface): waiting for a radio's `c0 53`/`40 51`, then the session supervisor
  (`swap_to_host_hook.sh`).

Transitions (each with a hard time limit, so neither state can last forever by mistake):
1. Boot -> **CARPLAY (armed)** only if the car flag file `s10caros.car` exists, else **ADB** (a freshly flashed or
   unconfigured phone is always a normal phone).
2. CARPLAY -> **ADB** as soon as the PC pattern (`SET_CONFIGURATION 0` right after 1) is seen, or when a host is attached
   (UDC configured) for 15 s without any `c0 53`/AOA/Apple request, or when the cable is pulled while idle for more than
   10 min. Each of these restores ADB through the HAL path (see Safety), and ADB is then kept until the cable is pulled.
3. ADB -> CARPLAY on a cable re-plug when the last session was a radio (flag with timestamp, 15 min), or when `c0 33`
   (AOA probe) is seen.
4. Session end (radio device disappears, supervisor already handles this) -> back to CARPLAY (armed), which is what makes
   reconnection immediate.
Manual override without reboot: `touch /data/local/tmp/s10caros.off` (daemon restores ADB and does nothing), `rm` to resume;
reachable from a PC in ADB state, or from a shell on the phone.

## Where it starts at boot (no /system changes)
Preferred: LineageOS runs `/data/local/userinit.sh` / `/data/local/userinit.d/*` as root at boot on debuggable builds
(**to verify on the phone**: `ls /system/bin | grep -i init`, `grep -r userinit /system/etc/init`). Files live in `/data`,
survive nothing else, and are removed with one `rm`. Fallbacks, only if that mechanism is absent: an init `.rc` under
`/system/etc/init` needs `adb disable-verity` + remount (touches /system: higher risk, needs an explicit go-ahead).

## Safety rules (the constraint is "never lose the ability to get in")
1. Do not modify /system, /vendor or the boot partitions. Everything lives in `/data/local` and `/data/local/tmp`.
2. ADB is the default. The daemon never leaves CARPLAY without a timer that returns to ADB.
3. Kill switch file (`s10caros.off`) is checked every loop; a missing or unreadable state file means ADB.
4. Boot guard: the boot hook starts the daemon in the background with a 60 s delay and a crash counter in
   `/data/local/tmp`; after 3 consecutive crashed starts it disables itself (writes `s10caros.off`).
5. Restore path must be proven BEFORE enabling any automatic switching: run ADB -> CARPLAY -> ADB 20 times on the bench
   with the phone on the PC and count failures. Known history: restoring by toggling `sys.usb.config none/adb` after our
   FunctionFS function and ids were applied left the phone off the bus several times; the likely culprits are the removed
   `configs/b.1/function0` symlink (SOLE) and an extra UDC unbind/bind. Do NOT remove `function0`; rebuild the whole config
   with the HAL instead.
6. Out-of-band recovery that never depends on this work: Lineage recovery (Vol Up + Bixby + Power, `adb sideload`),
   Download mode (Vol Down + Bixby + USB), key-combo reboot (Vol Down + Power ~10 s). Keep them in mind; none is touched.
7. First deployment is done with the phone on the PC, daemon in ADB state, `s10caros.off` present; remove the file only after
   the bench test passes.

## Open questions (to answer with the phone attached)
* Does Zlink accept our FunctionFS interface IN ADDITION to the adb interface (single config with both)? The earlier "extra
  interface breaks it" conclusion was withdrawn (PC noise), so it is UNKNOWN. If yes, ADB and CARPLAY could coexist for radios
  and only the ids would need switching.
* Does the Chevrolet radio need a different `c0 53` answer, serial format or descriptors (the reply sweep is built for this)?
* Is `userinit` present? Is the HAL restore reliable enough for 20 consecutive switches?
