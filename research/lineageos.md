# LineageOS for beyond2lte (Galaxy S10+) - research for S10CarOS

Agent A, research date 2026-11-14. Evidence levels: **V** = fetched/queried live from primary source today;
**D** = derived from V data by me; **U** = unverified (no device / not built here).
Licenses: LineageOS device/kernel trees Apache-2.0 (device tree) / GPL-2.0 (kernel, GitHub reports NOASSERTION); wiki CC BY-SA 3.0; vendor blobs (TheMuppets) proprietary Samsung binaries.

## 1. Which branch is real
| Finding | Evidence | Level |
|---|---|---|
| beyond2lte is **officially supported on lineage-23.2 (Android 16)**; wiki lists "Current version 23.2 (Android 16)", maintainer **Linux4**, kernel 4.14, models SM-G975F, SM-G975F/DS, SM-G975N, previously 18.1/19.1/20/21/22.1/22.2/23.0. https://wiki.lineageos.org/devices/beyond2lte/ (site generated 2026-11-13) | V |
| Official nightly exists: `lineage-23.2-20261114-nightly-beyond2lte-signed.zip`, 1,172,882,406 bytes, sdk 36, patch level 2026-11-01, plus boot/dtb/dtbo/recovery/vbmeta/super_empty images. https://download.lineageos.org/api/v2/devices/beyond2lte/builds (API returned 4 builds, 2026-10-24 .. 2026-11-14, weekly cadence) | V |
| Build target list: `beyond2lte userdebug lineage-23.2 W` (weekly). https://raw.githubusercontent.com/LineageOS/hudson/main/lineage-build-targets. No beyond* entry for lineage-24.0. | V |
| `lineage-23.2` manifest pins AOSP `refs/tags/android-16.0.0_r4`. https://raw.githubusercontent.com/LineageOS/android/lineage-23.2/default.xml | V |
| A `lineage-24.0` branch (AOSP `android-17.0.0_r1`) exists in LineageOS/android and in the beyond2lte device tree, but it points at the **same commit (3d04f9dc) as lineage-23.2** (a fresh branch cut, no 24.0 work), TheMuppets has **no lineage-24.0 branch** for beyond2lte vendor blobs, and Hudson does not build it. => **Not usable; 23.2 is the newest real branch.** | V |
| Candidate "23.2/Android 16" confirmed. | V |

## 2. Device tree, kernel, blobs, firmware, recovery
- Device tree `LineageOS/android_device_samsung_beyond2lte`, branch lineage-23.2, head `3d04f9dc` 2026-09-15 "Import hardware/samsung from vendor namespace" (prior: 2026-02-05 libssl VNDK 33 for gpsd). Depends only on `android_device_samsung_exynos9820-common` (lineage.dependencies). Kernel config `exynos9820-beyond2lte_defconfig`, density 560, non-A/B (`non_ab_device.mk`), launched-with P. V
- Common tree is named **`android_device_samsung_exynos9820-common`** (not universal9820), lineage-23.2 head `38af956a` 2026-02-08; dynamic partitions, super size 5976883200 B; pulls slsi sepolicy, hardware/samsung, samsung_slsi-linaro {config,exynos,exynos5,graphics,interfaces,openmax}, samsung_slsi nfc, and the kernel. All have lineage-23.2 branches. V
- Kernel `LineageOS/android_kernel_samsung_exynos9820` (**not** universal9820), branch lineage-23.2, head `a412cfaf` 2026-08-05, Linux **4.14.356-openela-rc1** (Makefile). V
- Vendor blobs: `TheMuppets/proprietary_vendor_samsung_beyond2lte` and `..._exynos9820-common`, lineage-23.2 (beyond2lte head 2026-09-15; common 2026-02-06). Listed in TheMuppets/manifests muppets.xml. `breakfast` fetches them; no phone needed for building. V
- Required firmware: **stock Android 12** base; proprietary-firmware.txt references G975FXXSGHWC2 (SM-G975F). Wiki: must be latest Android 12 stock for the model. https://wiki.lineageos.org/devices/beyond2lte/install/ and https://raw.githubusercontent.com/LineageOS/android_device_samsung_beyond2lte/lineage-23.2/proprietary-firmware.txt . V. Whether HWC2 is the very latest A12 for every region: U.
- Recovery: LineageOS Recovery (`recovery.img`), flashed in Download mode with **samloader-rs** (`samloader flash --partition RECOVERY recovery.img --no-reboot`), then manually boot recovery. Recovery key combo: Vol Up + Bixby + Power with USB connected; Download mode: Vol Down + Bixby then USB. Bootloader unlock via OEM unlock wipes data. Other recoveries "may not work". V (wiki; I did not execute anything).
- Known quirks listed on wiki: **IMS unsupported** (no VoLTE/VoWiFi), **Device Integrity** (snet quirk page) . https://wiki.lineageos.org/quirks/ims/ . V. No per-device bug list found; upgrade page mentions a possible black screen during ADB sideload (search-snippet only). U.
- Mainlining mirror lineage-wiki.mainlining.org is stale (shows 22.2 current) - do not use as authority.

