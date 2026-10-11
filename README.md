# Lambda1VR on Steam Frame

This is my Steam Frame build of [Team Beef's Lambda1VR](https://github.com/Team-Beef-Studios/Lambda1VR), the Xash3D-FWGS and GoldSrc-based Half-Life VR port. It runs in the Frame's Lepton Android container with ARM64, OpenXR and GLES. See the [architecture notes](docs/VR_ARCHITECTURE.md) for the input and rendering details.

**EXPERIMENTAL.** I made this for personal use. This is a source release of my Frame changes to Team Beef's existing port, with the original GPLv3 and component notices kept. See the [build and publication notes](docs/PR0_READINESS.md). The [original README](docs/README_PRE_PR0.md) is preserved.

The Frame build keeps tracked head and hands, stereo rendering, Steam Frame controller bindings, a laser pointer for menus, snap or smooth turning, and a flat-screen viewing option. I've played it on my Frame and found it smooth, sharp and clean. The in-game laser sight and several upstream assets, including Barney's vest and helmet and the scope sprite, are known gaps.

## Half-Life data

You need the full retail Half-Life game data from your own copy. The app can read Half-Life installed through Steam on the Frame, or a `valve/` folder under `/sdcard/Documents/Lambda1VR/valve` (`~/Documents/Lambda1VR/valve` on SteamOS). Half-Life: Source is not compatible. Saves and settings live in the same Documents folder. The bundled `extras.pak` comes unchanged from Team Beef's public source; it doesn't replace the required retail game files.

## Build and install

Build on x86-64 Linux or macOS, or on Windows through WSL2 with an x86-64 Ubuntu distribution. These instructions
use Bash. On Apple Silicon, install Rosetta for the pinned NDK's x86-64 host tools; Steam Frame is ARM64 and is not
a host for this Android toolchain.

Clone the public Frame branch and its submodules:

```sh
mkdir -p ~/src
cd ~/src
git clone --recurse-submodules --branch frame-dev https://github.com/astromaddie/lambda1-vr.git
cd lambda1-vr
```

Use JDK 17 and the Android SDK packages listed in the [Steam Frame guide](docs/STEAM_FRAME_VR.md). Before the first
build, restore the release assets the repository does not track, then build the Frame variant:

```sh
bash tools/steam_frame/fetch_release_assets.sh
cd Projects/Android
sh ./gradlew assembleFrame
```

The APK is `Projects/Android/build/outputs/apk/frame/lambda1vr-frame.apk`. Building creates the APK; it does not
install or launch it. The guide covers the first upload, later updates, and your Half-Life data.
