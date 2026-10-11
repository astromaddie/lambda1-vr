# Lambda1VR on the Valve Steam Frame

This is a Steam Frame build of Lambda1VR for the Frame's Lepton Android container. It uses ARM64, OpenXR and GLES, with Steam Frame controller bindings and a laser pointer for menus. The game keeps its head-tracked stereo view and adds a flat-screen viewing option.

I've played it on my Frame and found it smooth, sharp and clean. The in-game laser sight and several upstream assets remain known gaps. See the [main README](../README.md) for data and project status.

## Build

You need JDK 17 and NDK 25.1.8937393, same as the Quest build.

```
cd Projects/Android
sh ./gradlew assembleFrame
```

The APK ends up in `Projects/Android/build/outputs/apk/frame/lambda1vr-frame.apk`.
It's signed with the debug key.

The first build downloads the Khronos OpenXR loader (1.1.63) from Maven
Central, checks its SHA-256 and unpacks it into `Projects/Android/build/`. Nothing
gets committed. The Quest builds don't touch it.

### The menu tile

Team Beef's menu background has the Quest's button help baked into one tile, `800_1_a_loading.tga`.
The Frame build swaps in its own, with the Frame's buttons on it. `assembleFrame` makes it by itself: it
runs `tools/steam_frame/make_frame_tile.py`, which reads the release tile, takes the old text off and
draws the new one, and writes the result to `Projects/Android/build/frame-assets`. That folder is only
added to the Frame build, so Quest and Pico builds are the same as before. Nothing it makes is committed.

It needs two things. The release tile has to be in `assets/background` (git ignores that folder, so
copy it from Team Beef's v1.7.4 release APK, the `assets/background` folder in it). And it needs Python 3
with Pillow and numpy:

```
python3 -m venv tools/steam_frame/.venv
tools/steam_frame/.venv/bin/pip install pillow numpy
```

The build uses that venv if it's there, else `python3`. `-PframePython=/path/to/python` or
`FRAME_PYTHON` picks another. If the tile or the modules are missing, the build still works. It says so
and the Frame APK gets the release tile, with the Quest help.

## Install and run

Start "Lepton Development" on the Frame and use adb over the network:

```
adb connect <frame-ip>:5555
adb install -r lambda1vr-frame.apk
adb shell am start -n com.drbeef.lambda1vr.frame/com.drbeef.lambda1vr.GLES3JNIActivity
```

Lepton Development deletes the apps you install when it exits. For something that
sticks around you can add the APK as a devkit title in Steam on the Frame. The
game data lives outside the container either way (next section).

Launched from adb the app doesn't get controllers or focus from SteamVR, so use
that for logs. Start it from the Steam library to actually play.

## Game data

Install Half-Life from Steam on the Frame, and that's it. The Android container shows the headset's
Steam folders (read only), so the game uses the files where Steam put them, there's nothing to copy. At
start the app looks for `steamapps/common/Half-Life/valve/liblist.gam` in the Steam library (and in the
other libraries `libraryfolders.vdf` lists, like one on an SD card) and tells the engine to treat that folder
as read only (`-rodir`). The expansions (Opposing Force, Blue Shift) are in the same folder on Steam.

`~/Documents/Lambda1VR` is the writable side, and it wins when the same file is in both. Inside Lepton it's
`/sdcard/Documents/Lambda1VR`, the headset's own folder, so it survives a Lepton reset. It holds your saves and
settings, and the files this app puts there on first start (the replacement weapon models, the sprites, the
loading screens), like it does on the Quest. The engine also makes empty folders there with the names of the
game folders in Steam's. On the Quest it's `/sdcard/xash`, that hasn't changed.

If there's no Half-Life in Steam on the headset, it falls back to the old way: copy the `valve` folder from a
PC's Steam copy to `~/Documents/Lambda1VR/valve`. A copy there is used too when Steam has one as well (it's
found first, so a stale copy would be used over the Steam files). The log says which one it picked, with a
`[data]` line.

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

The map is one table, `VrFrameMap.c`, and `tools/steam_frame/tests` in the dev branch checks it for an
input that does two things in one situation and for actions nothing can reach.

If SteamVR hands the app Index or Touch controllers instead, they bind like they do on Quest (Index has no
Menu for apps, so pause is the left trackpad pressed hard). The Frame layout only kicks in when the runtime
says the Frame profile is active.

## Reference display settings

