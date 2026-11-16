# OkcarOS recovery report

Agent G, 2026-11-14. Every claim carries a URL and a verification tag.
Tags: [V] verified by direct API/fetch today; [W] verified through a web.archive.org snapshot (page text as archived); [S] secondary source (blog, search summary); [U] unverified.

## 1. Verdict

OkcarOS is a LineageOS 20 (Android 13) based ROM for ordinary phones. It makes the phone present itself over USB as an Apple device to a CarPlay head unit. It is **partly open source**:

| Layer | Status |
|---|---|
| AOSP/LineageOS tree, manifest, framework patches, sepolicy, init scripts, USB gadget config | (a) source was public; all of it now unreachable at the original location (see 3) |
| Kernel USB gadget drivers (`f_okcar_*`, `/dev/okcar` mode switch) | (a) source public, GPL-2.0, surviving in third-party kernel trees |
| The CarPlay protocol engine and the apps that drive it (`OkAutoconn`, `OkLauncher`, `OkMinusScreen`, `OkAppStore`) | (c) **binary-only**, downloaded at build time from the vendor CDN; no source anywhere |
| Licensing / activation backend | (c) closed. Only a third-party clone exists (see 5) |

The README advertises "open source". The part that does the CarPlay work is not.
Evidence that the protocol engine is closed:
- The manifest build step downloads prebuilt APKs from `cdn.okcaros.com` and checks them by SHA-256 [V].
- Upstream issue #26, "will opensource carplay protocol lib?", opened 2026-04-15, was still open in the 2026 archive snapshot [W].
- Issue #23 asks for a 1-3 day trial licence, so there is a paid or activated tier [W].
- A third-party summary of the paid tier (ads, watermark, random disconnects, frame-rate lock) is [S/U]. See the Techjunkie post at https://x.com/Techjunkie_Aman/status/2053750534486806613. It returned HTTP 402 to WebFetch and the summary came only through a search snippet.

## 2. Timeline (verified dates)

| Date | Event | Source |
|---|---|---|
| 2023-09-25 | Kernel commit "drivers: Providing kernel support for okcar" (author Leo) | https://github.com/dev-sm8350/kernel_oneplus_sm8350/commits/15.2/drivers/usb/okcar/okcar.c [V] |
| 2023-10-18 | Redmi Note 8 (ginkgo) port blog post. Update 2024-04-14 says the port was merged upstream | http://birdzhang.xyz/tags/okcaros/index.html [S] |
| 2023-11 | Commits by "herodhb": "Add OKCAR prefix to model/manufacturer/brand" (bionic), `_airplay_tcp` IPv6 (netd) | https://github.com/shockeyzhang/android_bionic, https://github.com/shockeyzhang/android_system_netd [V] |
| 2024-01-16 | USB3 support added to the `f_okcar_*` drivers | kernel commit `d9f87fbf` [V] |
| 2024-04-02/03 | Last upstream commits found. Last okcar-os/android commit "Add android_system_nfc"; last okcar repo commit "Bump okcar app version" (Leo) | https://github.com/TeaqariaWTF/android-carplay/commits/okcar-1.0 [V] |
| 2025-09-17 | Wayback snapshot of the org page: 63 repos, 49 followers, contact okcar@okcaros.com | https://web.archive.org/web/20250917064351/https://github.com/okcar-os [W] |
| 2026-04-15 | Issue #26 opened (protocol lib open-sourcing request) | https://web.archive.org/web/2026/https://github.com/okcar-os/android/issues [W] |
| 2026-05-09 | Snapshot of `okcar-os/android` still public: 512 stars, 45 forks, 1185 commits, **0 releases**, branch `okcar-1.0` | https://web.archive.org/web/20260509125222/https://github.com/okcar-os/android [W] |
| by 2026-11-14 | `okcar-os` org and `okcar-os/android` return 404 (`gh api`, `git ls-remote`). The exact deletion date is unknown (between 2026-05-09 and 2026-11-14) [U] | [V] |

The org's own README names these external resources:
- Website https://www.okcaros.com
- Device list https://wiki.okcaros.com/devices/
- ROM downloads https://download.okcaros.com
- Build guide: "OkcarOS Build Guide", no URL given

