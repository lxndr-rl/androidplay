# Zlink5 5.4.101 static analysis (2026-11-14)

Input: `zlink-5-4-101.apk` supplied by the user (sha256 f18cadb2d2c318cfa54131b1360c2db8840af7a7ab567f6f4e4c05ce239e6f80),
package com.zjinnova.zlink, armeabi-v7a only. Method: `strings`/`grep` on the native libraries, `aapt2` on the manifest.
Nothing executed. The Java layer is packed (classes.dex 70 KB stub, libshella/libshell-super), so it was NOT read.
Everything below is "strings present in the binary", not observed behaviour. Whether the user's radio runs this exact build is unknown.

## Facts (strings in libzjL10001.so, the core)
- Head-unit app: AirPlay *receiver* (libAirPlay/libzjAirPlay, "CarPlayControl", "AirPlayReceiverSession...", HID knob/touch/telephony), plus Android Auto (AOA: `AndroidAutoStart`, `aoa_mirror_*`, `aap_device_open`), Baidu CarLife, Huawei HiCar.
- It opens USB devices itself with libusb (`libusb_open_device_with_vid_pid`, hotplug callbacks, `usb_monitor_start`, `usb_device_check`), logs "got a apple device".
- `CarPlayModeStart: libusb_control_transfer fail` / `...open_device_with_vid_pid fail`: it sends a USB control transfer to the Apple device to start CarPlay mode (consistent with the OrangeCar role-swap request; request bytes NOT extracted).
- It then reconfigures ITS OWN USB port: strings that write host/device/peripheral to many vendor sysfs role nodes (sunxi `usb0modesel_state`, `mode_switch`, extcon `role`, dwc3_mode, ...) and a ConfigFS gadget `/config/usb_gadget/g1` with functions `iap.gs0`, `iap2.gs0`, `ncm.gs0`, device nodes `/dev/zjinnova_iap`, `/dev/zjinnova_iap2`, `enable_iap_ncm*`. So for wired CarPlay the head unit becomes a USB *device* exposing iAP2 + NCM.
- MFi: `zj_mfi_init`, `Err: Mfi chip not available !`, `setMfiId`: the radio needs an Apple authentication chip (or equivalent) for its own side.
- `libusbmuxd.so` is a real usbmuxd build run as a daemon (`/dev/z-usbmuxd -f&`) with a local TCP forward (`forward: local=%d, remote=%d`, "no connected phone"). Its purpose in the CarPlay path is NOT established: it may serve an iOS companion app or lockdown pairing.
- Wireless: `Got a wireless carplay device`, bonjour/mDNS (libzmdnsd), `get_wireless_ip_addr`.

## Consequence for S10CarOS (hypothesis, from the above)
- Zlink5 appears to implement wired CarPlay as the role-swap model (Model 1): control request to the iPhone, HU switches to gadget (iAP2 + NCM), the PHONE becomes USB host. That would need the S10+ to become a USB host toward the radio (USB-003: only rejected against a PC so far) and to talk iAP2 to `/dev/zjinnova_iap2` and NCM, not usbmux. This contradicts the earlier guess that a generic Android radio would use usbmux. Not verified on the radio; the observation log of the phone attached to the radio will tell.
- A plain Android phone is probed through the Android Auto / AOA path. That alone would explain "connecting" on the radio without any CarPlay involvement.
