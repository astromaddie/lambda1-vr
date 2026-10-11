# Lambda1VR on the Valve Steam Frame

This is a Steam Frame build of Lambda1VR for the Frame's Lepton Android container. It uses ARM64, OpenXR and GLES, with Steam Frame controller bindings and a laser pointer for menus. The game keeps its head-tracked stereo view and adds a flat-screen viewing option.

I've played it on my Frame and found it smooth, sharp and clean. The in-game laser sight and several upstream assets remain known gaps. See the [main README](../README.md) for the game data requirements and port features.

## Build the APK

Build on x86-64 Linux or macOS, or on Windows through WSL2 with an x86-64 Ubuntu distribution. These steps use Bash.
On Apple Silicon, install Rosetta for the pinned NDK's x86-64 host tools. Steam Frame is ARM64 and is not a host for
this Android toolchain. Android's [NDK downloads](https://developer.android.com/ndk/downloads) list the available host
packages.

### Windows with WSL2

Install [WSL 2 and Ubuntu](https://learn.microsoft.com/en-us/windows/wsl/install). From PowerShell run as
Administrator, install Ubuntu with:

```powershell
wsl --install -d Ubuntu
```

Restart Windows if prompted, open Ubuntu, and create its Linux user. Do the build in Ubuntu, with the checkout and
Android SDK under Linux `~` (for example `~/src`), not under `/mnt/c`. Install the Linux JDK and tools inside Ubuntu;
a Windows JDK or Android SDK is not used by the WSL build:

```bash
sudo apt update
sudo apt install -y openjdk-17-jdk git curl unzip zip perl python3-venv openssh-client
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export ANDROID_HOME="$HOME/Android/Sdk"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
mkdir -p "$ANDROID_HOME/cmdline-tools/latest" /tmp/android-cmdline-tools
curl -fL -o /tmp/commandlinetools-linux.zip \
  https://dl.google.com/android/repository/commandlinetools-linux-15859902_latest.zip
unzip -q /tmp/commandlinetools-linux.zip -d /tmp/android-cmdline-tools
mv /tmp/android-cmdline-tools/cmdline-tools/* "$ANDROID_HOME/cmdline-tools/latest/"
```

The Linux command-line tools archive and `sdkmanager` are documented on Google's [Android Studio downloads](https://developer.android.com/studio)
and [`sdkmanager` pages](https://developer.android.com/tools/sdkmanager). Keep this Ubuntu terminal open so the
environment variables remain set.

Clone the public Frame branch and its submodules into the Linux home directory:

```bash
mkdir -p ~/src && cd ~/src
git clone --recurse-submodules --branch frame-dev https://github.com/astromaddie/lambda1-vr.git
cd lambda1-vr
```

Install JDK 17, Android SDK command-line tools and Git. This branch uses Gradle 8.2.1, Android Gradle Plugin 8.2.1,
Android SDK platform 32, build-tools 29.0.3 and NDK 25.1.8937393. On Linux/macOS only, set these variables to the
JDK 17 and Android SDK locations for that host (skip this block if you followed the Windows/WSL setup above):

```bash
export JAVA_HOME="/path/to/jdk-17"
export ANDROID_HOME="/path/to/android-sdk"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"
```

Accept the Android SDK licenses, then install the pinned packages in the same terminal:

```bash
sdkmanager --sdk_root="$ANDROID_HOME" --licenses
sdkmanager --sdk_root="$ANDROID_HOME" \
  "platform-tools" "platforms;android-32" "build-tools;29.0.3" "ndk;25.1.8937393"
```

For Linux and macOS, install the command-line tools for that host from Google's [Android Studio downloads](https://developer.android.com/studio)
page and put their `cmdline-tools` contents under `<android-sdk>/cmdline-tools/latest` before running the setup above.

On Windows through WSL2, clone from Ubuntu into `~/src` and run the build there. A Windows file such as
`C:\Users\WindowsUser\Documents\valve` appears to WSL as `/mnt/c/Users/WindowsUser/Documents/valve`; replace
`WindowsUser` with the Windows account name. The Frame game reads Half-Life files from `~/Documents/Lambda1VR/valve`
on the headset.

The first build needs the release assets that are not stored in Git. From the repository root, make sure `curl`,
`unzip` and `shasum` are available, then run the checked asset-fetch script and build the Frame variant:

```bash
bash tools/steam_frame/fetch_release_assets.sh
cd Projects/Android
sh ./gradlew assembleFrame
```

The script downloads Team Beef's public v1.7.4 release APK, verifies its SHA-256, and restores missing ignored assets.
The build also downloads and verifies the Khronos OpenXR loader. Both downloads need an internet connection. The APK
is `Projects/Android/build/outputs/apk/frame/lambda1vr-frame.apk` and uses the debug key.

Running the checked asset-fetch script before Gradle is required for a source build. The restored files are bundled
into the APK; on first launch, the app copies `extras.pak` to its private files directory and copies the bundled
replacement models, sounds and menu images into the Frame's `~/Documents/Lambda1VR` data folder. You do not need to
copy these release assets to the Frame separately.

The Frame menu tile is generated when the release background image and Python 3 with Pillow and NumPy are available.
To install those optional modules, run `python3 -m venv tools/steam_frame/.venv` and
`tools/steam_frame/.venv/bin/pip install pillow numpy` from the repository root. Without the image or modules, the
build still completes with the release tile and its Quest button hints.

## Install and run

If Steam's Devkit Client is running on Windows, copy the built APK from WSL to a Windows folder so the client can
browse it. From the repository root in Ubuntu:

```bash
windows_user="$(cmd.exe /C echo %USERNAME% | tr -d '\r')"
mkdir -p "/mnt/c/Users/$windows_user/Downloads/Lambda1VR"
cp Projects/Android/build/outputs/apk/frame/lambda1vr-frame.apk \
  "/mnt/c/Users/$windows_user/Downloads/Lambda1VR/"
```

Use Valve's [SteamOS Devkit Client](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames) from Steam
on your desktop. Pair it once with the Frame: enable Developer Mode, choose **Settings > Developer > Pair new host**,
then register the Frame in the client's **Devkits** tab and confirm pairing on the headset.

For a first install, open **Devkits > Connect to devkit by IP**, choose **Title Upload**, and set:

- **Name:** `Lambda1VR`
- **Local Folder:** The folder containing the APK: the Windows Downloads folder copied above on WSL, or
  `Projects/Android/build/outputs/apk/frame` on Linux/macOS.
- **Start Command:** `lambda1vr-frame.apk`
- **Runtime:** `Android`

Upload and start the title. It then appears in the Frame's Steam library under **Library > Non-Steam > Devkit Game**.
For later builds, upload again with the same title name to update that library entry. Steam launches provide the
Frame controller input.

## Game data

Use the full retail Half-Life game data from your own copy; **Half-Life: Source is not compatible**. The app can read
Half-Life installed through Steam on the Frame, or a `valve/` folder under `~/Documents/Lambda1VR/valve` on the Frame.
Saves, settings and the files restored from the v1.7.4 release assets live under `~/Documents/Lambda1VR`.

If you do not have Half-Life installed through Steam on the Frame, copy the retail `valve/` folder from your own
Half-Life installation. Enable Developer Mode and set the SteamOS password using Valve's [Steam Frame setup guide](https://partner.steamgames.com/doc/steamhardware/steamframe/setup).
Valve's [debugging guide](https://partner.steamgames.com/doc/steamhardware/steamframe/debugging) documents SSH as
`steamos@frame`. Set `FRAME_HOST` to `frame` or the Frame's IP address; enter the password you set when SSH asks.
These commands use the host's OpenSSH `ssh` and `scp` clients; install `openssh-client` on Ubuntu (included in the
WSL setup above).
For WSL, a `valve/` folder under Windows Documents appears at `/mnt/c/Users/WindowsUser/Documents/valve`; replace
`WindowsUser` with your Windows account name. From the build computer, run:

```bash
export FRAME_HOST=frame  # replace with the Frame's IP if its hostname does not resolve
VALVE_DIR="/mnt/c/Users/WindowsUser/Documents/valve"  # use the local folder path on Linux/macOS
ssh "steamos@$FRAME_HOST" 'mkdir -p /home/steamos/Documents/Lambda1VR/valve'
scp -r "$VALVE_DIR"/. "steamos@$FRAME_HOST:/home/steamos/Documents/Lambda1VR/valve/"
```

If Half-Life is already installed through Steam on the Frame, no game-data copy is needed.

## Quest and Frame LAN multiplayer

Use compatible Lambda1VR builds from the same source revision, with matching Half-Life game data, and put
both headsets on the same local network. The LAN browser uses local UDP discovery and direct connections;
it does not need an Internet server, NAT bypass, or an IP address typed on-head. A guest Wi-Fi network with
client isolation can block discovery and direct play.

On the host, open **Multiplayer > LAN game > Create game**, choose a map, then select **Ok**. The player
count starts at 2 so the listen server has a slot for its local player and one joining player. The headset
that starts the map is player 1. On the other headset, open **Multiplayer > LAN game**, select the host in
the server list, and choose **Join game**. Use **Refresh** if the host does not appear.

Keep a Frame host active during play. In the Frame build, headset standby (`shouldRender=false`) currently
skips the engine frame, including the LAN server tick. A joining client can remain connected if the host
resumes within its 60-second `cl_timeout`; after that the client reports a timeout and must rejoin from the
LAN list. The server frees a silent client after its 125-second `sv_timeout` when ticking resumes. The Quest
build continues its engine frame while not rendering, so its current standby behavior differs.

## Controls

The Frame's controllers are a split gamepad. Left has the stick, trigger, grip, bumper, D-pad and
View. Right has the stick, trigger, grip, bumper, A B X Y and Menu.

Half-Life's 25th anniversary update ships its own Steam Input layout for a gamepad
(`controller_configs/xbox_controller_config_standard.vdf` in the game's `valve` folder). I used that
one, so it works like the gamepad version people already know, and added the VR parts: your head looks,
and the grips are your hands.

| Frame | Playing |
| --- | --- |
| Left stick | Move |
| Left stick click | Crouch (toggle). Physically ducking still works |
| Right stick left / right | Turn (Lambda1VR's snap or smooth turn) |
| Right stick click | Flashlight |
| Right stick up / down | Nothing in the game |
| Weapon hand trigger | Fire |
| Other hand trigger | Alt fire |
| A | Jump |
| B | Crouch (while held) |
| X | Use |
| Y | Reload |
| Left / right bumper | Previous / next weapon (fire to pick it, like in Half-Life) |
| D-pad left / right | Previous / next weapon, same as the bumpers |
| D-pad up | Last weapon |
| D-pad down | Laser sight, and steadies a scope |
| Menu | Pause menu. Held for half a second: quick save |
| View | Flashlight (the scoreboard in multiplayer) |
| View, held 1 s | Recentre, with a buzz |
| View, held 3 s | Recentre and use the head height as standing height |
| Weapon hand grip | Use from that hand: grab, push or pull things, press buttons. Behind your head it pulls the crowbar from the backpack |
| Other hand grip | Near the weapon: the two-handed hold (steadier aim, a scope). Well away from it: use from that hand |

The weapon hand is the right one, or the left one with `vr_control_scheme 10`. Left-handed swaps the
roles that belong to a hand (the triggers, the grips, the laser, where the gun is) and leaves the buttons
where they are.

The grips use what Lambda1VR already had: the game looks for things you can use near the hand and in
front of it, from where the controller is, not from your head. The other grip decides what it is when you
press it, by how far the hands are from each other (under 35 cm it's the gun hold, over 55 cm it's use, in
between it stays what it was last time), and keeps that until you let go. The log says which it picked.
`vr_gesture_triggered_use` has to stay on for this, it's what gives the short reach and use from the other
hand. Waving a hand about doesn't use things on the Frame, double clicking jump doesn't crouch.

Kept as it was: aiming from the controller, swinging the crowbar, the flashlight beam from your off hand
(`vr_headtorch` and `vr_reversetorch` still work), the two-handed scope, reaching behind your head for the
crowbar, ducking by ducking, walking around the room, haptics, the highlight on things you can use. With
your other hand behind your head, the right stick click is quick save and View is quick load, like the
two buttons on a Quest controller. Walk (the slow key) is gone, the stick is analog.

The right stick is only for turning. Pushing it up or down does nothing in the game, so you can't hit the
laser sight or anything else by accident while you turn. The crowbar is the weapon hand's grip behind your
head, there's no button for it.

There's no button for the flat screen view either. It's a setting now: Pause, Configuration, Video, Video
options, "Flat Screen View" (the `vr_flat_screen` cvar, off by default, and only in the Frame build). It
puts the game on a flat screen in front of you instead of around you. The controllers only point and click
in that mode, like in a menu, so it's for watching and not for playing. Menu still opens the pause menu, so
you can turn it off again from there.

| Frame | In a menu |
| --- | --- |
| Weapon hand laser and trigger | Point and click |
| A | Confirm |
| B, Menu, View | Back |
| D-pad, left stick | Move |
| Left / right bumper | Previous / next tab or page |
| Right stick up / down | Scroll |


If SteamVR hands the app Index or Touch controllers instead, they bind like they do on Quest (Index has no
Menu for apps, so pause is the left trackpad pressed hard). The Frame layout only kicks in when the runtime
says the Frame profile is active.

## Display settings and performance

These are the build defaults and the display settings I prefer on the Frame.

- Refresh is 120 Hz. That comes from SteamVR, from the per-app setting for the game in the headset's
  SteamVR settings. The game's own `vr_refresh_rate` (default 90) only picks from the rates SteamVR
  offers, and it offers just the one set for the app, so the app can't push it higher. It only counts when
  the game is started from the Steam library.
- Each eye is 2160 x 2160. That's the 1728 SteamVR recommends times `vr_resolution_scale` (1.25), which is
  the panel's own size. There's no extra supersampling on top of that.
- The eye images are sRGB (`GL_SRGB8_ALPHA8`), one swapchain per eye, three images each. No multiview.
- No MSAA. It's one sample, plain GLES 3 framebuffers. The engine's `gl_msaa` is 0 too.
- Depth is a 24 bit renderbuffer, no stencil, and it isn't handed to SteamVR as a depth layer.
- No foveation.
- The game's GL comes from gl4es, using its GLES 2.0 backend on top of a GLES 3 context.
- Textures are trilinear (`gl_texturemode GL_LINEAR_MIPMAP_LINEAR`) with 8x anisotropic filtering
  (`gl_anisotropy 8`), no LOD bias, no picmip.
- The shipped `config.cfg` has `gamma 2.0` and `brightness 0.8`. My own copy on the headset had `gamma 2.5`,
  so that's what I was looking at.

In six recorded runs on two saves, with the Frame display in standby, the game held 120 fps at 120 Hz and
2160x2160 per eye with no missed frames. The game thread used about 3.1 ms of the 8.33 ms frame period. The
Adreno ran at 903 MHz and was about 60% busy at 1728 pixels per eye and fully busy at 2160. One run each on a
busy save also recorded 120 fps at 2592 and 3024 pixels per eye.
