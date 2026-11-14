# USB feasibility: S10+ (beyond2lte) as the USB device toward a CarPlay radio

Agent C, 2026-11-14. No phone attached (`adb devices` empty), so **nothing below was run on hardware**. Evidence levels: **verified** (read primary text myself), **code-backed** (read source at a named commit), **hypothesis** (inference or secondary source), **blocked** (cannot proceed without something we lack). Sources cloned to the scratchpad, not into the repo.

## P1 answer
- **Can the S10+ enumerate as a USB device with arbitrary (Apple-style) descriptors toward a host radio, from a native service?** Yes. Confidence **high (code-backed)**: ConfigFS + FunctionFS are built in, DWC3 runs OTG/dual-role, and the kernel's FunctionFS can deliver device-recipient vendor requests to userspace.
- **Can it then complete the wired-CarPlay USB role swap (become host, radio becomes device)?** **Unknown, medium-low confidence (hypothesis).** Samsung's MAX77705 Type-C stack and `dwc3` OTG state machine support both roles, but the data-only swap that keeps the radio's VBUS (no OTG boost) has no evidence and carries a hardware risk (section 2).
- **Whole P1 (radio accepts S10 as an iPhone, CarPlay runs):** **blocked** by non-USB items (iAP2/CarPlay messages are NDA, see `research/carplay-protocol.md`) and by missing hardware (radio, USB analyzer).

## 1. Controller and kernel support
| Fact | Level | Evidence |
|---|---|---|
| Kernel is 4.14.356-openela-rc1 (Makefile), LineageOS `android_kernel_samsung_exynos9820` (name is `exynos9820`, not `universal9820`), default head `a412cfaf86b9` 2026-08-05; `lineage-23.2` and `lineage-24.0` branches point at the same SHA, `lineage-22.1` also exists | code-backed | https://github.com/LineageOS/android_kernel_samsung_exynos9820 (`git ls-remote`, 2026-11-14) |
| Controller is Synopsys DWC3 via Samsung glue `dwc3-exynos.c`; DT node `&usbdrd_dwc3`: `dr_mode = "otg"`, `maximum-speed = "super-speed-plus"`, `usb3_lpm_capable`; gadget name `10c00000.dwc3` | code-backed | kernel `arch/arm64/boot/dts/samsung/exynos9820-beyond2lte_common.dtsi`; device tree `BoardConfigCommon.mk:165` (`gadget_name,10c00000.dwc3`) |
| defconfig `exynos9820-beyond2lte_defconfig`: `USB_GADGET=y`, `USB_DWC3=y`, `USB_DWC3_DUAL_ROLE=y`, `USB_DWC3_EXYNOS=y`, `USB_CONFIGFS=y`, **`USB_CONFIGFS_F_FS=y`**, `USB_CONFIGFS_ACM/NCM/RNDIS=y`, `F_MTP/F_PTP/F_ACC/F_AUDIO_SRC/F_MIDI=y`, `USB_XHCI_HCD=y`, `TYPEC=y`, `USB_NOTIFIER=y`, `CCIC_MAX77705=y`, `USB_GADGET_VBUS_DRAW=900` | code-backed | same kernel commit. Also legacy `USB_G_ANDROID=y` alongside ConfigFS |
| **Not set**: `USB_CONFIGFS_F_HID`, `F_UAC1/2`, `F_UVC`, ECM, EEM, SERIAL, MASS_STORAGE, `TYPEC_TCPM`, `TYPEC_UCSI`, `CCIC_ALTERNATE_MODE`, `PDIC_PD30` | code-backed | same file. Consequence: HID or UAC would need either defconfig changes or custom FFS interfaces |
| LineageOS device tree already uses ConfigFS: `configs/init/init.exynos9820.usb.rc` creates `/config/usb_gadget/g1` (VID 0x18d1), FFS instances adb/mtp/ptp, accessory, rndis, ncm, acm, midi; sets `sys.usb.configfs 2` ("Use USB Gadget HAL"); packages `android.hardware.usb-service.samsung` and `android.hardware.usb.gadget-service.samsung` | code-backed | https://github.com/LineageOS/android_device_samsung_exynos9820-common @ `38af956a` (2026-02-08, lineage-23.2/24.0 head); HAL: https://github.com/LineageOS/android_hardware_samsung `aidl/usb/gadget/UsbGadget.cpp` (lineage-23.2) writes `kGadgetName` to the gadget's `UDC` |
| LineageOS 22.x: I did **not** read the 22.1/22.2 branches; 23.2 and 24.0 are identical | not checked | `lineage-22.2` branch exists in both repos |
| Samsung's own open-source tarball for SM-G975F (opensource.samsung.com) | not read | not accessed; LineageOS kernel derives from it. No claim made |
| "ConfigFS works on the booted device" | **blocked (no phone)** | only static source read; run `scripts/usb-probe.sh` once a phone is on adb |

