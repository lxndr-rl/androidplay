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

## USB-004 step 1: usbmux device side (host-only, no hardware) 2026-11-14
- Source of the wire format: libimobiledevice/usbmuxd `src/device.c` and `src/usb.h` (GPL; read as specification, no code copied). Facts used: VERSION request/reply use an 8-byte header (proto, length) with major 2; after it every packet has a 16-byte header with magic 0xfeedface and tx/rx seq; SETUP carries byte 0x07; TCP header is 20 bytes, data offset 0x50, window advertised in units of 256 (host: 131072 = 0x0200), host SYN seq 0, SYNACK expected to advance both sides by 1; host reads bulk-IN in 16384-byte units and treats a transfer of exactly 16384 as continued.
- Implemented: `services/CarPlayService/{include,src}/usbmux.*`, `tests/test_usbmux.cpp` (82 checks, ASan/UBSan, 0 failures). Golden bytes for the VERSION request and a SYN were derived by hand from the C source. A deliberate mutation (payload 16348) is caught.
- Limits: the test host is ours. Not verified against Zlink5, the Chevrolet radio or any real host. Device-side seq/rx_seq semantics and v1 mux are unverified/unimplemented.
- User statement (not verified): Chevrolet Captiva Premier 2024 (Ecuador) radio does not run Android.

## Radio observation 1: Samsung ids vs Apple ids on the user's radio (2026-11-14)
Radio: user-reported Zlink 5.2.44 on "NUOWEIDA-QUAD-CORE T3 k2001-nwd", features CPL CPW AAL AAW QTL QTW AML AMW, display 853x480-1280x720 (photo of the Device Info screen; ID/SN/KEY not recorded here).
Method: passive observer on the phone (tests/hardware/radio_observe.sh: dwc3_ctrl_req tracepoint, logcat, 1 Hz UDC/typec poll), phone as USB device only, adb function only. Raw: diagnostics-radiolog-183349/, diagnostics-radiolog2/ (gitignored).
- **Samsung ids (04e8:685d)** [V]: radio sent the Android Open Accessory sequence `c0 33` (getProtocol), four `40 34` (strings: manufacturer/model/...), `40 35` (start), 13 times about every 14 s. Android entered accessory mode ("Android Auto", 23 times) and left; no connection completed. Zlink showed only "conectando".
- **Apple ids (05ac:12a8, "Apple Inc."/"iPhone")** [V]: the radio sent `c0 53 00 00 00 00 04 00` (vendor, device-to-host, wLength 4) **115 times, every ~0.30 s**, and nothing else vendor-specific; our side did not answer it (no handler, so the request was not served). Zlink UI showed no sign. No `40 51` was sent, which is expected because the `c0 53` answer never arrived. `research/usb-feasibility.md` documents the answer as `01 00 00 00` from OrangeCar's protocol notes (not yet tried here).
- Conclusion (observed, one radio, one session): this radio does react to Apple's identity and begins the role-switch handshake of the OrangeCar document. It does not use a usbmux probe first. Whether answering `c0 53` leads to `40 51` and a swap request is NOT yet tested.
- Incident: the opt-in probe script did not restore the phone's ids. Cause: radio_observe.sh wipes /data/local/tmp/radiolog, which held the saved original ids written just before, so restore() found no file. With Apple ids the PC's adb could not hold the device (offline/absent). Recovered by a key-combo reboot (ids reset to Samsung). Fixed: saved ids moved outside the wiped dir and restore() falls back to this phone's stock values. The fixed script was NOT re-run.

