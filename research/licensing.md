# Licensing compatibility for reuse in a LineageOS-based ROM

Agent G, 2026-11-14. Engineering assessment, not legal advice. Licences are those reported by the GitHub API or the repo's own files, checked 2026-11-14.

## 1. Ground rules for a LineageOS 20/21 ROM

- LineageOS userland is mostly Apache-2.0 (AOSP), kernel is GPL-2.0-only, vendor blobs are proprietary and shipped as binaries.
- A ROM is a collection of separate programs, so permissive and copyleft components can coexist in one image (aggregation). Copyleft binds only the component and what is linked into it.
- Rules used below:
  - Apache-2.0, MIT, BSD: OK anywhere, keep notices. Apache-2.0 patent grant is fine.
  - GPL-2.0-only: OK for kernel code and for standalone userspace binaries. Source for those parts must be offered with the ROM.
  - GPL-3.0: OK as a standalone app or binary in `/system` or `/vendor`. Source must be offered for that component. It cannot be merged into the GPL-2.0-only kernel, and it must not be linked into Apache-2.0 frameworks code in a way that makes a combined work.
  - AGPL-3.0: as GPL-3.0, plus network-use clause. Avoid in anything that serves users over a network.
  - CC BY-NC(-SA): not a software licence. Non-commercial and ShareAlike terms conflict with an open ROM that anyone can redistribute. Avoid.
  - No licence file: all rights reserved by default. No copy, no fork-and-ship, no derived code. Read only for ideas, then clean-room.
  - Proprietary or binary-only without a grant: do not redistribute.
- Trademarks: Apple, CarPlay, AirPlay and iPhone are Apple trademarks. An Apple VID/PID and "Apple Inc." strings in the USB descriptor (as OkcarOS does) raise a separate identity and legal question that is not a copyright question. The orchestrator and Agent C should decide it explicitly.

## 2. Per-item matrix

Compat key: OK = reusable with notice; COPYLEFT = reusable if we publish source and keep component separate; REF = read-only reference, reimplement; NO = do not reuse or redistribute.

