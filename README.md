# Lambda1VR on Steam Frame (experimental)

This branch adds a Steam Frame build to [Team Beef's Lambda1VR](https://github.com/Team-Beef-Studios/Lambda1VR), the Xash3D-FWGS/GoldSrc-based Half-Life VR port. It targets Steam Frame's Lepton Android container with ARM64, OpenXR and GLES. The Frame input map, runtime-located eye poses and build-specific Android overlay are documented in [docs/VR_ARCHITECTURE.md](docs/VR_ARCHITECTURE.md); see [docs/STEAM_FRAME_VR.md](docs/STEAM_FRAME_VR.md) for build and setup.

**Status: PRIVATE · EXPERIMENTAL · HOLD.** The private publication target is astromaddie/lambda1-vr; it is distinct from the pre-existing public astromaddie/Lambda1VR remote and from upstream. The audit identifies tracked/history signing and machine-specific files, a third-party asset provenance question, and unresolved GPLv3 / Half-Life SDK terms compatibility. No blanket license or game-data grant is asserted. See [docs/PR0_READINESS.md](docs/PR0_READINESS.md) and the [Steam Frame PR0 audit](https://github.com/astromaddie/steam-frame-vr-playbook/blob/main/reports/pr0-2026-10-11/README.md#material-findings). The prior root README is preserved byte-for-byte at [docs/README_PRE_PR0.md](docs/README_PRE_PR0.md).

## Capabilities and evidence

| Capability | Evidence recorded in the project | Limit for this source revision |
| --- | --- | --- |
| OpenXR startup and stereo | Earlier unattended SteamVR runs reached the session, loaded c1a0, rendered both eyes and exited; stereo geometry was checked from eye dumps. | These are historical device results, not a rebuild or acceptance of current HEAD. |
| Steam Frame controls | Binding tables and synthetic input have host checks; earlier diag_press runs logged mapped actions. | Physical controller input and full menu/gameplay control were not worn or accepted. |
| Worn gameplay | On 2026-10-05 an earlier Steam title build was reported smooth at 120 fps and “very sharp and very clean.” | No wearer timing capture was made; this report is not current-build performance proof. |
| Known gaps | Unattended dumps did not show the in-game laser sight; barney_vest, barney_helmet and sprites/scope are documented missing assets. | SteamVR-menu exit, physical controls, pause/save workflows and complete gameplay remain unverified by a wearer. |

## Half-Life data

The full retail Half-Life installation is an external requirement. The provenance of bundled assets/extras.pak remains unresolved. Install Half-Life from Steam on the Frame; the app looks for steamapps/common/Half-Life/valve/liblist.gam and reads the Steam valve directory in place. The fallback is a user-supplied valve/ directory under /sdcard/Documents/Lambda1VR/valve (SteamOS ~/Documents/Lambda1VR/valve). Half-Life: Source is not compatible. The same Documents directory holds writable saves and settings. Team Beef release assets for hands, replacement weapons and the optional Frame menu tile are separate from commercial game data; consult the build notes before preparing them.

## Build route

Use JDK 17 and NDK 25.1.8937393. From the Android project directory:

    cd Projects/Android
    sh ./gradlew assembleFrame

The documented APK is Projects/Android/build/outputs/apk/frame/lambda1vr-frame.apk. The first build downloads and verifies the Khronos OpenXR loader. The optional Frame menu tile needs assets/background/800_1_a_loading.tga from Team Beef's v1.7.4 release APK and Python Pillow/NumPy; if absent, the build uses the original tile. These are documented build instructions, not a claim that this source commit was rebuilt, tested or installed here.