This is what the build runs at by default, and what I wore when it felt right. I treat it as the
target for anything else I port to the Frame, so I'm writing it down.

- Refresh is 120 Hz. That comes from SteamVR, from the per-app setting for the game in the headset's
  SteamVR settings. The game's own `vr_refresh_rate` (default 90) only picks from the rates SteamVR
  offers, and it offers just the one set for the app, so the app can't push it higher. It only counts when
  the game is started from the Steam library. Started from adb it runs at 72.
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

What I measured with it unattended at 120 Hz and 2160 (headset in standby, so there's no scan-out and the
compositor's cost isn't in it): 120 fps and no missed frames, over six runs on two saves. The frame period is
8.33 ms. The game thread uses about 3.1 ms of that. The Adreno runs at 903 MHz and is about 60% busy at
1728, and 100% busy at 2160. Bigger still held 120 on the busy save (2592 and 3024 per eye, one run each),
but I didn't wear those.

## What's different from the Quest build, and why

- There's a `frame` build type, not a flavour. A flavour would make `assembleDebug`
  and `assembleRelease` build it as well, and the Quest builds should stay as
  they are. minSdk is 30 there because Lepton is Android 11 (the Quest build is 32).
- The manifest overlay (`Projects/Android/src/frame`) drops the headtracking
  requirement, since the container doesn't have that feature and the install
  would fail, and the Quest and Pico metadata.
- Lepton has no OpenXR loader broker, so the APK carries the Khronos loader.
  Java loads it as `openxr_loader`.
- Only OpenGL ES is required from the runtime. Everything else is enabled if
  the runtime lists it. There's no Pico or Meta code on that build.
- The eye image is the runtime's recommended size (1728 square) times `vr_resolution_scale` (archived, default 1.25,
  which is the panel's own 2160 square), held to what the view and the swapchain allow. The Quest build uses 1.3x of the
  recommended size. The log has the recommended, the largest allowed and the size used.
- `vr_refresh_rate` (archived, default 90) is the rate to ask for: the highest the runtime offers that isn't
  over it, with the offered list, the request and the result in the log. SteamVR only offers an app the rate in
  its per-app settings in the headset, so that's often the only one on the list. `vr_refresh`, which the game
  uses to scale walking, follows the real frame period.
- When the headset is in standby (`shouldRender` is false) the frame goes in with
  no layers and the game doesn't simulate. Without that the server kept running.
- If SteamVR quits the app the game shuts down the normal way.
- The eye framebuffers use plain GLES 3 on the Frame, so the runtime's GL doesn't
  need `EXT_multisampled_render_to_texture`. It's one swapchain per eye, there's
  no multiview.
- The HUD, game text and titles (the tram credits) stay level with the world when you tilt your head. They
  were drawn into the eye image, so they rolled with your head. The HUD pass is turned the other way by the
  roll the world is drawn with, about the middle of each eye's view, so it follows your head's turn and
  nod but not its roll. The menus (a panel fixed in the world) and a scope are as they were.
- Each eye is drawn from the eye pose the runtime gives (position, and rotation if the eye
  has one, in head space), and each projection view goes in with that eye's own pose. The
  Quest build moves each eye by a fixed 65 mm along the side and submits both views with
  the head's pose. On my Frame the runtime says the eyes are at +-34.5 mm (68.9 mm apart,
  that's the headset's IPD setting), with no rotation, so for the world the difference is small. The
  fixed pose in the layer was the wrong thing to tell the compositor though.
