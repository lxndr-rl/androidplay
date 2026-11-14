# Hardware evidence (S10+ SM-G975F, serial R58M74PSHAY)

Raw outputs: `diagnostics-*/` (gitignored). Date 2026-11-14. Everything here was read, not changed.
Level: V = observed on this phone. Nothing here shows CarPlay works.

## Gate 0-2: install (executed with user's explicit go-ahead)
- Stock: SM-G975F, Android 12, G975FXXSGHWC2, bootloader locked (V).
- Bootloader unlocked via Download-mode unlock (user pressed the keys), data wiped (V: `flash.locked=0`, `verifiedbootstate=orange`, `vbmeta.device_state=unlocked`).
- Recovery: `recovery.img` of lineage-23.2-20261114 flashed with samloader-rs 2.2.0 (`--no-reboot`); Lineage Recovery booted (V, user saw it). Log printed no transfer line, so the flash is proven only by the recovery booting.
- ROM: `lineage-23.2-20261114-nightly-beyond2lte-signed.zip` (sha256 4b46860c...d673 verified) sideloaded, `Total xfer: 1.00x` (V).
- Booted: `ro.lineage.version=23.2-20261114-NIGHTLY-beyond2lte`, Android 16, `userdebug`, kernel 4.14.356-openela-rc1, SELinux Enforcing (V).
- `adb root` works after enabling Settings > Developer options > Superuser debugging: `uid=0 context=u:r:su:s0` (V).
- Incidents: `adb reboot download` skips the Warning screen (no unlock menu); `samloader dump-pit` reboots the phone and left Download mode unresponsive to the ODIN handshake until re-entered with the key combo; samloader needed `sudo samloader fix-usb` (udev rule 60-samloader.rules, user ran it).

## USB / Type-C (read-only, host-connected to PC)
- UDC `10c00000.dwc3`, state configured, high-speed, max super-speed-plus.
- `typec/port0`: `data_role=host [device]`, `power_role=source [sink]`, `port_type=[dual] source sink`, PD revision 3.0, Type-C 1.2, `power_operation_mode=3.0A` (current: device/sink toward the PC).
- `data_role` / `power_role` files are `rw-rw-r-- root system`: writable by root. NOT yet written: whether a swap works, and what happens to VBUS, is unknown.
- ConfigFS gadget `g1` exists with functions incl. `ffs.adb`, `ffs.mtp`, `ncm.gs9`, `rndis.gs4`, `acm`, `accessory`; no `hid`.
- Kernel config (`/proc/config.gz`): FunctionFS, NCM, DWC3 dual role, Type-C manager notifier, MAX77705 MUIC/CCIC = y; F_HID not set.
- Power supply nodes: usb, otg, wireless, max77705-charger. No `/sys/class/usb_role`.
- Battery 15% charging from the PC; do not run swap tests below ~50% with the PC cable as the only link.

## Not done
- No role swap attempted. No VBUS measurement (needs a meter). No radio connected. No CarPlay session.

## USB-003 attempt 1: data_role=host against a PC port (2026-11-14, root, battery 15%)
Method: on-device script (survives adb loss) wrote `host` to `typec/port0/data_role`, waited 7 s, wrote `device` back. Log: `diagnostics-*/swaptest-pc-host.log` (gitignored).
- Result (V): write returned rc=1 (rejected); `data_role` stayed `host [device]` (device); `power_role` stayed sink; UDC stayed `configured`; adb never dropped; restore write also rc=1 because nothing had changed.
- Kernel (V): `data_role_store host` -> `max77705_dr_set: typec_power_role=0, typec_data_role=0, role=1` -> opcode 0x37 sent to the MAX77705 -> no role change. So the Samsung Type-C manager does accept the request path; the PC end did not perform a DR_Swap (a PC port is typically not DRP-capable: HYPOTHESIS, PC port capabilities not read).
- Charging unaffected: ~545 mA at 5 V (`Inow=547mA`, `Imax=1100mA`, VBUS valid), SOC 15%.
- NOT learned: whether the swap works against a real radio (a car port may accept DR_Swap, or the radio side expects the phone in device mode and the role-switch happens via the Apple vendor requests documented in research/carplay-protocol.md, not via Type-C DR_Swap). NOT tested: `power_role` writes, accessory mode, a powered hub, VBUS behavior in host mode.

