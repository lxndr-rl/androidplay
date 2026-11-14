# Source inventory

Agent G, 2026-11-14. Classes: (a) source available, (b) announced but unpublished or removed, (c) binary only.
Role: S = sender (acts as phone), R = receiver (acts as car/dongle host), - = n/a. Transport: USB, WL = wireless.
Verification: V = verified by API/fetch on 2026-11-14, W = via Wayback snapshot, S = secondary, U = unverified.
Last date = last push (repo) or last release. Licence from the GitHub API unless noted ("none" = no licence file detected).

| # | Item | URL | Class | Licence | Last date | State | Role/Transport | Contains | Reuse | Ver |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | okcar-os/android (OkcarOS manifest) | https://github.com/okcar-os/android | (b) removed | README says open source; no LICENSE file seen | 2024-04-02 (last commit) | repo/org 404 today; present 2026-05-09 (512 stars, 1185 commits, 0 releases) | S / USB | repo-tool manifest + READMEs | via mirror #2 | W |
| 2 | TeaqariaWTF/android-carplay (mirror of #1) | https://github.com/TeaqariaWTF/android-carplay | (a) | none | 2024-04-16 | static fork, 1 star | S / USB | `default.xml` (1082 projects, Lineage 20, AOSP android-13.0.0_r75), README | reference for tree composition | V |
| 3 | shockeyzhang/android (mirror of #1) | https://github.com/shockeyzhang/android | (a) | none | 2024-04-02 | static fork | S / USB | same as #2 | same | V |
| 4 | shockeyzhang/okcar (vendor overlay) | https://github.com/shockeyzhang/okcar | (a) | **none** | 2024-04-03 | static copy of okcar-os/okcar | S / USB | main.mk, product.mk, init rc (incl. exynos9820), sepolicy, overlays, Pccall (C), APK fetcher, public OTA/recovery certs | read-only reference | V |
| 5 | OkAutoconn / OkLauncher / OkMinusScreen / OkAppStore APKs | https://cdn.okcaros.com/apps/prebuilds/ (4 files) | (c) | proprietary (assumed) | build tag 0403 (2024-04-03) | not downloaded; URL liveness not tested | S | CarPlay engine, launcher, app store | none | V (hashes only) |
| 6 | shockeyzhang/android_frameworks_base | https://github.com/shockeyzhang/android_frameworks_base | (a) | Apache-2.0 (NOASSERTION in API) | 2024-04-03 | static, 5.3 GB | S | Lineage 20 frameworks + Okcar commits | cherry-pick ideas only | V |
| 7 | shockeyzhang/android_bionic, _system_netd, _packages_services_Mtp, _packages_apps_Settings, _packages_modules_adb, _external_libusb (fork of zhiliao007) and 10 more | https://github.com/shockeyzhang?tab=repositories | (a) | mixed AOSP (Apache-2.0/BSD), mostly "none" in API | 2023-11 to 2024-04 | static | S | Okcar-patched AOSP components | diff reference | V |
| 8 | dev-sm8350/kernel_oneplus_sm8350 | https://github.com/dev-sm8350/kernel_oneplus_sm8350 | (a) | GPL-2.0 | 2025-11-14 | active | S / USB | `drivers/usb/okcar/okcar.c`, `f_okcar_{image,mobile,usb_ethernet}.c`, dwc3-msm hooks | high as kernel reference (GPL-2.0) | V |
| 9 | Madara273/Sakura.MTX-OP9-Pro | https://github.com/Madara273/Sakura.MTX-OP9-Pro | (a) | GPL-2.0 | 2026-11-14 | active | S / USB | same Okcar kernel files (a 2025-02-13 commit refactors okcar.c) | same | V |
| 10 | shockeyzhang/android_kernel_xiaomi_sm8350 | https://github.com/shockeyzhang/android_kernel_xiaomi_sm8350 | (a) | GPL-2.0 | 2024-04-11 | fork of JiangHanfeng; contents not inspected | S / USB | Okcar kernel for sm8350 [U] | same | V/U |
| 11 | yyjjls/android_device_xiaomi_msm8998-common | https://github.com/yyjjls/android_device_xiaomi_msm8998-common | (a) | none | 2023-10-26 | static | S / USB | device-common tree for OkCarOS | reference | V |
| 12 | Samsung Galaxy S10/S10+ OkcarOS device tree/kernel | none found | (b) / U | n/a | n/a | only an Exynos 9820 init rc and sepolicy dir exist in #4; third-party blog/search summary lists "Galaxy S10" | S / USB | n/a | n/a | U |
| 13 | Redmi Note 8 (ginkgo) OkcarOS port | http://birdzhang.xyz/tags/okcaros/index.html | (b) | n/a | blog 2023-10-18, upd 2024-04-14 | upstream repo under okcar-os (now 404); no mirror found | S / USB | blog only | none | S |
| 14 | okcaros.com site, wiki, download | https://www.okcaros.com, https://wiki.okcaros.com/devices/, https://download.okcaros.com | (c) | proprietary (ROM images) | unknown | TLS errors (wrong host cert / expired); not retrieved; no Wayback snapshot of okcaros.com | S | ROM zips, device list | none | V (TLS errors) |
| 15 | OkcarOS protocol library | https://web.archive.org/web/2026/https://github.com/okcar-os/android/issues (issue #26) | (b)/(c) | n/a | issue 2026-04-15 | requested to be open-sourced, never published | S | n/a | none | W |
| 16 | ZTXBOSS666/OKCarOS_API_Server | https://github.com/ZTXBOSS666/OKCarOS_API_Server | (a) clone | CC BY-NC 4.0 per README (API: NOASSERTION) | 2026-09-01 | 2 commits | n/a | third-party clone of the activation backend | **do not use** | V |
| 17 | OkCarLink | https://github.com/Enthlinn/okcarlink | (a) + (c) dependency bundle | MIT | 2026-11-11 (release 1.0.2 on 2026-11-10) | active, 10 days old | S / USB + WL | Java daemon, C natives, KernelSU module, docs | high (after provenance check) | V |
| 18 | carjack / Slipstream | https://github.com/cfraz89/carjack | (a) | Apache-2.0 OR MIT | 2026-11-14 (slipstream-v0.3.0) | active | S / WL | libcarjack (iAP2, CarPlay, AirPlay), Android app, Linux app, notes | high | V |
| 19 | catplay (AirPlay dependency of carjack) | https://github.com/cfraz89/catplay | (a) | **none** | 2026-10-17 | fork flag true | S / WL | Rust CarPlay/AirPlay impl | blocked until licensed | V |
| 20 | CarDeck | https://github.com/shaonialife/cardeck | (a) | MIT | 2026-10-24 (forks to 2026-11-14) | active, 10+ forks | S / WL | Android sender app, protocol notes | medium | V |
| 21 | Pi2Carplay | https://github.com/Fotsbeats/Pi2Carplay | (a) + (c) SDK | GPL-2.0-or-later for own src; CC BY-NC-SA 4.0 base; binary SDK | 2026-10-30 (v0.2.0) | active, 2 weeks | S / USB | launcher, capture, overlays, adapter; ARM32 **binary** CarPlay SDK | design reference only | V |
| 22 | OrangeCar | https://github.com/sbuysse/orangecar | (a) | GPL-3.0 | 2026-10-10 | one-shot | S / USB | Rust CarPlay sender, `docs/protocol.md` | high as spec | V |
| 23 | CarDroid | https://github.com/pavunato/CarDroid | (a) | **none** | 2026-11-08 (v0.1.3) | active | S / WL | Android sender app; classes named Okcar*/MFi (not opened) | **excluded** | V |
| 24 | DiPlay | https://github.com/shihabal3amri/DiPlay | (a) | GPL-3.0 | 2026-11-12 | active | R / USB+WL | Android head-unit CarPlay receiver | test bench | V |
| 25 | AndroidPlay / WinPlay / MacPlay / LinuxPlay | https://github.com/Roylyl/AndroidPlay | (a) | GPL-3.0 (AGPL parts per README) | 2026-10-17 to 10-27 | active | R / WL (MacPlay USB) | receivers | test bench | V |
| 26 | react-carplay, node-CarPlay | https://github.com/rhysmorgan134/react-carplay | (a) | MIT | 2026-01-14 / 2025-06-08 | slow | R / USB dongle | Carlinkit dongle drivers/UI | background | V |
| 27 | pycarplay, carplay-receiver | https://github.com/electric-monk/pycarplay | (a) | MIT | 2020-12-06 / 2020-11-29 | dead | R / USB dongle | first Autobox reverse-engineering | background | V |
| 28 | carplay-client | https://github.com/rayphee/carplay-client | (a) | none | 2022-05-20 | dead | R / USB dongle | Rust Autobox client | none | V |
| 29 | FastCarPlay | https://github.com/niellun/FastCarPlay | (a) | GPL-3.0 | 2026-03-30 | slow | R / USB dongle | Linux receiver | background | V |
| 30 | WirelessCarPlay (Pi) | https://github.com/45clouds/WirelessCarPlay | (a) | MIT | 2024-06-24 | slow | relay / USB+WL | Pi relay of an iPhone | background | V |
| 31 | scrcpy | https://github.com/Genymobile/scrcpy | (a) | Apache-2.0 | 2026-11-12 | very active | - | virtual display + input injection | high (Apache-2.0) | V |
| 32 | OpenAuto / aasdk (opencardev forks) | https://github.com/opencardev/openauto | (a) | GPL-3.0 / aasdk none | 2026-02-10 / 2026-06-11 | slow | R / USB+WL (Android Auto) | AA head-unit emulator | background | V |
| 33 | AACS | https://github.com/tomasz-grobelny/AACS | (a) | GPL-3.0 | 2024-02-29 | dead | S / USB (Android Auto) | AA server via USB gadget | concept only | V |
| 34 | OpenAA | https://github.com/manum45/OpenAA | (a) | GPL-3.0 | 2026-08-16 | active | S (Android Auto) | experimental AA app; mentions okcaros | background | V |
| 35 | XDA thread "Has anyone used OkcarOS" | https://xdaforums.com/t/has-anyone-used-the-android-custom-rom-okcaros-connected-with-carplay.4641525/ | n/a | n/a | 2025 [S] | HTTP 403 to fetch | n/a | user reports | none | S/U |
| 36 | Telegram / Discord for OkcarOS | none found | n/a | n/a | n/a | not found | n/a | n/a | n/a | U |