Today `okcaros.com` presents a TLS certificate for `backend.okcarbox.com` (hostname mismatch). `wiki.okcaros.com` presents an expired certificate. Both are [V]. I did not bypass either error. The device list and the build guide are therefore unread [U].

## 3. Surviving copies (the recoverable source)

The original `okcar-os/*` repos are gone. These survive:

| Copy | What it holds | Verified state |
|---|---|---|
| https://github.com/TeaqariaWTF/android-carplay (fork of the manifest repo, branch `okcar-1.0`) | `default.xml` (127 KB, 1082 `<project>` entries), README, README_cn, imgs. 16.8 MB, no releases or tags | last push 2024-04-16 [V] |
| https://github.com/shockeyzhang/android (fork of the same manifest repo, created 2025-03-13) | same, 16.8 MB | last push 2024-04-02 [V] |
| https://github.com/shockeyzhang/okcar (not a fork; the vendor overlay, 65 KB) | `main.mk`, `product.mk`, `init/*.rc`, `sepolicy/`, `overlay/`, `apps/Pccall` (C), `apps/AppImport` (APK fetcher), OTA/recovery **public** certs | last push 2024-04-03 [V] |
| https://github.com/shockeyzhang/android_frameworks_base (5.3 GB, branch `okcar-1.0`) | LineageOS 20 frameworks with Okcar commits, e.g. "Support set default launcher by addPreferredActivity" | last push 2024-04-03 [V] |
| shockeyzhang mirrors of `android_{bionic, system_netd, packages_services_Mtp, packages_apps_Settings, packages_modules_adb, external_libusb, vendor_lineage, packages_apps_SetupWizard, packages_apps_LineageParts, bootable_recovery, system_nfc, hardware_xiaomi, tools_extract-utils, system_apex, packages_apps_Updater, packages_apps_AudioFX}` | Okcar-patched AOSP/Lineage components | pushed 2023-11 to 2024-04 [V] |
| https://github.com/dev-sm8350/kernel_oneplus_sm8350 (GPL-2.0, branch 15.2, last push 2025-11-14) and https://github.com/Madara273/Sakura.MTX-OP9-Pro (GPL-2.0, pushed 2026-11-14) | Okcar kernel code: `drivers/usb/okcar/okcar.c`, `drivers/usb/gadget/function/f_okcar_{image,mobile,usb_ethernet}.c`, hooks in `dwc3-msm.c` | [V] |
| https://github.com/shockeyzhang/android_kernel_xiaomi_sm8350 (fork, GPL-2.0, 2024-04-11) | Okcar kernel for Xiaomi sm8350 (Mi 11 family) | [V] (contents not inspected) |
| https://github.com/yyjjls/android_device_xiaomi_msm8998-common (no licence, last push 2023-10-26) | Device-common tree "for OkCarOS" (Xiaomi msm8998 devices) | [V] |

Two items could not be recovered:
- No device trees or kernels for ginkgo, d2s, beyond2lte etc. were found on GitHub, so those are [U].
- I could not enumerate the other ~50 org repos. The 2025 snapshot lists only 10 by name (kernel_xiaomi_sm8350, okcar, device_oneplus_sm8350-common, frameworks_base, SetupWizard, vendor_lineage, LineageParts, bootable_recovery, android, system_nfc). It also shows `lawnchair` and `git_batch_push` [W].

`Obsidian6s/android` is the fork parent shown by the API and now returns 404. That implies an earlier owner or name for the manifest repo [U].

Wayback has no snapshot of okcaros.com and none of `okcar-os/okcar`. Only the org page and `okcar-os/android` have snapshots (availability API).

## 4. What the recovered source shows (technical)

