# PR0 readiness: Lambda1VR Steam Frame

**Disposition: HOLD · EXPERIMENTAL · PRIVATE.** The target is the private astromaddie/lambda1-vr repository. Do not confuse it with origin (astromaddie/Lambda1VR), an existing public repository, or Team Beef upstream. The 2026-10-11 audit records unresolved signing/machine-specific artifacts, assets/extras.pak provenance, and GPLv3 / Half-Life SDK terms compatibility; it does not provide a grant: [PR0 audit](https://github.com/astromaddie/steam-frame-vr-playbook/blob/main/reports/pr0-2026-10-11/README.md#material-findings).

## Source identity and checkout evidence

- Repository: lambda1-vr; branch frame-dev; source HEAD at review: 4fb7428b9e0485a3eee88334008f900b0eb49011.
- Remotes: upstream is Team-Beef-Studios/Lambda1VR; private is the intended target astromaddie/lambda1-vr; origin is the distinct astromaddie/Lambda1VR repository and is not the target.
- Initial worktree had untracked .DS_Store, Projects/.DS_Store and Projects/Android/.DS_Store; all were preserved. No fresh clone was made.
- The previous root README is archived byte-for-byte at docs/README_PRE_PR0.md.

## Platform and build route

Target is Steam Frame / Lepton Android 11, ARM64, SteamVR OpenXR and GLES 3.2; APK id com.drbeef.lambda1vr.frame. Build with JDK 17 and NDK 25.1.8937393 using cd Projects/Android && sh ./gradlew assembleFrame; expected output is Projects/Android/build/outputs/apk/frame/lambda1vr-frame.apk. The first build fetches and verifies Khronos OpenXR loader 1.1.63. The optional Frame menu tile needs assets/background/800_1_a_loading.tga from Team Beef's v1.7.4 release APK and Python Pillow/NumPy; the build falls back to the release tile if these are absent. No build, host test, package or device action was run for this documentation pass.

## Historical evidence and limits

The architecture record reports earlier unattended SteamVR sessions: main menu at 72 fps, the first level c1a0 loading in both eyes, Steam Half-Life files read in place, and an unattended 2026-10-05 Steam-title run at 120 Hz / 2160 per eye. The 2026-10-05 wear was on an earlier build and the user reported a smooth 120 fps and a sharp image. There was no worn frame-time capture. The host binding test and synthetic button logs do not prove physical controller input. In-game laser sight was not visible in four unattended dumps; barney_vest, barney_helmet and sprites/scope remain missing. SteamVR-menu exit and full wearer workflows remain unverified. No historical result is presented as a test of this HEAD.

## Game data and rights

Use a user-owned Steam Half-Life install on the Frame (steamapps/common/Half-Life/valve/liblist.gam) or place a retail valve/ directory at /sdcard/Documents/Lambda1VR/valve; Half-Life: Source is incompatible. The full retail installation must be supplied separately; assets/extras.pak remains under provenance review. The audit calls for cleanup/provenance review of tracked or historical Android debug signing material, local.properties and assets/extras.pak. Root/Xash GPLv3 and Half-Life SDK terms require a documented compatibility/separation or permission route before publication; upstream notices and game ownership do not resolve that.

## Pending acceptance

From a clean, identified source snapshot, verify install as a persistent Steam devkit title; physical Frame button and pose input; both-eye stereo and head tracking; pointer-driven menus; actual first-level gameplay, weapon and flashlight actions; pause/resume; save/load across relaunch; SteamVR-menu exit and clean shutdown. Repeat the 120 Hz / 2160 run while worn and capture frame timing before making a performance claim. No current-HEAD build, headset or worn timing result is claimed.