## Radio observation 2: Apple ids + FFS function composed into g1 (2026-11-14)
Run: tests/hardware/radio_c053_probe.sh 600 (05ac:12a8, adb + ffs.carplay with one vendor interface, two bulk endpoints; ffs_probe serving ep0 with ALL_CTRL_RECIP|CONFIG0_SETUP). Raw: diagnostics-radiolog3/ (gitignored). The run was cut at ~430 s by the user's reboot, so the script's own restore path ran only from the watchdog test earlier, not at the end of this run.
- [V] The radio did a full enumeration 13 times (device/BOS/config(55 bytes)/strings 0-5, SET_CONFIGURATION 1) and then SET_CONFIGURATION 0 about every second (12 times), i.e. it kept configuring and unconfiguring the device. ffs_probe saw BIND/UNBIND/ENABLE/DISABLE churn at the same rhythm.
- [V] **No `c0 53` arrived and ffs_probe logged no SETUP event at all.** Contrast with observation 1 (same ids, adb-only config of 32 bytes): `c0 53` x115 every 0.3 s and no re-configuration loop.
- Not diagnosed. Candidates, none tested: the radio rejects/re-evaluates a device whose configuration has an extra vendor interface or string index 5; the radio only sends `c0 53` to a device it considers stable; the early (pre-config) requests never reach FunctionFS on this kernel. The only change between the two runs is the extra FFS interface (config descriptor 32 -> 55 bytes).
- Consequence: answering `c0 53` is not achieved. Next experiment must change one thing at a time (e.g. FFS function whose interface has zero endpoints, or Apple-like interface layout), or serve the vendor request from a function that is guaranteed to see ep0 requests before configuration.

## Radio observation 3: role-switch handshake reaches the phone (2026-11-14)
Run: `PROBE_MODE=clone SOLE=1 radio_c053_probe.sh 1500` (05ac:12a8; adb REMOVED from the config so the radio sees only our FunctionFS interface, class ff/42/01 with two bulk endpoints, config descriptor 32 bytes like observation 1). Raw: diagnostics-radiolog6/ (gitignored). User saw on Zlink: "Connecting, please wait" then "Link fail, try another usb Port", repeated.
- [V] ffs_probe received `c0 53 00 00 00 00 04 00` 7 times and answered `01 00 00 00` (the answer documented by OrangeCar via research/usb-feasibility.md).
- [V] After the answer the radio sent **`40 51 01 00 00 00 00 00`** (vendor OUT, wValue 1, wLength 0) 6 times, the second request of the documented sequence. The first was acknowledged (role switch NOT performed); the later ones got a stall only because RoleSwitchHandler does not reset its state (harness artifact, not a radio decision).
- [V] Nothing else vendor-specific arrived. After each cycle the radio dropped the device (SET_CONFIGURATION 0, re-enumeration) and retried every ~25-30 s; Zlink reported link failure. typec stayed `data_role [device]`, `power_role [sink]` with the partner present: the phone never became host.
- Established: this Zlink radio (v5.2.44 per its screen) follows the OrangeCar role-switch model (Model 1): C0 53 query, 40 51 swap request. It does NOT use usbmux first.
- Not established / blocking: what the radio expects after `40 51` (the phone must drop the bus and become USB host; the radio then acts as a device with iAP2 + NCM per the Zlink strings). Whether the S10+ can become host toward this radio depends on the cable and port (A-to-C cable gives the phone no CC role to swap; PD DR_Swap is unavailable over such a cable), and on VBUS back-feed. Not tested.
- Cases when the earlier observation 2 (interface extra) is read with this one: with ff/42/01 as the only interface the radio proceeds; with an additional interface it loops without sending `c0 53`. Cause of that difference unknown.