### 4.1 Build composition [V]
- `default.xml`: remotes `okcar-os` (GitHub, branch `okcar-1.0`), `aosp` pinned to tag `android-13.0.0_r75`, and Lineage upstream. Only about 14 projects are Okcar forks: build, bionic, recovery, libusb, frameworks/base, okcar, Settings, Trebuchet, adb, Connectivity, Mtp, netd, nfc.
- A second remote `okcar-os-private` over SSH is declared, so some repos were intentionally private [V].
- `okcar/main.mk` sets `ro.okcaros.*` properties, version 1.0, and `sys.updater.allow_downgrading=true`. A commented-out line mentions a "promoter ID" for reseller profit. This is evidence of a commercial reseller scheme [V].
- `product.mk` packages `pccall`, `autoconn`, `oklauncher`, `okappstore`, `minusscreen` and 3 overlays. It copies `init.exynos9820.okcar.rc` (the S10 family SoC) and `init.qcom.okcar.rc`. A sepolicy dir `sepolicy/universal9820/` exists [V]. So the Exynos 9820 platform was supported in the vendor overlay. That fact does not prove an S10+ build was ever published [U].

### 4.2 Binary-only apps [V]
`okcar_apps.json` and `app_sync.py` download these over HTTPS and verify SHA-256:

| App | File | SHA-256 (full, from `okcar_apps.json`) |
|---|---|---|
| OkAutoconn | `autoconn_401618_0403_19_06.apk` | 1873b6c77ff3d52c673e1dca36efeecd884b9401eacb65f80214e7ba1fcee788 |
| OkMinusScreen | `minusscreen_37_0403_19_06.apk` | c88dba7474c3937025c729da0a86945d39b0d4e72d4f709e78da997c6eb0ba96 |
| OkAppStore | `okappstore_13_0403_19_06.apk` | 330d8ab510e2bdb00a9b5290ecabdde34450dbb616fbdb60b1214703c22599d6 |
| OkLauncher | `oklauncher_12010014_0403_19_07.apk` | 79314fd5a7957721a7202aa39c8e80dfa784b49dee7648c861dd47865ea53ba8 |

All four are fetched from `cdn.okcaros.com/apps/prebuilds/`. I did not download them (instruction: no binaries). `OkAutoconn` is the CarPlay engine. Its `privapp-permissions` file requests a large set of signature or privileged permissions. The ones that matter for our design:
- `CAPTURE_SECURE_VIDEO_OUTPUT`, `CAPTURE_AUDIO_OUTPUT`, `INJECT_EVENTS`, `READ_FRAME_BUFFER`
- `MANAGE_USB`, `TETHER_PRIVILEGED`
- `INSTALL_PACKAGES`, `WRITE_SECURE_SETTINGS`

That confirms the app-side architecture: a privileged system app handles capture, audio, input injection and networking. It is a design reference. The code itself is unavailable.

### 4.3 USB identity and gadget plumbing [V]
`init.exynos9820.okcar.rc` (on `sys.usb.config=okcar`) writes:
- VID `0x05AC` / PID `0x12A8`, manufacturer "Apple Inc.", product "iPhone", with a hard-coded serial string.
- Four configfs configs: "PTP", "iPod USB Interface", "PTP + Apple Mobile Device", "PTP + Apple Mobile Device + Apple USB Ethernet".
- Custom functions `okcar_image` (class 6/1/1, still image), `okcar_mobile` (string "Apple USB Multiplexor") and `okcar_usb_ethernet` (vendor class 0xFF, subclass 253, protocol 1), plus `audio_source` and `hid` (208-byte report descriptor `okcar.hidreport.bin`).
- Modprobe of `cdc_ncm` on demand.

The `f_okcar_*` kernel functions are modified copies of Google's `f_accessory.c` (GPL-2.0, "Copyright 2011 Google"). They are built into the kernel; ConfigFS alone is not enough. `drivers/usb/okcar/okcar.c` exposes `/dev/okcar` with the command `usbmode,1,<mode>` (1 = device, 2 = host), implemented through `okcar_usbmode_toggle` hooks in `dwc3-msm.c`. This mode switch is what lets the phone flip to USB host after the car's role-switch request.

The kernel half is therefore non-trivial. It touches the DWC3 glue of each SoC, and on Exynos 9820 the DWC3 glue is a different driver. The Samsung port is [U].

`Pccall` (C, `apps/Pccall/src/main.c`) is a small root helper with three functions:
- `-i` sets IP rules.
- `-b` handles "boot when charging" through `/metadata/bootstat`.
- `-l` writes dmesg to `/sdcard`.