## 3. USB gadget / configfs state (`exynos9820-beyond2lte_defconfig`, lineage-23.2, fetched 2026-11-14; generated header says 4.14.260, kernel Makefile 4.14.356)
Enabled (=y): USB_GADGET, USB_DWC3 + USB_DWC3_DUAL_ROLE + USB_DWC3_EXYNOS, PHY_EXYNOS_USBDRD, USB_CONFIGFS, **USB_CONFIGFS_F_FS (FunctionFS)**, USB_CONFIGFS_ACM, NCM, RNDIS, F_MTP, F_PTP, F_ACC, F_AUDIO_SRC, F_MIDI, F_CONN_GADGET, F_SS_MON_GADGET, F_DM, CONFIGFS_UEVENT, TYPEC, USB_TYPEC_MANAGER_NOTIFIER, plus Samsung `USB_G_ANDROID` and `USB_ANDROID_SAMSUNG_COMPOSITE` (legacy android gadget alongside configfs).
**Not set**: USB_CONFIGFS_F_HID, F_UVC, F_UAC1/UAC2, ECM, EEM, SERIAL, OBEX, MASS_STORAGE, F_PRINTER, TYPEC_UCSI, TYPEC_TCPM.
Implication (D): a custom gadget over FunctionFS bulk endpoints is buildable without a kernel change; HID/UAC would need defconfig changes. Whether DWC3 can actually enumerate to a car head unit as a *CarPlay* device, and the USB role switching behaviour, are **U** (needs hardware; Agent C's scope).
Defconfig URL: https://raw.githubusercontent.com/LineageOS/android_kernel_samsung_exynos9820/lineage-23.2/arch/arm64/configs/exynos9820-beyond2lte_defconfig

## 4. Build requirements (lineage-23.2)
Source: https://wiki.lineageos.org/devices/beyond2lte/build/ (V, 2026-11-14)
- Docs say tested on Ubuntu 20.04; any recent 64-bit Ubuntu LTS works. Ubuntu 24.04 apt has every package in the list (checked with apt-cache policy: none missing). Package list in scripts/setup.sh.
- **RAM: 64 GB for lineage-21 and up. Storage: 400 GB for lineage-21 and up** (+ccache 25-100 GB), SSD advised. Python 3 (python-is-python3). Java: in-tree prebuilts (`prebuilts/jdk/jdk21`, `jdk25` present in default.xml); host JDK not needed. `repo init -u https://github.com/LineageOS/android.git -b lineage-23.2 --git-lfs --no-clone-bundle`; `repo sync` (-j4 -c default); `source build/envsetup.sh; breakfast beyond2lte; brunch beyond2lte`.
- repo tool: https://storage.googleapis.com/git-repo-downloads/repo (HTTP 200).
- Manifest check done: shallow `repo init` (`--depth=1`) of lineage-23.2 plus manifests/local_manifests/s10caros.xml parsed and merged by `repo manifest` (1169 projects, our 5 present). No source sync performed; scratch removed.

### Can this host build it? (host: Ubuntu 24.04.5, 8 cores, 39 GB RAM + 7 GB swap, 189 GB free, JDK17)
- **Officially NO**: RAM 39 < 64 GB and disk 189 < 400 GB (verify.sh fails on both). Tools missing: flex gperf xsltproc lz4 erofs-utils ccache git-lfs protoc imagemagick schedtool pngcrush repo, multilib; no passwordless sudo, so setup.sh needs a password.
- Practical (U, my estimate, not measured): synced source + out for one device commonly lands well over 189 GB with ccache; a build is likely to run out of disk. Possible mitigations (untested): lower `S10CAROS_MIN_*`, more swap/zram, external disk, or build on a larger machine/CI. 39 GB RAM may build slowly; OOM risk at link/R8 steps.
- Alternative needing no source build (D): use the official nightly zip/recovery from download.lineageos.org and add S10CarOS components as apps/overlay or a vendor-image layer later.

## 5. Artifacts
scripts/setup.sh (idempotent, `--dry-run`), scripts/verify.sh (exit 1 if unmet), scripts/build.sh (needs `S10CAROS_CONFIRM_BUILD=1`; init|sync|build|all), manifests/local_manifests/s10caros.xml. Outputs of runs on this host: see Agent A final report (verify.sh exit 1: RAM, disk, 12 missing tools, multilib packages; bash -n OK for all; setup.sh --dry-run exit 0; build.sh without env exit 3).