- The HUD sits `vr_hud_distance` metres in front of you (archived cvar, default 1.0). The
  client DLL (a submodule I can't touch) shifts every HUD item by width / 36 per eye, which
  came to about 0.4 m on the Frame and was hard to look at. While the HUD draws, the DLL is told
  the eyes are the mono ones (it adds nothing then), and the HUD is shifted instead by the
  pixels that put it at that distance, from that eye's real position and focal length. A
  scope and the flat menu screen are left as they were.
- The eye framebuffers are made through gl4es but their attachments are set with the
  driver's own GL (`eglGetProcAddress`). The plain `gl*` names in `libxash.so` are gl4es's,
  and it swaps a texture it doesn't know for an empty one of its own.
- The engine's messages go to logcat under the tag `Xash`. They always did, but the developer
  level defaulted to 0, so nothing came out. It's 3 on the Frame build; `-dev` in
  `commandline.txt` still wins.
- `vr_refresh` is taken from the frame period, not from the refresh rates the runtime lists.
  SteamVR sets the rate per app, and it offered 90 Hz while the display ran at 72.
- The log lines that start with `[openxr]` say what was picked: runtime, enabled
  extensions, swapchain format and size, session state, which profiles got
  bindings and what the runtime answered.
- The all-files permission screen doesn't exist on the Frame. There the app
  checks that it can write to the data folder and only asks if it can't.

## Files I changed that the Quest build uses too

- `Projects/Android/build.gradle`: the frame build type, the loader download,
  `buildConfig` on (for `BuildConfig.STEAM_FRAME`)
- `Projects/Android/gradlew` and `gradlew.bat`: the wrapper jar and properties
  were there already, the scripts weren't
- `Projects/AndroidPrebuilt/jni/Android.mk`: the Frame build says which loader
  to link
- `Projects/Android/jni/src/Xash3D/xash3d/engine/Android.mk`: `L1VR_STEAM_FRAME`,
  and `-Wall` for the VR sources
- `java/com/drbeef/lambda1vr/GLES3JNIActivity.java`: loader, data folder,
  permission check
- `TBXR_Common.c/.h`, `OpenXrInput.c`, `L1VR_SurfaceView.c`, `VrEyeMath.c/.h` (new),
  `VrCvars.h`, `common/port.h`, and in the engine `gl_rmain.c`, `gl_draw.c`, `cl_game.c`,
  `cl_scrn.c`: the Frame code, all behind `L1VR_STEAM_FRAME`
- `#include <string.h>` in `TBXR_Common.c` and `OpenXrInput.c`, which `-Wall` asked for

## The Quest build

I built `assembleRelease` before and after these changes. The repo doesn't ship
the Meta loader, so both builds used the Khronos one as a stand-in to link.

This was done before the shutdown fix below, and the comparison wasn't redone after it.

- Everything except `libxash.so` is byte for byte the same.
- The `string.h` includes change how a few functions in `libxash.so` compile
  (strcpy becomes the checked version, memset gets inlined differently).
  Same behaviour.
- With those two includes taken out of the Quest build too, the code,
  read-only data, data, GOT, dynamic symbols and relocations of `libxash.so` are identical
  to the old build. Only the debug info and the build id differ, because line
  numbers moved.
- The Quest APK gets a `BuildConfig` class in the dex, since Java checks it now.
- One real change for Quest too: `jVM` in `L1VR_SurfaceView.c` was never set, so
  `jni_shutdown` dereferenced NULL. The compiler drops everything after a call like
  that, which made the quit path in `AppThreadFunction` run off the end of the function
  (an abort on the Frame). `JNI_OnLoad` sets it now. I haven't built or run it on a Quest.

## Checked and not checked

Checked on the Frame, unattended (headset on a desk, nobody wearing it, started over adb):

- the Khronos loader finds SteamVR 2.17.10, 1728x1728 swapchains, the Frame, Index and Touch
  binding suggestions all come back `XR_SUCCESS`
- the session goes through READY and SYNCHRONIZED, the frame loop holds 72 fps
- the first level (`c1a0`) loads from the Steam Half-Life files and both eyes show it, with
  a sensible difference between the eyes, turned 90 degrees too
- the app quits cleanly when told to
- the stereo, measured on the eye images with rays traced into the map: surfaces at 2.4 to
  10 m land within 0.6 px of the interpupillary distance times the focal length over
  the depth, a wall 58 m away shows 0.1 px where 0.7 is right, and the HUD at
  `vr_hud_distance` 1 and 2 measured 41.0 and 21.0 px of disparity (41.4 and 20.7 expected)

Checked on the Mac:

- `assembleFrame` builds
- the APK has the pinned Khronos loader and the game libraries, arm64 only
- `aapt2 dump badging` says minSdk 30, no headtracking feature, no Quest or
  Pico metadata, package `com.drbeef.lambda1vr.frame`
- the Quest release build is as described above

Not checked yet:

- anything with a person in the headset: how it looks and feels, scale, the HUD,
  controllers, performance, quitting from the SteamVR menu
- starting it from the Steam library (so far only over adb)
- the replacement weapon and hand models: the repo's `assets` don't have the `.mdl`
  files (they're git-ignored), so the engine logs that it can't load `v_hand.mdl` and
  friends
- that the Frame controller paths are accepted by the runtime (the log lines say)
- the recentre buttons
- Pico: nothing there changed as far as I can tell, but I can't build or run it