Other recovered build details:
- `android_packages_services_Mtp` carries the commit "CarPlay and MTP are not compatible".
- `system_netd` has a patch for `_airplay_tcp` over IPv6.
- `bionic` prefixes the model, manufacturer and brand strings with "OKCAR".

Reuse of any of this is a design reference for Agent C (kernel and USB gadget) and Agent B (protocol). It does not change what we may do about MFi and keys (see AGENTS.md rules).

### 4.4 Authentication posture, unknown
The recovered tree contains no MFi or iAP2 authentication code, because that lives in the closed `OkAutoconn`. Whether OkcarOS passes real MFi authentication, or relies on head units accepting the self-reported result, is [U]. I did not look into it further (rule: no MFi bypass).

## 5. Related and derivative OkcarOS artifacts

| Item | URL | State |
|---|---|---|
| OkcarOS API Server (PHP) | https://github.com/ZTXBOSS666/OKCarOS_API_Server | Created 2026-09-01, 2 commits, 44 KB. Its README says it is a **reverse-engineered clone** of the official verification backend (device registration, activation codes, RSA+JWT), unaffiliated, "no commercial use". Licence is CC BY-NC 4.0 (API shows NOASSERTION). It ships `cert/*.pem` and `server.key` files. The same author also published `dopacrack` (MIT, 2026-03-17), and an AGPL `new-api-ztx`. **Do not use or run.** It exists only as evidence that activation is server-side |
| OkCarLink (KernelSU/Magisk module) | https://github.com/Enthlinn/okcarlink | See `alternatives.md`. Internal names keep the `okcar-*` prefix, so it appears to be derived from OkcarOS's internal kit; its docs say it is "the public release of an internal kit" [V]. Provenance relation to the OkcarOS vendor is [U] |
| CarDroid (Android launcher + wireless sender) | https://github.com/pavunato/CarDroid | Source includes `OkcarMfiSapImpl.kt` and `OkcarBypassProvider.kt` plus `com.okcar.tool.encrypter` classes (search hits only; **not opened**). No licence. Flagged: possible vendor-derived or MFi-related code, out of bounds for Duende |
| OpenAA README | https://github.com/manum45/OpenAA | Lists okcaros as "server for Apple Carplay on android, requires to be flashed via recovery" (GPL-3.0 repo, pushed 2026-08-16) [V] |
| XDA thread | https://xdaforums.com/t/has-anyone-used-the-android-custom-rom-okcaros-connected-with-carplay.4641525/ | HTTP 403 to WebFetch. Search snippet says a 2025 user reported it did not work on their CarPlay, Android Auto and dual-protocol cars, and another replied they used it until recently [S/U] |

## 6. Unverified or not found

- Telegram or Discord community for OkcarOS: not found [U].
- GitLab or other mirrors of `okcar-os/android`: not found by GitHub search [U].
- Any release tag: none (the 2026 snapshot shows 0 releases; the manifest fork has no tags or releases) [W/V].
- The official ROM list and any Galaxy S10 / S10+ build: the only mention is a third-party write-up (search summary) listing Galaxy S10 [S/U]. The only hard evidence is the Exynos 9820 overlay files in `okcar` [V].
- Whether the closed engine is a wired-only or also a wireless implementation: issue #17 asks for wireless CarPlay, so wired-only is likely [W].
- Whether the `okcar-os` org was deleted voluntarily, taken down by DMCA, or renamed: unknown [U].

## 7. Reuse assessment for Duende

1. Reusable (clean): the Apple-gadget configuration layout (configs, function names, HID report, role-switch sequence) as a *specification* for Agent C. The code is GPL-2.0 and the base is Google GPL-2.0 `f_accessory.c`, so any derived kernel code must stay GPL-2.0.
2. Reusable as reference: `main.mk` property scheme and the `product.mk`/overlay pattern, sepolicy domain `okcarte`, and the privapp-permission list for the engine app. The `okcar` repo has **no licence file**, so the default is all rights reserved; use as read-only inspiration, and write our own.
3. Not reusable: the four APKs (proprietary, activation-bound), the OTA keys (public certs only, not ours to adopt), the activation backend clone.