Implication: the Android USB HAL owns `g1` and the single UDC. A second gadget cannot bind until `g1/UDC` is cleared. `usb/GadgetManager` therefore has `--takeover g1`. Race with the HAL rewriting `UDC` on a USB-mode change is **hypothesis** (stop the HAL service or set `persist.sys.usb.config=none`; not tested).

## 2. Type-C, role swap, charging
| Fact | Level | Evidence |
|---|---|---|
| Role comes from the Samsung CCIC (MAX77705): `usb_notifier.c` handles `ATTACH_DFP` (host on), `ATTACH_UFP` (device on), detach; VBUS events go to `dwc3_exynos_vbus_event()`; `dwc3/otg.c` is an `otg_fsm` that calls `otg_drv_vbus()` when entering host | code-backed | kernel `drivers/usb/notify/usb_notifier.c`, `drivers/usb/dwc3/otg.c` |
| MAX77705 PD driver receives DR_SWAP, PR_SWAP, VCONN_SWAP (`max77705_pd.c`); kernel has `TRY_ROLE_SWAP_DR/PR/TYPE` states; Linux `typec` class and `usbpd-dual-role.c` present | code-backed | kernel `drivers/ccic/` (reading only; whether a userspace write can request DR_SWAP not confirmed) |
| Phone plugged into a radio normally lands as UFP (device) and sink (charges at VBUS, up to 900 mA draw per defconfig); charging while enumerated as device is the normal case | hypothesis | inferred from the above; not observed |
| To complete the CarPlay swap the phone must become the **USB host while the radio still supplies VBUS**. Samsung's host path (`otg_drv_vbus(1)`) turns on the OTG boost, which would back-feed the radio's VBUS. A data-only swap needs either PD DR_Swap accepted by a PD-capable radio, or a kernel change to force host mode without VBUS drive | **hypothesis, hardware risk** | nothing in public source shows this; needs a scope/multimeter before any test |
| Radio port type matters: a USB-A port (A-to-C cable, Rp-only) gives no CC-based swap; a Type-C DRP port could use PD DR_Swap | hypothesis | USB-C spec reasoning |
| What a Lightning iPhone does to swap | unknown | `research/carplay-protocol.md` row 10 |

## 3. What the radio expects
Primary evidence: USENIX VehicleSec 2025, Won et al., third-party "Magic Box" adapter analysed with a Beagle USB 5000 against real iPhone 8/12, https://www.usenix.org/system/files/vehiclesec25-won.pdf (I extracted the PDF text and read section 4, Tables 1 to 3).

| Item | Level | Detail |
|---|---|---|
| Radio is host at first, detects device by **VID 0x05AC, PID 0x12NN** only ("Finding 1": IVI verifies only product and vendor IDs) | verified | device descriptor seen: `12 01 00 02 00 00 00 40 ac 05 a8 12 02 13 01 02 03 04` = USB 2.0, bMaxPacketSize0 64, 05ac:12a8, bcdDevice 0x1302, 3 strings |
| Role-switch is **three EP0 vendor requests to the device**: (1) `C0 53 00 00 00 00 04 00` (IN, answer `01 00 00 00`); (2) `40 51 01 00 00 00 ?? 00` (OUT, wValue 1). Then the bus drops and re-enumerates with the iPhone/adapter as **host** and the radio as **device** | verified (paper). The paper's Table 3 shows wLength 0 and its text shows 4 for step 2, so the handler accepts both | matches QNX `RoleSwap_AppleDevice` PID range 0x1200-0x12FF and the lkml vendor request 0x51 on 0x12a8 cited in `research/carplay-protocol.md` rows 2, 3, 5 |
| Wording check on direction: the paper says the swap makes "the iPhone the USB host" | verified | also matches the open `carplayd` project (Unlicense), where the iPhone becomes host and the Pi acts as gadget with iAP2 + NCM: https://github.com/lvalen91/carplayd |
| After the swap the radio (now device) exposes an iAP2 interface plus **CDC NCM**: control interface class 0x02 subclass 0x0D protocol 0x00, data interface class 0x0A subclass 0 protocol 0x01; interface numbers distinct | verified (paper Tables 1, 2) | the S10 is then a *host* talking to these, so GadgetManager does not create NCM; CarPlayService/usbfs does |
| iAP2 interface class/subclass/protocol, endpoints, config number, extra descriptors the radio checks before the swap | **unknown** | `carplay-protocol.md` row 8. `FfsInterface` defaults to vendor class 0xFF placeholders and must be replaced by a real iPhone descriptor dump |
| MFi authentication happens radio to phone (radio certificate 928 bytes, challenge/response); the phone side only checks it | verified for flow, **hypothesis** that the S10 can skip verifying | paper section 4.2.3; no MFi bypass attempted or needed for the USB layer |
| Carlinkit VID/PID 0x1314/0x1520 are receiver-dongle IDs, not usable here | verified | task brief |