## USB-002 attempt: FunctionFS gadget on the real UDC, host = PC (2026-11-14, root)
Binaries built with NDK 26.1 (aarch64, API 29): `usb/GadgetManager/src/ffs_probe.cpp` (+ `gadget_manager`). Script: `tests/hardware/usb_ffs_probe_inplace.sh`. Raw log: `diagnostics-*/probe2.log`.
- Run 1 (new gadget `s10probe`, neutral VID 1d6b:0104): `mkdir /config/usb_gadget/s10probe` -> `Out of memory` (V). The Samsung kernel refuses a second ConfigFS gadget; `gadget_manager setup` therefore cannot work as written on this phone. adb was restored by the script (UDC rebind + sys.usb.config toggle).
- `mkdir g1/functions/ffs.carplay` works (V, rc=0).
- Run 2 (compose into existing `g1`, keep `ffs.adb`, add `ffs.carplay` as `configs/b.1/function1`, unbind -> link -> bind): (V)
  - UDC `configured`, high-speed.
  - PC `lsusb -v -d 04e8:685d`: `bNumInterfaces 2`; interface 0 = adb (255/66/1, EP1 OUT/IN); interface 1 = our FFS function (255/0/0, bulk EP2 IN 0x82 / OUT 0x02). Our descriptors (66 B) and strings (27 B) were accepted by the kernel.
  - FFS events on ep0: BIND, UNBIND, BIND, ENABLE. No SETUP events (the PC sent no class/vendor request).
  - Cleanup removed the function, UDC rebound, `sys.usb.config=adb`, adb back, 2 funcs -> 1 interface on the PC again (V).
- Identity used: stock Samsung VID/PID (04e8:685d). No Apple IDs were used (LEGAL-001 open).
- NOT shown: bulk data transfer (no PC-side client), iAP2 over the endpoints, the Apple VID/PID, CDC-NCM bring-up, anything with the radio.
- Design consequence: GadgetManager must compose into the existing gadget (or replace its functions in place) instead of creating `s10carplay`; the unit tests assume a new gadget and need updating.

## Radio (user-reported, not verified by us)
Generic Android-style head unit, Allwinner SoC, 1 GB RAM; works with an iPhone over USB cable (user statement). Model unknown. Whether its USB port is host-only, which CarPlay app/stack it runs, and whether it accepts a non-iPhone device: unknown.

## USB-002: GadgetManager compose-into mode on the S10+ (2026-11-14)
`gadget_manager --compose-into g1 setup|bind|teardown` (new mode; host tests 80/80) run on the phone with `tests/hardware/usb_gadget_manager_compose.sh` + `ffs_probe`. (V)
- PC saw `bNumInterfaces 2` (adb + our FFS interface 255/0/0 with bulk 0x82/0x02); ep0 events BIND, UNBIND, BIND, ENABLE; UDC `configured`. After teardown and the sys.usb.config toggle: 1 interface, adb back.
- Teardown's first unbind write returned `short write` (the kernel had already dropped the gadget after ffs_probe closed ep0); the code now ignores that write and still removes our link and function (host tests only; not re-run on the phone after that edit).
- Still NOT shown: data over the endpoints, iAP2, Apple IDs, the radio. The script still toggles `sys.usb.config` to bring adb back; whether the USB HAL recovers without that toggle is untested.
- User requirement (2026-11-14): the only requirement on the radio is that it has wired CarPlay. That is the design goal, not a verified compatibility claim.

## USB-002: bulk loopback PC <-> S10+ FunctionFS function (2026-11-14) (V)
`tests/hardware/pc_bulk_client.py` (raw usbfs ioctls, no libusb) + `ffs_probe carplay 40 echo` on the phone via `tests/hardware/usb_gadget_manager_compose.sh`. Log: `diagnostics-*/compose_bulk.log`.
- Device -> host: banner `S10HELLO` (8 bytes) received on the IN endpoint (0x82).
- Host -> device -> host: 300 rounds of 4000 random bytes, OUT 0x02 then IN 0x82, **300 ok / 0 mismatches**, 1.2 MB each way in 0.23 s (5.1 MB/s each way, USB 2.0 high-speed, no tuning). Device counted rx=tx=1,200,000 bytes.
- Lessons (V): (1) FunctionFS endpoint files do not support `poll`; OUT data arrives only while a blocking `read` is pending (use a thread). (2) A read buffer larger than the host transfer needs a short packet to complete: transfers that are an exact multiple of 512 (tried 512 and 4096) timed out; 4000 worked. The real protocol framing must send a short packet or ZLP at message ends or read with exact lengths.
- NOT shown: iAP2 or any CarPlay protocol on these endpoints, Apple IDs, throughput under sustained video load, behavior with the radio as host.
- `teardown_rc=1` in this run came from the arm64 `gadget_manager` pushed before the tolerant-teardown edit; the rebuilt binary is pushed to the phone but that path was not re-run.