## Radio observation 4: swap hook run, no c0 53 at all (2026-11-14)
Run: `PROBE_MODE=clone SOLE=1 SWAP=1 radio_c053_probe.sh 1500` (same config as observation 3 plus the data_role=host hook). Raw: diagnostics-radiolog7/ (gitignored). The script ran its full 1500 s.
- [V] 431 enumeration cycles by the radio, **0 `c0 53`, 0 `40 51`, no SETUP event**, so the hook never fired and swap.log was never created. data_role/power_role were never touched in this run.
- [V] Cycle structure (trace analysis): in the 7 cycles of observation 3 that produced `c0 53`, the radio sent SET_CONFIGURATION 1, then SET_CONFIGURATION 0 after ~16 ms, then SET_CONFIGURATION 1 again after ~54 ms, then `c0 53` at ~+0.3 s. In the usual cycle it stops after SET_CONFIGURATION 0, and re-enumerates ~0.9 s later. In observation 1 (real adb function, 32-byte config) the radio instead read string descriptors (254-byte reads) after SET_CONFIGURATION 1, never sent SET_CONFIGURATION 0, and sent `c0 53` every 0.3 s.
- Not explained: why the radio deconfigures our FunctionFS function but not the adb function with the same class triple and endpoint layout; why 7/148 cycles proceeded in observation 3 and 0/431 in observation 4. Candidates (untested): radio state degraded after repeated failures (needs a fresh Zlink/radio start); difference in the interface string descriptor (adb's vs our FFS string); timing of our function's enable after SET_CONFIGURATION 1.

## CORRECTION to observations 2 and 4 (2026-11-14, same day)
Splitting each trace into windows showed that two signatures are mixed in the logs:
- **PC signature** (my development PC, whose kernel driver `apple-mfi-fastcharge` binds any 05ac device and disconnects it about every second): enumeration with SET_CONFIGURATION 1 then SET_CONFIGURATION 0 after ~16 ms, no 254-byte string reads, a new cycle each ~1 s.
- **Radio signature** (Zlink/libusb): 254-byte string-descriptor reads after SET_CONFIGURATION 1, no SET_CONFIGURATION 0, then `c0 53` (and `40 51` after our reply).
Window analysis: observation 1 radio window = 1 enumeration, 8 string reads, 115 `c0 53`. Observation 3 radio window (154 s) = 68 enumerations, 0 SET_CONFIGURATION 0, 216 string reads, 6 `c0 53`, 6 `40 51`. Observation 2 and the noeps run show ONLY the PC signature; observation 4 (user was away from the radio for part of the run, and he reports he was not there 25 minutes) shows ONLY the PC signature.
Therefore: the statements "radio re-configures our FFS function in a loop" (obs. 2) and "431 radio enumerations, no c0 53" (obs. 4) are WRONG: those cycles were the PC's. The obs.-2 conclusion that an extra interface suppresses `c0 53`, and the 7/148 statistic, are withdrawn. What stands: obs. 1 and obs. 3 (radio sends `c0 53` then `40 51` to a device with the Apple ids and a sole ff/42/01 interface). Whether the extra-interface or noeps variants make the radio proceed is UNKNOWN: in those runs the radio's enumeration is not visible in the trace (the radio may not have been connected or the trace may have missed it).
Method lesson: never read cycle counts without separating hosts; analyse per window with the signature above.

## Radio observation 5: role-switch request reaches the phone, kernel refuses to become host (2026-11-14)
Run: `PROBE_MODE=clone SOLE=1 SWAP=1 radio_c053_probe.sh 1500` on the radio (user saw "Connecting" and "Link fail" again; first attempt of the session showed nothing). Raw: diagnostics-radiolog8/ (gitignored).
- [V] ffs_probe: 9 x `c0 53` answered, 9 x `40 51 val=1` acknowledged, swap hook started 9 times (the hook is not single-flight, so runs overlapped; harness defect, results are still readable).
- [V] In every hook run: gadget dropped (UDC empty, `not attached`), `echo host > /sys/class/typec/port0/data_role` returned rc=1 three times per run, `data_role` stayed `[device]`, `power_role` stayed `[sink]`, `/sys/bus/usb/devices` never listed a device on the phone: the phone did not become host. power_role was never written (no VBUS source).
- [V] Reason from the kernel (re-run on the PC port right after reboot): `max77705_dr_set: typec_power_role=0, typec_data_role=0, role=1` then `try reversing, from UFP to DFP`, write rejected rc=1. The Samsung MAX77705 driver does the swap as a PD DR_Swap exchange; a USB-A-to-C legacy cable carries no PD, so there is nobody to answer it.
- Consequence (code-backed + observed, not a proof about every path): wired CarPlay Model 1 needs the phone to become host after `40 51`. On this phone with an A-to-C passive cable the standard swap path is unavailable. A forced DFP through raw CCIC opcodes (`/sys/class/sec/ccic/debug_opcode`) would also have to source VBUS into the radio (Type-C DFP requires it): not attempted; risk to both ports; needs explicit user consent.

## USB-003: host mode by forcing the Exynos OTG FSM (2026-11-14) [V]
`/sys/devices/platform/10c00000.usb/10c00000.dwc3/{id,b_sess,state}` exist (id=1, b_sess=1, state=b_peripheral). On a PC port, `echo 0 > id` (tests/hardware/otg_id_probe.sh, 15 s, auto-restore):
- state -> `a_host`; the kernel registered xHCI root hubs usb1/usb2 on the phone (the phone is a USB host); UDC `not attached`.
- typec `data_role` stayed `[device]`, `power_role` stayed `[sink]` (never written); charger kept "Charging", VBUS valid from the PC, iin ~460 mA, no OTG boost. Log line `max77705_set_host_turn_on_event: current_set is 1!` appeared; no VBUS source observed.
- `echo 1 > id` restored `b_peripheral` with the gadget configured again.
This bypasses the PD DR_Swap that max77705_dr_set needs (research/okcaros-recovery.md describes OkcarOS doing the equivalent with a kernel mode switch). Not yet tested with the radio as partner. NOT yet done: iAP2 over the host-side USB interface of the radio, NCM host, AirPlay session.

## Radio observation 6: phone became host, the radio never appeared (2026-11-14)
Run: `PROBE_MODE=clone SOLE=1 SWAP=1 radio_c053_probe.sh 1500` with the id=0 hook. Raw: diagnostics-radiolog9/ (gitignored). User saw "Link fail" again and noticed USB debugging dropping and returning at each retry, with the phone still charging.
- [V] 4 x (`c0 53` answered, `40 51` acked, hook run). Each run: `id=0` -> `state=a_host`, xHCI root hubs usb1/usb2 up on the phone, held 40 s, then `id=1` -> `b_peripheral`, gadget back. power_role stayed `[sink]`; the phone kept charging (VBUS from the radio).
- [V] In all 4 runs the phone's host side listed **no USB device** besides its own root hubs (no `DEV` line in swap.log): the radio did not show up as a device within 40 s.
- [V] During a_host the gadget pullup toggled `pullup=1/0` about every 0.5 s with `vbus = 0` in dmesg (the gadget layer keeps trying while the controller is in host mode; harmless but noisy, not understood).
- Unknown (candidates, none tested): the radio's port does not support device mode (Zlink's own text is "try another usb Port"; many head units wire the CarPlay/OTG role to one port only); the radio switches to device mode later than our 40 s window or only after a longer disconnect; the radio needs a particular signal from the phone before it pulls D+; no VBUS handling mismatch is visible.

