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
