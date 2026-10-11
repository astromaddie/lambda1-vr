# Lambda1VR on Steam Frame

This is my Steam Frame build of [Team Beef's Lambda1VR](https://github.com/Team-Beef-Studios/Lambda1VR), the Xash3D-FWGS and GoldSrc-based Half-Life VR port. It runs in the Frame's Lepton Android container with ARM64, OpenXR and GLES. See the [architecture notes](docs/VR_ARCHITECTURE.md) for the input and rendering details.

**EXPERIMENTAL.** I made this for personal use. This is a source release of my Frame changes to Team Beef's existing port, with the original GPLv3 and component notices kept. See the [build and publication notes](docs/PR0_READINESS.md). The [original README](docs/README_PRE_PR0.md) is preserved.

The Frame build keeps tracked head and hands, stereo rendering, Steam Frame controller bindings, a laser pointer for menus, snap or smooth turning, and a flat-screen viewing option. I've played it on my Frame and found it smooth, sharp and clean. The in-game laser sight and several upstream assets, including Barney's vest and helmet and the scope sprite, are known gaps.

## Half-Life data

You need the full retail Half-Life game data from your own copy. The app can read Half-Life installed through Steam on the Frame, or a `valve/` folder under `/sdcard/Documents/Lambda1VR/valve` (`~/Documents/Lambda1VR/valve` on SteamOS). Half-Life: Source is not compatible. Saves and settings live in the same Documents folder. The bundled `extras.pak` comes unchanged from Team Beef's public source; it doesn't replace the required retail game files.

## Build and install

Use JDK 17 and NDK 25.1.8937393. Set `ANDROID_HOME` to your Android SDK directory; machine-specific SDK paths are kept out of Git. From the repository root:

```sh
cd Projects/Android
sh ./gradlew assembleFrame
```

The APK is `Projects/Android/build/outputs/apk/frame/lambda1vr-frame.apk`. The first build downloads and verifies the Khronos OpenXR loader. Launch the registered `Lambda1VR` title from Steam. The [Steam Frame setup guide](docs/STEAM_FRAME_VR.md) covers controls, game data and build assets; the [architecture notes](docs/VR_ARCHITECTURE.md) describe the implementation and open issues; [docs/PR0_READINESS.md](docs/PR0_READINESS.md) records the checks.