## Radio observation 7: on the OTHER USB port the radio appears as a device when the phone is host (2026-11-14) [V]
Run: same probe, user connected to the radio's second USB port (from ~22:03 local), raw diagnostics-radiolog10/ (gitignored). On the usual port (first two hook runs) the phone's host side saw nothing, as in observation 6.
- [V] Third and later hook runs (other port): while the phone was host (`a_host`, power_role sink, still charging), the phone's xHCI enumerated **a USB device 1-1, VID:PID `1f3a:1007`, manufacturer/product `Android`/`Android`, 3 interfaces, high speed (480)** for ~80 s per run. 1f3a is Allwinner's vendor id. So the radio did switch its port to device mode after the `40 51` request and exposed a gadget (consistent with the Zlink strings: ConfigFS g1 with iap/iap2/ncm functions).
- Zlink UI on that port: "Connecting" then nothing, no "Link fail".
- NOT yet captured: the device's interface classes/endpoints, descriptors, drivers bound, strings. The hook was extended to dump them on first sight (DESC/IFACE/SERIAL lines).
- Not yet done: iAP2 identification over the radio's iAP2 interface, NCM host side, AirPlay session. Everything beyond the swap is unverified.

## Radio observation 8: the radio's gadget descriptors; NCM already works on the phone (2026-11-14) [V]
Run: same probe on radio port "B" (the second port), raw diagnostics-radiolog11/ (gitignored). Zlink UI on port A: "Link fail" (as before).
Device enumerated by the phone's xHCI (phone = host, power_role sink): `1f3a:1007`, manufacturer/product "Android"/"Android", serial `20080411`, 1 configuration, bMaxPower 2 mA, high speed. Raw descriptor (hex) is in the log; decoded:
- Interface 0: class `ff/f0/00`, string "iAP Interface", 2 endpoints: bulk IN `0x81`, bulk OUT `0x01`, 512 bytes. No kernel driver bound on the phone.
- IAD + interface 1: class `02/0d/00` ("CDC Network Control Model (NCM)"), 1 interrupt IN endpoint `0x84`, CDC functional descriptors (union, NCM, ethernet MAC string); interface 2: class `0a/00/01` ("CDC Network Data"), bulk IN `0x82`, bulk OUT `0x02`, 2 alt settings.
- The phone's kernel bound `cdc_ncm` and registered `eth0` (MAC `62:a0:38:25:e8:a2`, 425 Mbit/s). This is the radio-side NCM gadget of the CarPlay IP link.
Not done: iAP2 over interface 0 (endpoints 0x81/0x01), IPv6/mDNS/AirPlay over eth0.