Design consequence: stage A (what GadgetManager implements) = enumerate as 05ac:12xx, answer 0x53 with `01 00 00 00`, accept 0x51/wValue=1, then trigger the role swap. The vendor requests are device-recipient and arrive before SET_CONFIGURATION. **Code-backed**: kernel 4.14.356 `f_fs.c` `ffs_func_req_match()` delivers them to userspace only if the FFS function declared `FUNCTIONFS_ALL_CTRL_RECIP` (64) and `FUNCTIONFS_CONFIG0_SETUP` (128), and `composite.c` (~line 2100) walks `cdev->configs` with `config0=true` when no configuration is set. `build_ffs_descriptors()` sets both flags by default. No kernel patch should be needed (**hypothesis until run on hardware**).

## 4. Android integration (drafts, **not run, not compiled against AOSP**)
- `usb/GadgetManager/android/s10gadgetd.rc`: mounts a functionfs instance (same pattern as adb/mtp in the Lineage rc), disabled service started via `sys.s10.carplay=1`. ueventd: nothing needed for configfs or `/dev/usb-ffs` (not uevent nodes); later host phase needs `/dev/bus/usb/*` access, normally already `0660 root usb` in the system ueventd.rc (not verified here).
- `usb/GadgetManager/android/s10_gadgetd.te`: domain with rights on `configfs`, `sysfs_udc`, `functionfs`, `usb_device`. Type names are from AOSP policy knowledge; **not compiled**. Risk: AOSP neverallow rules may forbid a non-HAL domain writing configfs. Fallback listed in the file (run through `hal_usb_gadget_default` or an init `exec`).
- SELinux must stay enforcing; no permissive plan.

## 5. What was actually built and run
`usb/GadgetManager/` (C++17, no Android deps): `Fs` wrapper with dry-run and fake-tree modes, `GadgetManager` (setup, bind with UDC auto-pick, unbind, teardown, takeover of `g1`, status), FunctionFS v2 descriptor/strings builder, `RoleSwitchHandler` for the 0x53/0x51 sequence.
- Run: `cd usb/GadgetManager && make test` gave **46 checks, 0 failures** on this host (g++ 13.3, Ubuntu 24.04). Covers dry-run touches nothing, build+bind+teardown on a temp-dir fake configfs, takeover order, missing-UDC failure, FFS blob lengths/flags, role-switch order and stall cases.
- CLI smoke-tested against a fake root and `--dry-run`. `scripts/usb-probe.sh` was run with no phone: prints a notice, exit 0; the device path was only exercised with a stub `adb` (shows the script parses, not that the real commands work on the phone).
- **Limits**: tests use a plain directory, so real configfs semantics (attribute auto-creation, EBUSY on rmdir, ordering) are emulated, not verified. **Not implemented**: the FFS `ep0` serve loop (open ep0, write blobs, read `usb_functionfs_event`, answer setup via the handler, then `bind`), the host-mode swap, anything on a real UDC.

## 6. Blockers and next steps
1. Hardware: phone on adb, a real radio, and a USB analyzer or Linux host-in-the-middle (usbmon) to capture a real iPhone's descriptors and the swap. Without that, descriptors beyond the device descriptor are guesses.
2. Data-only role swap with radio-supplied VBUS: needs PD capability knowledge of the target radio and a kernel/hardware plan; do not enable OTG boost against a powered port.
3. Confirm the Lineage gadget HAL cannot reclaim the UDC while the manager holds it.
4. Remaining protocol layers are Agent B's blockers (NDA specs), independent of USB.