| Item | Licence | Compat with LineageOS ROM | Notes |
|---|---|---|---|
| OkcarOS manifest and README (TeaqariaWTF/android-carplay, shockeyzhang/android) | none | REF | Repo contains the Lineage manifest. Facts are not copyrightable but the file as a whole is unlicensed. Rebuild our own manifest |
| okcar vendor overlay (shockeyzhang/okcar) | none | REF / NO | Includes `main.mk`, rc files, sepolicy, Pccall C source. Unlicensed. Do not copy; write our own equivalent |
| Four Okcar APKs (OkAutoconn, OkLauncher, OkMinusScreen, OkAppStore) | proprietary, activation-bound | NO | Binary-only, cannot be audited, vendor-controlled licensing |
| OkcarOS ROM images and site downloads | proprietary | NO | Not fetched. Not to be flashed or redistributed by this project |
| okcar-os/* AOSP forks (frameworks/base, bionic, netd, adb, Settings...) | AOSP Apache-2.0 base (API sometimes NOASSERTION) | OK for the AOSP parts; Okcar-authored commits are unlicensed contributions to Apache-2.0 trees | We should write our own patches against Lineage |
| Okcar kernel files `f_okcar_*.c`, `okcar.c` (dev-sm8350, Madara273, shockeyzhang trees) | `MODULE_LICENSE("GPL")` + GPL-2.0 repo; derived from Google `f_accessory.c` (GPL-2.0) | COPYLEFT | Fine for our GPL-2.0-only kernel tree, keep headers and attribution. `okcar.c` header says "Copyright (C) Okcar" with no SPDX line; MODULE_LICENSE("GPL") means GPL v2 or later in kernel terms |
| ZTXBOSS666/OKCarOS_API_Server | CC BY-NC 4.0 per README; API says NOASSERTION; repo ships private keys | NO | Clone of a commercial activation backend; legal and ethical risk; no use case for us |
| OkCarLink (Enthlinn/okcarlink) | MIT | OK | Needs a provenance check: internal `okcar-*` naming and "public release of an internal kit" mean it may contain OkcarOS-internal material relicensed by someone else. Its CI depends on a prebuilt "dependency bundle" (binary). Its native part links BoringSSL (BSD-style/OpenSSL licence, OK) |
| carjack / Slipstream | Apache-2.0 OR MIT | OK | But depends on catplay (below) |
| catplay (cfraz89) | none | NO | Unlicensed; carjack's AirPlay layer depends on it. Ask the author to license it or reimplement |
| CarDeck (shaonialife) | MIT | OK | Includes `reference/` copies of DiPlay and xcertplay sources per README; those are separately licensed (GPL-3.0 for DiPlay), so MIT may not cover all files in that repo. Check each directory |
| Pi2Carplay | GPL-2.0-or-later for own `src/` and `scripts/`; Linux GPL-2.0-only; CC BY-NC-SA 4.0 Android base; **binary ARM32 protocol SDK with no licence grant** | COPYLEFT for own sources, NO for SDK and base image | The SDK's origin is unknown, and the repo itself says distribution review is pending. Do not reuse the SDK or the image. The adapter's design docs may be cited |
| OrangeCar | GPL-3.0 | COPYLEFT (standalone) or REF | Rust code cannot be merged into Apache-2.0 platform code without making that component GPL-3.0. As a separate app or daemon it is fine. Cannot go into the GPL-2.0-only kernel |
| CarDroid (pavunato) | none | NO | Also flagged for MFi-related class names (not opened) |
| DiPlay, DiPlay-MG4, AndroidPlay, WinPlay, MacPlay, LinuxPlay, PlayThing, AllPlay | GPL-3.0 (AndroidPlay: GPLv3/AGPLv3 mix) | COPYLEFT as test tools; not for the ROM image | Receivers; use as test benches outside the image |
| react-carplay, node-CarPlay, cphacks/node-CarPlay | MIT | OK | Background on dongle protocol; not a sender |
| pycarplay, carplay-receiver, WirelessCarPlay | MIT | OK | Old, receiver or relay roles |
| carplay-client (rayphee) | none | NO | Unlicensed |
| FastCarPlay | GPL-3.0 | COPYLEFT / REF | Receiver for Pi |
| scrcpy | Apache-2.0 | OK | Best licence-fit building block for virtual display and input injection |
| OpenAuto (opencardev), openDsh/dash | GPL-3.0 | COPYLEFT / REF | `f1xpl/openauto` and `openDsh/openauto` show no licence in the API |
| aasdk (opencardev) | none in API | REF | Upstream licence not confirmed |
| AACS, OpenAA | GPL-3.0 | COPYLEFT / REF | Android Auto, not CarPlay |
| WirelessAndroidAutoDongle | MIT | OK | Android Auto relay |
| CarProjection (aa112901) | none | NO | Baidu CarLife |

## 3. Flags for the orchestrator

1. **No-licence repos that hold OkcarOS material**: `shockeyzhang/okcar`, the manifest mirrors, `yyjjls/android_device_xiaomi_msm8998-common`. They are unlicensed and the original owner removed the org, so nobody can currently grant a licence. Treat as read-only evidence.
2. **Proprietary, binary-only**: the 4 Okcar APKs and the ROM images. Also the Pi2Carplay ARM32 SDK ("supplied binary", origin unknown).
3. **Non-commercial or ShareAlike traps**: Pi2Carplay's Android base (CC BY-NC-SA 4.0) and the OKCarOS_API_Server clone (CC BY-NC 4.0).
4. **GPL-3.0 vs GPL-2.0-only**: OrangeCar, DiPlay and the other GPL-3.0 projects cannot be copied into the kernel tree. Userspace use is a separate-binary decision.
5. **Dependency licence gap**: carjack (permissive) depends on catplay (unlicensed). OkCarLink (MIT) depends on a binary dependency bundle. Verify before adoption.
6. **Provenance doubt**: OkCarLink uses OkcarOS internal names; CarDroid contains Okcar and MFi-named classes. Confirm no vendor-confidential code or keys before anything is used.
7. **Identity and trademark**: Apple VID 0x05AC, "Apple Inc.", "iPhone" strings in USB descriptors, and use of MFi concepts. Not a copyright issue but a legal-risk one. Agents A/B/C should avoid hard-coding Apple serials or keys, and AGENTS.md already bans MFi bypass and extracted keys.
8. **Public-key files**: the OkcarOS tree contains `okcardev.x509.pem` and `okcarprod.x509.pem` public certificates. They are used only to trust Okcar OTA and recovery packages and must not be reused as Duende trust anchors.

## 4. Recommended licence posture for Duende

- Kernel and USB gadget functions: GPL-2.0-only, new files with SPDX headers, keep upstream copyright for anything derived from Google's `f_accessory.c`.
- Services: Apache-2.0 or MIT for our own code, so permissive upstreams (scrcpy, carjack if catplay is resolved, OkCarLink after review, CarDeck) can be absorbed.
- GPL-3.0 projects are used as test benches or protocol documentation only, or as separate user-space binaries with their source published.