## Radio observation 9: FIRST iAP2 CONTACT with the radio (2026-11-14) [V]
Run: phone host on port B, `usb_host_iap2` (services/CarPlayService/src/usb_host_iap2.cpp: DeviceSession/Driver from CP-004/005 over usbfs bulk 0x81/0x01). Raw: diagnostics-radiolog12/iap.log (gitignored).
- [V] The radio sent the iAP2 start-of-link bytes `ff 55 02 00 ee 10`; we echoed them. Radio SYN (`ff 5a 00 17 80 ...`) -> our SYN|ACK. The radio's link parameters: max outstanding 0x7f? (as parsed by CP-004), session table 3 entries.
- [V] Identification: the radio sent IdentificationInformation (name "zlink", model "zlink3", manufacturer "zjinnova", serial "0123456789A01234", versions "1.0.0"). Our driver accepted it.
- [V] Authentication: the radio sent a 908-byte certificate message; we requested the challenge; the radio answered with 128 bytes whose leading bytes are all zero. Our driver does NOT verify certificate/signature (documented design); we do not know whether the zeros are a real signature or padding of a missing MFi chip (Zlink strings say "Err: Mfi chip not available"). The driver reported success and sent DeviceInformationUpdate "iPhone" and DeviceLanguageUpdate "en"; the radio acked them.
- [V] After the burst the radio sent nothing for ~35 s, then the bus died (RX error errno 71) at the end of the radio's attempt.
- [V] eth0 (cdc_ncm) came up with link-local IPv6 `fe80::cc34:a1af:6ea5:4f`; no IPv6 neighbor/route was visible 6 s after the device appeared.
- Not known: what the radio waits for after the burst (candidates: mDNS/AirPlay over eth0; an iAP2 message our driver did not send; the radio's accept list gating). Next: mDNS probe on eth0, then AirPlay client over IPv6 link-local.

## Radio observation 10: the radio publishes an AirPlay service over the NCM link (2026-11-14/15) [V]
Run: same hook; raw diagnostics-radiolog14/ (gitignored). iAP2 replay of observation 9 reproduced the same sequence (identification, certificate, auth, burst) at a second connection; after ~35 s the radio dropped the bus again (errno 71).
- [V] The phone's `mdns_probe` on eth0 got **mDNS responses from the radio at `fe80::30c9:b4ff:fe92:809`** (EUI-64 of MAC 32:c9:b4:92:08:09) for `_airplay._tcp.local`: instance `localhost`, TXT `deviceid=32:C9:B4:92:08:09`, `features=0x44540380,0x61`, `flags=0x4`, `model=AirPlayGeneric1,1`, `pi=4bbff8d8-43fa-4af4-95f5-7fcc5039e249`, `srcvers=280.3`. Our phone-side `_carplay-ctrl._tcp` and `_raop._tcp` queries drew no visible answer in the first lines. SRV port was not decoded by that tool (binary); airplay_probe now decodes it.
- Not yet: TCP connect to the radio's AirPlay port, `GET /info`, the AirPlay session (SETUP/RECORD) over IPv6 link-local. The existing SessionRunner is IPv4 only (CP-008).

## Radio observation 11: AirPlay port and the Android routing blocker (2026-11-15) [V]
Run: carplay_phone via the hook, raw diagnostics-radiolog16/ (gitignored). The previous run (diagnostics-radiolog15) was void: I launched without `adb root` and every step failed with Permission denied (my error).
- [V] mDNS: `localhost._airplay._tcp.local` SRV **port 47158** (dynamic port, not 7000) on `fe80::30c9:b4ff:fe92:809` (the radio); TXT as in observation 10.
- [V] `connect()` to `[fe80::30c9:b4ff:fe92:809%eth0]:47158` failed with `ENETUNREACH` (errno 101), for both the throwaway `GET /info` and the runner. Cause (observed `ip -6 rule` on the phone): Android's policy rules end with `32000 from all unreachable`; netd-managed interfaces (dummy0) have `iif lo oif IFACE lookup IFACE` rules and a table with `fe80::/64`; `eth0` (cdc_ncm, unmanaged) has neither. Multicast mDNS works because it uses the multicast interface option.
- Fix in the hook (not yet verified on the radio): `ip -6 route replace fe80::/64 dev eth0 table 4242` + `ip -6 rule add pref 17001 iif lo oif eth0 lookup 4242`, removed at the end of each attempt. Syntax checked on dummy0.

## Radio observation 12: FIRST IMAGE AND AUDIO FROM OUR CODE ON THE RADIO (2026-11-15) [V + user report]
Run: carplay_phone via the hook with the eth0 policy route; raw diagnostics-radiolog17/ (gitignored).
- [V] mDNS gave port 46010 this time (the radio's AirPlay port changes per attempt). `GET /info` answered 200 with `Server: AirTunes/280.3`. The Client ran the sequence SETUP /setup, GET /info, RECORD, modesChanged, SETUP (event/screen/audio), feedback every 1 s; state 2 (Streaming) reached at 4.56 s; the radio asked for a key frame at 5.29 s.
- [user report] The radio displayed the looping Settings video (our 1200x512 fixture), complete, uncropped; the 440 Hz tone was audible; no cut by the radio. The session ended at 100.0 s because the tool's own limit (100 s) expired: 2661 access units sent (the user's "cut after ~1 min" matches the run ending at its limit; not a radio disconnect).
- [V] The radio sent 164 touch events to us over the event/HID path (`HID report len=5`, touches decoded as x 0..27, y 0..9: tiny values). The legacy 5-byte layout assumed by TouchTracker (logical 0..32767) therefore does NOT match this radio. The radio's per-session `/info` contains `displays` and `hidDevices` (22464-byte body) but the old tool logged only the first 600 bytes. carplay_phone now saves all control/event bytes to /data/local/tmp/session_raw.bin and prints HID report hex.
- Not done: real phone screen capture instead of the fixture; touch scaling from the radio's HID descriptor; real audio; persistent service; stability beyond 100 s.

## Radio observation 13: the phone's REAL screen on the radio; touch format decoded (2026-11-15) [V + user report]
Runs: diagnostics-radiolog17/18 (gitignored).
- [user report + photo] The radio showed the S10+'s own Settings app live (Spanish UI, "Buscar en Configuración", Internet y redes, Dispositivos conectados, ...), produced by: ProjectionService VirtualDisplay 1280x720 -> OMX.Exynos.AVC.Encoder -> loopback -> carplay_phone -> AirPlay session. Audio (440 Hz test tone) audible.
- [V] Exynos encoder on the S10+ rejects KEY_PROFILE baseline, KEY_LATENCY, KEY_PRIORITY and KEY_MAX_B_FRAMES (CodecException -2147479551); it accepts KEY_REPEAT_PREVIOUS_FRAME_AFTER but an idle screen still produced few frames (95 AUs in 138 s of mostly idle).
- [V] The virtual display stays `state OFF` and encodes nothing while the device sleeps; and the lock screen is drawn into it while locked. Mitigation used: `svc power stayon true`, `locksettings set-disabled true`, wake + dismiss keyguard. A trusted/own-display-group virtual display (privileged) would avoid depending on the physical screen state.
- [V] HID: the radio's /info lists 4 hidDevices (Touch Screen, My Knob, Media Button, My Telephony). Touch Screen descriptor (62 bytes): Digitizer Touch Screen, finger collection, 1-bit Touch + 7 padding, X 16-bit 0..0x0500 (1280), Y 16-bit 0..0x02D0 (720), no report id. 1170 reports from the user's test decode to X 18..1275, Y 5..717 = full screen in pixels. The 32767 assumption of the legacy layout was wrong for this radio; fixed (hidTouchLogicalMax + TouchTracker logical range, tested with the real descriptor).
- [V] Touch injection (InputBridge InjectMain through a FIFO, as root via app_process) tested without the radio: a tap at (640,160) on the virtual display opened "Internet y redes" in Settings (video frame decoded to confirm).
- Session end at 142.7 s in run 18: the radio sent changeModes + two forceKeyFrame requests, then closed the control channel 0.3 s later (consistent with the user leaving the screen or unplugging; the cause is not proven).
- Not done: touch-to-phone wiring on the real radio (built, not yet run); real audio; launching other apps (SmartTube is installed) on the display; running without the screen being awake; persistent service; multi-touch.

## Observation 14: launcher, trusted virtual display, Xuper stays on the projected display (2026-11-15) [V on the phone, not yet with the radio]
User report from the radio run of observation 13: touch "works very well, almost 100% real time"; opening Xuper from Settings showed its splash then the app moved to the PHONE and the radio went back to Settings; app audio plays on the phone speaker only (no system-audio capture yet); swiping down shows the radio's own navigation bar (radio UI, not ours).
- [V] Reproduced on the phone: with an app-owned virtual display, Xuper's `HomeActivity` ends up on Display #0. Cause: third-party apps may only start NEW tasks on a virtual display that is trusted (or that they own).
- [V] Fix: `services/Projector` (app_process as root; package "root" for uid 0) creates the display with PUBLIC|PRESENTATION|OWN_CONTENT_ONLY|SUPPORTS_TOUCH|ROTATES_WITH_CONTENT|DESTROY_CONTENT_ON_REMOVAL|TRUSTED|OWN_DISPLAY_GROUP|ALWAYS_UNLOCKED|TOUCH_FEEDBACK_DISABLED|OWN_FOCUS. dumpsys shows FLAG_TRUSTED/OWN_DISPLAY_GROUP/ALWAYS_UNLOCKED/OWN_FOCUS. After tapping the Xuper tile in the launcher (injected touch), `com.xuper.netxxus/.HomeActivity` is Resumed on the virtual display and Display #0 only has the phone's own launcher. The encoded video (decoded on the PC) shows Xuper Hydra's live TV UI with the Back/Home pill on top.
- [V] While the phone is put to sleep with KEYCODE_SLEEP the virtual display also goes OFF/black even with its own display group (frames decay to black). Mitigation in use: `svc power stayon true` while plugged in. A wakeup of the display's own group / keep-screen-on window is not implemented.
- Launcher (packages/CarLauncher): 1280x720 landscape home: 24 h clock + date, favourites row (pinned, else SmartTube/Xuper/YouTube Music/Settings if installed), all apps grid, floating Back/Home pill (NavOverlayService on the projected display; Back is injected through InputBridge `--listen 47600`). Viewed as an H.264 frame decoded on the PC (phone is the real device).
- Not done: system-audio capture to the radio; running this whole chain on the radio again (the hook now starts the Projector, launcher, overlay and injector); persistent boot service; handling the radio's HID knob/media buttons.

## Observation 15: audio capture works on the phone (2026-11-15) [V on the phone, not yet with the radio]
- `services/Projector` AudioTap: AudioRecord REMOTE_SUBMIX (48 kHz stereo s16le) from the root app_process. With a tone generated by ToneTest (440 Hz, amplitude 8000, USAGE_MEDIA) the capture shows a steady tone (rms 5656 = expected 8000/sqrt2) and nothing else; the app's output device moves to the submix (deviceIds changes from the speaker), i.e. the phone speaker goes quiet.
- [V] The submix device has its own music volume: at the default it came out ~ -33 dB (rms 125); setting the music stream volume to max WHILE the submix is the active output gave full scale. AudioTap now re-applies max volume every second; verified starting from volume index 5 (full level within ~1 s).
- Sender side: carplay_phone listens on 127.0.0.1:47501 for raw PCM and pushes 10 ms chunks to the AirPlay session (the fixture tone is only used when no live source is connected).
- Not yet verified with the radio: audio quality/latency/lip sync on the real session, the radio's volume knob, behaviour with silence between sounds.

## Observation 16: Chevrolet MyLink radio (user-reported, not Android) reacts to Apple ids but stops after C0 53; keyboard, panel off, CarPlay-style launcher (2026-11-15)
- User report (Zlink radio run of observation 14/15): audio and video "very fluid"; the soft keyboard opened on the PHONE's screen; the phone screen cannot be turned off without breaking the projection and it only drains battery; the launcher should look like CarPlay and show only useful apps.
- [V, from the phone's logs, diagnostics-radiolog19] When the user connected the phone "as is" to a Chevrolet infotainment (MyLink, 10.4 in, per the user/ChatGPT, not verified) the trace shows a host window with: enumeration, SET_CONFIGURATION 1, string reads, then `c0 53` (3 times, answered `01 00 00 00`), and **no `40 51`** afterwards; the host re-enumerated every ~3 s. No SET_CONFIGURATION 0 loop. During that connection the phone's typec showed `power_role = source` (the phone was sourcing VBUS), unlike the Zlink radio (sink). This is a different host that does start the Apple handshake but does not continue after our answer. Why is unknown.
- Experiment prepared (not yet run on that radio): `SWEEP=1`: every other `C0 53` is answered with a different 4-byte value from a list (baseline 01 00 00 00 in between), the log marks which variant preceded any `40 51`; the serial string is set to an iPhone-style 25-char value.
- [V] Keyboard on the projected display: `IWindowManager.setDisplayImePolicy(displayId, LOCAL)` (called from the Projector) moves the IME to the virtual display (mCurTokenDisplayId = display id, mInputShown=true, IME layering target = the app on the virtual display).
- [V] Physical panel off without sleeping the device: Projector powers the panel off with SurfaceControl.setDisplayPowerMode through DisplayControl (services.jar via DexClassLoader + libandroid_servers); SurfaceFlinger reports powerMode=Off for the physical display while the virtual display keeps streaming (stable frames over 30 s). `PowerManager.wakeUp(time, reason, details, displayId)` is called every second for the virtual display's group (it wakes the whole device but the panel stays off) and a partial wake lock is held. The panel is switched back on when the Projector exits. The old approach (KEYCODE_SLEEP) turned the virtual display black.
- Launcher: CarPlay-style (sidebar with 24 h clock/date, paged grid of 4x2 large rounded icons with labels, page dots). Lists user-installed apps plus an allow-list of system apps (Settings, Browser, Music, Chrome, Maps, YouTube): recorder, camera, calculator, contacts etc. no longer appear.
