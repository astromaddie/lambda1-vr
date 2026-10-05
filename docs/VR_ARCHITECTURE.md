# Lambda1VR on the Steam Frame: how it fits together

Current state of the Frame build (`assembleFrame`, `L1VR_STEAM_FRAME`). The public
description is in [STEAM_FRAME_VR.md](STEAM_FRAME_VR.md); this one is for development.
First unattended device runs 2026-10-04 (headset on a desk, over adb, nobody wearing it); first worn run 2026-10-05 (see section 7: smooth 120 fps, "very sharp and very clean"). Steam-in-place, menu laser, 120 Hz and resolution runs 2026-10-05 (the 120 Hz ones launched as the Steam title through `~/.steam/steam.pipe`, logs from the journal).

## 1. Target and probe facts

| Item | Value |
| --- | --- |
| Device | Valve Steam Frame, Snapdragon 8 Gen 3, Adreno 750 |
| Container | Lepton: Android 11 (API 30), arm64-v8a, GLES 3.2 through Zink on Turnip |
| Runtime | SteamVR OpenXR 2.17.10 at `/vendor/etc/openxr/1/active_runtime.json`; no loader broker, so the Khronos loader 1.1.63 is in the APK |
| Recommended eye size | 1728x1728, asymmetric FOV (left eye about L -1.028, R 0.885, U 0.864, D -1.048 rad) |
| Refresh | the rate set for the app in SteamVR (72 Hz by default; 120 for the Lambda1VR Steam title, `steam.app.4086534518` in steamvr.vrsettings); `XR_FB_display_refresh_rate` offers only 90 and the app can't ask for more. Only a Steam title launch gets the per-app rate |
| Controllers | `/interaction_profiles/valve/frame_controller_valve` (`XR_VALVE_frame_controller_interaction`) |
| GL | `GL_OVR_multiview(2)` are advertised, not used: one swapchain per eye, sRGB (`GL_SRGB8_ALPHA8`), plain GLES 3 framebuffers |
| Game libs | `libxash.so` (engine plus the VR layer), `libgl4es.so` (GL on top of GLES), `libmenu.so`, `libclient*.so`, `libserver*.so` |
| Data | `/sdcard/Documents/Lambda1VR` (the headset's `~/Documents/Lambda1VR`) |
| Package | `com.drbeef.lambda1vr.frame`, minSdk 30 |

Measured 2026-10-04 (unattended): SteamVR/OpenXR 2.17.10 offers 48 instance extensions, not `XR_EXT_performance_settings`;
swapchain formats 0x8c41, 0x8c43 (sRGB8_ALPHA8, used), depth16/24/32F; recommended 1728x1728; the runtime listed 90 Hz but the
frame period is 13.89 ms (72 Hz); Frame, Index and Touch suggestions all `XR_SUCCESS`; gl4es reports "GLES 2.0 backend", Mesa.

Two things that bit: the plain `gl*` names in `libxash.so` resolve to gl4es (it's first in NEEDED), and gl4es doesn't know
framebuffers or textures the driver made. So swapchain attachments are set with `eglGetProcAddress` functions, but the
framebuffers are created and bound through gl4es (it refuses to bind ones it didn't make). A readback from the eye
framebuffer has to use the driver's `glReadPixels` too. `jVM` was never assigned, which made the quit path fall off the end of
`AppThreadFunction` (fixed).

## 2. Frame protocol (as found in `TBXR_Common.c` and `L1VR_SurfaceView.c`)

One thread (`AppThreadFunction`) owns the EGL context, the OpenXR session and the engine.

1. `TBXR_InitialiseOpenXR`: EGL context (pbuffer), `xrInitializeLoaderKHR`, instance (Frame: only the
   extensions the runtime lists), system, GL requirements, view configuration, eye size.
2. `Host_Main` (engine init), `VR_Init` (cvars), `TBXR_EnterVR` (session, head-space), `TBXR_InitRenderer`
   (reference spaces, swapchains), `TBXR_InitActions` (actions, bindings, attach), `TBXR_WaitForSessionActive`.
3. Each XR frame:
   - `TBXR_FrameSetup`: events and session state, `xrWaitFrame`, `xrBeginFrame`, `xrLocateViews`
     (`vrFOV`, the per-eye projections), head pose, controllers (`xrSyncActions`), haptics.
   - Frame build: if `shouldRender` is false the frame goes in with zero layers and nothing else runs.
   - `Host_BeginFrame`: the whole server frame, input, timing. Once per XR frame.
   - for each eye: acquire the swapchain image, set `vr_stereo_side` (0/1, or 2/3 for the mono scope eyes),
     `Host_Frame` (the client frame and the render of that eye), comfort-mask and border clears, release.
   - `Host_EndFrame`, then `xrEndFrame` with a projection layer (both swapchains) or, when
     `VR_UseScreenLayer()`, a 5 x 4.5 m quad layer 4.5 m away that shows eye 0 only.
4. Frame build: each eye is drawn from the located eye pose in head space (`VR_GetEyeTransform`, position scaled by
   `vr_worldscale`, plus the eye's rotation if any) and its projection view is submitted with the head pose placed by
   that eye pose (`VrEye_ComposePose`), both from the same `xrLocateViews` at the predicted time. Mono scope eyes and the
   screen layer draw from the middle and submit the head pose. (Quest build: fixed 65 mm times `vr_worldscale`, head pose
   for both views.)
5. Exit: `runStatus == 0` leaves the loop: `Host_Shutdown`, `TBXR_LeaveVR`, Java `shutdown()` (`System.exit`).
   The Frame build also sets it on `EXITING`, `LOSS_PENDING` and instance loss.

Session state is logged with `[openxr] session state: X`. `FOCUSED` and `VISIBLE` set flags on `ovrApp` that
nothing reads. Input while unfocused is empty (the runtime gives nothing); the game isn't paused on focus loss.

Spaces: `CurrentSpace` is STAGE when the runtime has it (else LOCAL minus 1.675 m). Recentre (Frame build,
View held 1 s) moves our offset yaw and floor position, keeps the game's view yaw (`snapTurn` takes the difference)
and the world position, so nothing jumps.

## 3. Camera-relative and screen-space draw inventory

How the engine draws what isn't plain world geometry, and the rule each one has now. From reading the code,
nothing run. Paths are under `Projects/Android/jni/src` (`E/` = `Xash3D/xash3d/engine`).

Common rules: the eye is `vr_stereo_side`; `R_SetupModelviewMatrix` (`E/client/gl_rmain.c`) shifts by
`vr_worldscale * VR_IPD / 2` (a fixed 0.065 IPD; the Frame build uses the runtime's eye poses instead); the projection is the headset's
asymmetric per-eye FOV (`VR_GetVRProjection`); 2D content gets `R_Get2DOffset` (`E/client/gl_draw.c`), the pixel
shift from the optical axis; `GetStereoDepthOffset` adds +-width/36 per eye for HUD items (two copies, `E/client/cl_scrn.c`
and the client DLL's `hud.cpp`).

| Category | Where | Rule it uses now | Notes |
| --- | --- | --- | --- |
| HUD | `E/client/cl_view.c` `V_PostRender`, `cl_game.c` `CL_DrawHUD` / `CL_RedrawHUD`, client DLL | Quest: 2D ortho with the axis shift, most elements add the DLL's own width/36 per eye (about 0.4 m on the Frame's lenses). Frame build: the DLL is told the eyes are mono while it draws (no shift), and the whole HUD pass (centre print too) is shifted in `R_Get2DOffset` by `VrEye_HudShiftPixels` for `vr_hud_distance` (default 1 m) | hud_scale 1.3; messages, scoreboard and VGUI draw outside that pass and stay at infinity; scope and the screen layer unchanged; the HUD redraws once per eye |
| Sprites | `E/client/gl_sprite.c`, `gl_rpart.c`, `gl_beams.c` | oriented with the shared view vectors, positions from the cyclopean `RI.vieworg`; eye shift is in the modelview | fine in stereo; glow sprites test occlusion from the middle eye |
| Crosshair | `E/client/cl_game.c` `CL_DrawCrosshair` | only in a scope: centre of the 2D space (the optical axis), rotated by hand and head roll, mono | outside a scope the client DLL forces `crosshair 0` |
| Laser sight | server DLL (`weapons.cpp`, `rpg.cpp`) | a beam entity or a glow spot, both world entities | follows the weapon's aim vector, not the controller ray |
| Comfort vignette | `cl_game.c` `CL_DrawVignette`, mask in `L1VR_SurfaceView.c` | textured quad and scissor mask, both centred on the optical axis | three separate level statics each step per eye; the mask isn't gated on the screen layer |
| Scope overlay | `cl_game.c` `CL_DrawScope` | mono eyes (2/3), centred | three different "scope engaged" thresholds (`vrFOV`, 85, 85) |
| Menus and console | `E/client/cl_view.c`, `cl_menu.c`, `mainui/` | 2D mode on eye 0, shown on the screen-layer quad; symmetric projection, no axis shift | the quad is at y = 1.0 in stage space, 4.5 m along `playerYaw` |
| Pointer | `E/client/touch.c`, `VrInputCommon.c` | angle based: `-sin(yaw - playerYaw)`, `pitch / 90` from the controller angles | not a ray against the quad; no ray is drawn |
| Skybox | `E/client/gl_warp.c` `R_DrawSkyBox` | cube of half-size `farClip / 2` around `RI.cullorigin` (middle eye) | effectively at infinity; fine in stereo |
| View weapon, hands, torch | `E/client/gl_studio.c`, `cl_view.c`, client DLL `vr_helper.cpp` | world positions: `vieworg + weapon.org`, controller offset scaled by `vr_worldscale`; depth range squeezed to 0.3 | origin computed on the left eye and reused; hidden in a scope |
| Fog | `E/client/gl_rmain.c`, `gl_rsurf.c`, `cl_game.c` | GL fog in eye space | the sky isn't fogged unless custom fog |
| Mirrors | `E/client/gl_mirror.c` | reflect the middle-eye view, then the normal eye projection | mirror viewport clamped to 96-1024 x 72-768, so the aspect is off; copies the eye framebuffer |
| Culling | `E/client/gl_rmain.c` `R_SetupFrustum`, `gl_cull.c` | symmetric frustum from `vrFOV` (int degrees) about the middle eye | can be narrower than the real per-eye FOV; vertical edge pop-in possible |
| Screen fades, cinematics | `cl_game.c`, `cl_video.c` | fades overdraw the shifted ortho; cinematics go on the screen layer | |
| Shake, strobe | client DLL `view.cpp`, `gl_rstrobe.c` | shake compiled out under `VR`; strobe off | |

Frame split inside the engine: `Host_BeginFrame` runs the server and input once per XR frame; `Host_Frame` runs per eye
and only does the client frame. Per-frame client state runs on the left eye (`cl.time`, `CL_ReadPackets`, `CL_AddEntities`,
`V_SetupRefDef`, `pfnCalcRefdef`) or the right eye (`S_RenderFrame`, `CL_DecayLights`, cinematics, console).

## 4. Controls

Source of the layout: Half-Life's own gamepad layout from the 25th anniversary update (`valve/controller_configs/xbox_controller_config_standard.vdf`),
with the VR parts added (grips, head look). It replaced the first Frame map (the Quest layout moved around) after the second wear, and replaces
`standard.md` sections 1 and 2 where they differ: B is crouch (not back), A jump, X use, Y reload, bumpers and D-pad pick weapons, right stick click is the
flashlight. Menu and View kept their system jobs (Menu pause, View recentre) as in the standard.

Code: `VrFrameMap.c` is the map as one table (`rows[]`: physical input, context, action); `VrInputFrame.c` turns it into the Quest-shaped states that
`VrInputDefault.c` reads (dominant hand: trigger, backpack grip, turn stick, use on the stick-click bit, crouch and jump on its two buttons; other hand: steady on
the grip, move stick, torch and quick load (hand behind the head) on its two buttons, laser on its stick-click bit) and into commands for what the scheme code doesn't have
(alt fire, reload, weapon select, last weapon, scoreboard, quick save, menu keys, recentre). `OpenXrInput.c` reads the physical buttons (new `dpad*` actions) and logs the
profile. It only runs when the runtime reports the Frame profile active (or `diag_press` is in use). Contexts: menu (`cls.key_dest != key_game`), weapon hand
behind the head, other hand behind the head (not in multiplayer), multiplayer, and the other grip's mode (decided at press time).

Scheme code kept as it was: aim from the controller, crowbar swing (weapon velocity), flashlight beam from the off hand, two-handed hold and scope (`vr_weapon_stabilised`,
`+alt1`), backpack grab, physical ducking (`vr_enable_crouching`), room-scale walking, haptics, usable-object highlight. Server side (`hlsdk-xash3d/dlls/player.cpp`,
`PlayerUse`): `+use` finds usable things within `PLAYER_HAND_SEARCH_RADIUS` of the *weapon hand's* position in front of the hand's direction (64 units; 24 and a wide cone
when `vr_gesture_triggered_use` is 1), and `+use2` does the same from the off hand but only when that cvar is 1. So hand-origin use already exists and needs no DLL change; the
grips need the cvar at 1 and the client's automatic gesture use switched off (`VR_FrameHandUse()`, `VrInputDefault.c`). The reach for `+use` is therefore 24 units (0.6 m), which also
applies to X.

Changed defaults on the Frame (saved settings are updated once, `vr_frame_map`): `vr_quick_crouchjump` 0 (double clicking A would be a second crouch input), `vr_gesture_triggered_use` 1 (see above).
Not carried: walk/slow (`+speed`, was the off trigger; the stick is analog), weapon select on the turn stick (it fought the turn).

Not kept from the standard: dominant-hand setting beyond `vr_control_scheme`.

Changed after the second wear (the user's call): the right stick's up and down are unbound in the game (they still scroll in a menu), so turning can't toggle anything. D-pad
left / right are previous / next weapon (aliases of the bumpers), up is the last weapon, down the laser sight (what right stick down did). The D-pad crowbar is gone, the crowbar is
the weapon hand's grip behind the head. The flat screen view has no button any more: it is `vr_flat_screen` (Frame build only), a "Flat Screen View" checkbox on the Video options
page (`mainui/menus/VideoOptions.cpp`, compiled with `-DL1VR_STEAM_FRAME` for the Frame only). `VR_UseScreenLayer()` returns true while it is set, which is the same screen layer the
Quest's toggle shows: the controllers point and click, the scheme code's gameplay half doesn't run, Menu still pauses. Quick load had been on right stick up with the other hand
behind the head; it is View now (right stick click stays quick save), the two buttons a Quest controller has. In multiplayer View toggles the scoreboard only (`+showscores`,
sent from `VrInputFrame.c`), where it used to toggle the flat screen view as well.

The menu tile: `tools/steam_frame/make_frame_tile.py` (Pillow and numpy) reads the release tile `assets/background/800_1_a_loading.tga`, inpaints the Quest help off it and draws the
Frame's, into `Projects/Android/build/frame-assets/background/`. `build.gradle`'s `makeFrameTile` runs it before `mergeFrameAssets` and the `frame` source set adds that folder, so the
Frame APK's tile overrides the release one. A missing tile or module is a warning and the release tile. Not committed, Quest and Pico builds don't see it.

The table, generated by `tools/steam_frame/tests/test_frame_map --markdown`:

| Frame input | Playing | Notes |
| --- | --- | --- |
| weapon hand trigger | fire | weapon hand trigger |
| other hand trigger | alt fire | other hand trigger |
| A | jump |  |
| B | crouch (hold) |  |
| X | use |  |
| Y | reload |  |
| left stick click | crouch (toggle) |  |
| right stick click | quick save | off hand behind the head |
| right stick click | flashlight |  |
| View (short) | quick load | off hand behind the head |
| View (short) | scoreboard | multiplayer |
| View (short) | flashlight |  |
| left stick up | move | move |
| left stick down | move | move |
| left stick left | move | move |
| left stick right | move | move |
| right stick left | turn | snap or smooth turn |
| right stick right | turn | snap or smooth turn |
| left bumper | previous weapon |  |
| right bumper | next weapon |  |
| D-pad left | previous weapon | alias of the left bumper |
| D-pad right | next weapon | alias of the right bumper |
| D-pad up | last weapon |  |
| D-pad down | laser sight / scope steady | laser sight, steadies a scope |
| weapon hand grip | backpack weapon | the crowbar, from behind the head |
| weapon hand grip | use | alias of X: grab, push, pull, press |
| other hand grip | hold the gun steady | two-handed hold, scope: near the weapon |
| other hand grip | use (other hand) | grab with this hand: well away from the weapon |
| Menu (short) | pause |  |
| Menu (hold 0.5 s) | quick save | held for half a second |
| View (hold 1 s) | recentre |  |
| View (hold 3 s) | recentre and set height |  |

| Frame input | In a menu | Notes |
| --- | --- | --- |
| weapon hand trigger | menu click | pointer click |
| A | menu confirm |  |
| B | menu back |  |
| Menu (short) | menu back | alias of B |
| View (short) | menu back | alias of B |
| D-pad up | menu up |  |
| D-pad down | menu down |  |
| D-pad left | menu left |  |
| D-pad right | menu right |  |
| left stick up | menu up | alias of the D-pad |
| left stick down | menu down | alias of the D-pad |
| left stick left | menu left | alias of the D-pad |
| left stick right | menu right | alias of the D-pad |
| left bumper | menu previous tab | previous tab or page |
| right bumper | menu next tab | next tab or page |
| right stick up | menu scroll up | scroll |
| right stick down | menu scroll down | scroll |

Aliases (one action, two inputs): use on X and the weapon hand's grip; previous / next weapon on the bumpers and D-pad left / right; flashlight on the right stick click and View;
quick save on Menu held and (hand behind the head) the right stick click; menu back on B, Menu and View; menu up/down/left/right on the D-pad and left stick. The host
test fails on any other action with two inputs, on any input doing two things in one context, and on an action nothing reaches.

## 5. Diagnostics (Frame build, `vr-diag.ini` in the data folder)

Set with `tools/steam_frame/deploy.sh --diag KEY=VAL`, removed with `--diag-reset`. No file, no effect.

| Key | Effect |
| --- | --- |
| `force_render` | draw and run the game while `shouldRender` is false, synthetic head at 1.6 m, hands parked in front of the chest, FOV and 64 mm eye offsets when the runtime gives none |
| `diag_yaw` | synthetic head yaw in degrees (turning left is positive); the hands follow |
| `autostart`, `map` | load a map without the menus after 150 rendered frames (`map` implies `autostart`; default `c1a0`) |
| `dump_frame` | on rendered frame N: `vr-dumps/vr-eye0.bmp`, `vr-eye1.bmp` (one synchronous readback each, with the driver's GL) and `vr-state.txt` (poses, FOVs, each eye's pose in head space, connection state, and 80 `probe` lines: rays from the left eye through a pixel grid traced into the world, with depth and the disparity a correct pair should show; `tools/steam_frame/analyze_stereo.py` matches the right eye there and compares). The log gets the driver's framebuffer bindings, viewport and scissor and the percentage of pixels that aren't black |
| `diag_roll` | synthetic head roll in degrees (tilting left is positive); with `force_render`. The HUD should stay level with the world |
| `diag_press` | holds buttons down: `A@300+30, LG@400+20` is A from 300 frames after the map starts for 30 frames, then the left grip (names: LT RT LG RG LB RB LC RC A B X Y MENU MENUHOLD VIEW UP DOWN LEFT RIGHT, LS/RS + UP DOWN LEFT RIGHT). Runs the map with no controllers; each press is logged `[vr] input -> action` |
| `diag_gap` | metres between the synthetic hands (default 0.5), for the two-handed hold (under 0.35) against the grab (over 0.55) |
| `cmd` | console commands (`;` between them) about 300 rendered frames after the map starts: `sv_cheats 1; impulse 101` for the suit and weapons, `vr_hud_distance 2`, `vr_hand_model 0` |
| `probe` | right before the dump, clear each eye to a flat colour (red, blue) instead: tests the readback and the swapchain, not the game |
| `diag_hand_yaw`, `diag_hand_pitch` | turn the synthetic hands' aim from the head's (degrees), to point the menu laser. `vr-state.txt` then has `pointer`, `hand_aim_pose`, `beam_origin`, `beam_end`, `layer_order` and each layer's pose; `tools/steam_frame/composite_menu.py DUMP OUT.png` composites them over the menu image |
| `load`, `exit_seconds` | `load` = a save name to `load` instead of a map after the start (`save000`); `exit_seconds` = leave through the normal shutdown N seconds after the first frame. `tools/steam_frame/perf_run.sh` uses both |
| `exit_after` | after N XR frames, leave through the normal shutdown |
| `perf` | `[vr-perf]` every 10 s, on by default when the file exists: fps, missed frames (period over 1.5x the display period), cpu ms from after the wait to after the submit, and the connection state (`game=active` once a map runs) |
| `oneshot` | rename the file to `vr-diag.ini.last` when it has been read |

Log prefixes: `[openxr]` (setup and state), `[vr-diag]` (what was read, autostart, exit), `[vr-dump]`, `[vr-perf]`, and the engine's own
messages under the tag `Xash` (developer level 3 by default on this build).

Eye geometry measured on the Frame (flags 0x3, valid): eye 0 at (-34.5, 0, 0) mm, eye 1 at (+34.5, 0, 0) mm, rotation 0 yaw / 0 pitch / 0 roll for
both, so 68.9 mm apart (the headset's IPD setting), fovs L -1.028 R 0.885 U 0.864 D -1.048 and L -0.877 R 1.033 U 0.866 D -1.042 rad. The engine's
fixed 65 mm was 6% short, and there is no cant. Ground-truth check on the eye images (after the fix): wall 10.2 m away 4.0 px measured against
4.05 predicted (11 probes within 0.2 px), 58.6 m away 0.1 against 0.7, walls at 2.4 to 4 m within 0.6 px, a floor point at 1.06 m 39.5
against 38.9 px; floor texture and blurred near floors don't match reliably (spread up to 8 px), which is a limit of the block matching,
not the render. HUD (`c1a0` with `impulse 101`): 41.0 px at `vr_hud_distance` 1 and 21.0 px at 2 (41.4 and 20.7 expected); the old fixed shift was
2 x 48 = 96 px, about 0.43 m.

Yaw 0 and 90 dumps of `c1a0` (the first corridor): the eyes differ by about the asymmetric-frustum axis offset, 90 turns the head
to the side wall. Those runs predate the release assets below, so they have no hands or weapons.

The repo git-ignores `*.mdl`, `*.wav` and `assets/background`, so a plain build has no `v_hand`, `v_torch` or replacement
weapons (`Mod_ForName ... couldn't load`). Run `tools/steam_frame/fetch_release_assets.sh` once before `assembleFrame`: it
takes the missing, ignored files from Team Beef's v1.7.4 release APK (SHA-256 pinned) and leaves tracked files alone. The APK
is then about 39 MB, like the release.

Host tests: `tools/steam_frame/tests/run_tests.sh` (binding tables against the profiles' component lists, unique action names,
the ini parser, the BMP writer, the booking check in `deploy.sh`).

## 6. Status

| Area | State |
| --- | --- |
| Gradle `frame` build, APK content, manifest | verified unattended: installs over adb and as the Steam devkit title `Lambda1VR` (shortcut appid 4086534518) |
| Steam title launch | verified unattended (2026-10-04): session SYNCHRONIZED, Frame/Index/Touch suggestions `XR_SUCCESS`, main menu at 72 fps, exit_after clean, Steam logs "Game process removed" |
| Release assets (hands, replacement weapons, sounds) | verified unattended: `v_hand` loads and the parked hands show in the eye dumps. `barney_vest`/`barney_helmet` and `sprites/scope` are still missing (upstream gaps too) |
| Khronos loader finds SteamVR | verified unattended (SteamVR/OpenXR 2.17.10) |
| Instance, session, swapchains, frame loop | verified unattended: 72 fps, 1728x1728, session SYNCHRONIZED |
| Standby handling | built, not run (force_render hides it); exit_after: verified unattended, clean |
| Exit from the SteamVR menu (EXITING) | built, not run |
| Data folder in Documents | verified unattended (Steam Half-Life loose files, no pak0.pak, load fine) |
| Bindings: Frame, Index, Touch | suggestions `XR_SUCCESS` unattended; host-checked against the component lists; no input tested |
| View hold recentre | built, not run |
| Diagnostics (force render, autostart, dumps, perf, exit_after, probe) | verified unattended |
| Eye dumps at yaw 0 and 90 | verified unattended (yaw 180 not done); stereo parity test not started |
| Eyes drawn and submitted from the runtime's eye poses | verified unattended (eye images against traced depths); not worn |
| HUD at `vr_hud_distance` | verified unattended at 1 and 2 m (disparity); not worn |
| Menu laser along the aim pose, quad layout | not started |
| Depth layer, motion vectors, foveation | not started |
| Controller map (HL25 gamepad layout, grips as hands, menu navigation) | built, host table test passes; the buttons ran unattended through `diag_press` on 2026-10-04 and logged the intended actions; not worn |
| HUD, text and camera-facing sprites kept level when the head rolls (`vr_hud_level`) | HUD verified unattended at roll +-20 (it turned with the world once the sign was fixed: +20.5 / -20.5 deg against +-20); sprites built, not measured; not worn |
| Menu laser on top of the screen (aim pose ray, beam and dot as layers after the screen) | verified unattended (2026-10-05) as far as standby allows: the dump's `vr-state.txt` has the hand's aim pose, the cursor (u 0.4765, v 0.5211 = pixel 1029, 1126, where the engine's own cursor is drawn), the layer order (screen, beam, dot) and each layer's pose; `tools/steam_frame/composite_menu.py` composites them over the eye 0 menu image from the left eye: the beam leaves the hand and ends in the dot on the cursor, both over the panel. That models the compositor, it isn't SteamVR's output (standby captures are black). Not worn |
| In-game laser sight (`vr_lasersight` 1 and 2) | not seen in four unattended dumps (glock, force_render, hand aimed three ways); the HUD and the weapon draw. Cause not found; needs a look in the headset or a server-side trace |
| Half-Life from Steam in place (`XASH3D_RODIR`) | verified unattended (2026-10-05): with 4174 byte-identical files moved out of `~/Documents/Lambda1VR/valve`, the log says `[data] game files: Steam, in place`, `fs_path` lists the Steam `valve/` as `rodir` and its wads (halflife.wad 3116 lumps), c1a0, the save and c2a5 load, and the hands and replacement weapons still come from Documents (no `Mod_ForName` errors for them). Reads are direct: Documents and Steam are ext4 bind mounts, not FUSE; SELinux is off in Lepton; no avc or EACCES. Case is handled (`exec MEDIA/STARTUPVIDS.TXT` finds `StartupVids.txt`) |
| `vr_refresh_rate`, `vr_resolution_scale` (default 1.25 = 2160) | `vr_resolution_scale` verified (1728, 2160, 2592, 3024 logged and drawn). `vr_refresh_rate` only picks from what the runtime offers (90 Hz alone), so it asked for 90 while SteamVR ran the title at its per-app 120: the rate comes from SteamVR's per-app setting, and only for the Steam title launch (an adb launch runs at 72) |
| 120 Hz and 2160 measurement | done unattended 2026-10-05 from the Steam title (standby, force_render, the user's control-room save): 120 fps, 0 missed at 1728 and 2160, 3 runs each, and in c2a5 at 2160 (3 runs), 2592 and 3024 (1 each); frame period 8.33 ms. Adreno 903 MHz 56-61% at 1728, 100% at 2160 and up. Standby: no display scan-out, so the compositor's cost with the headset worn is not in these numbers |
| Comfort set, haptics table, VR settings page | not started |
| Performance at the Frame's eye size | unattended numbers above; thread CPU 3.1 ms of 8.33, the rest of `cpu_ms` (7.5) is waits (acquire 2.25 ms, wait 0.65 ms, GPU-fenced calls). Worn (2026-10-05): the user saw a smooth 120 fps with the FPS counter on at 2160 per eye, "very sharp and very clean"; no worn frame-time capture |

## 7. Reference display settings (the build that was worn, 2026-10-05)

The user wore the Steam title at its defaults with the FPS counter on: smooth 120 fps, "very sharp and very clean", "feels really good".
This is the reference target for the other Frame ports. Everything below is read from the code or the logs; the file and function
names are the anchor, line numbers are for this commit.

| Setting | Value | Where |
| --- | --- | --- |
| Refresh | 120 Hz, from SteamVR's per-app setting (`~/.config/openvr/config/steamvr.vrsettings`: `steam.app.4086534518` `preferredRefreshRate` 120). `XR_FB_display_refresh_rate` offers only that one rate for an app (the logs said 90 on the first run), so the app's request can't raise it. Only a Steam title launch gets it; an adb launch ran at 72 | `TBXR_ChooseRefreshRate` (`TBXR_Common.c`:1537) |
| App's own request | `vr_refresh_rate` 90 (archived), the highest offered rate not over it, else the lowest. The run asked for 90 and got 120 from SteamVR | `L1VR_SurfaceView.c`:586, `TBXR_Common.c`:1537 |
| Frame period | 8.33 ms (`predictedDisplayPeriod`); `vr_refresh` (walking scale) is set from it, 120 | `L1VR_SurfaceView.c`:705-714 |
| Eye image | 2160 x 2160 = 1728 (recommended) x `vr_resolution_scale` 1.25 (archived, clamped 0.5 to 2, and to the view and swapchain maximum, 8192), rounded down to even. Log: `[openxr] eye image: recommended 1728x1728 ... scale 1.25, using 2160x2160` | `TBXR_SetEyeScale` (`TBXR_Common.c`:1504), `L1VR_SurfaceView.c`:587 |
| Supersampling | none: `SS_MULTIPLIER` is 1.0 on the Frame (1.3 on Quest), only the `-s` command line option changes it, and `TBXR_SetEyeScale` multiplies by it | `TBXR_Common.c`:60-65, `L1VR_SurfaceView.c`:964 |
| Colour format | `GL_SRGB8_ALPHA8` (0x8c43), the engine draws linear into sRGB images. Offered: 0x8c41, 0x8c43, 0x81a5, 0x81a6, 0x8cac | `ovrRenderer_Create` (`TBXR_Common.c`:820), `TBXR_CheckSwapchainFormats` |
| Samples | 1 on the swapchain (`sampleCount = 1`) and `NUM_MULTI_SAMPLES = 1`: no MSAA and no `EXT_multisampled_render_to_texture`. The Frame path attaches the swapchain texture and a depth renderbuffer with the driver's own GLES 3 calls (`GenRenderbuffers`, `FramebufferTexture2D`). The engine's `gl_msaa` is 0 | `TBXR_Common.c`:58, 577, 625-660; `vid_common.c`:901; `opengl.cfg` |
| Swapchain | one per eye, 3 images, 2D (array size 1, face count 1, 1 mip). Log: `swapchain: format 0x8c43, 2160x2160, 3 images, multiview off (one swapchain per eye), 1 samples` | `ovrFramebuffer_Create`, `TBXR_Common.c`:699 |
| Multiview | not used. `GL_OVR_multiview(2)` is advertised, but `glExtensions.multi_view` is detected and never read, and the engine draws each eye in its own pass | `TBXR_Common.c`:260, 279 |
| Depth | `GL_DEPTH_COMPONENT24` renderbuffer per swapchain image, no stencil (the engine's `gl_stencilbits` 8 has no effect on the eye FBO), `glInvalidateFramebuffer` on it after each eye so the tiler doesn't write it back. No `XrCompositionLayerDepthInfoKHR` is submitted | `TBXR_Common.c`:638, 731 |
| Foveation | none. The enum has Quest's `FOVEATION_*` entries and nothing sets them; no `XR_FB_foveation` use on the Frame; the loader log shows Valve's `fdm_injection` XR layer loaded, and I haven't checked what it does for this app | `TBXR_Common.c`:98, 117 |
| Layers | one projection layer, flags `BLEND_TEXTURE_SOURCE_ALPHA` and `CORRECT_CHROMATIC_ABERRATION`, each view with its own pose and the runtime's asymmetric FOV; quad layers for the menu, laser beam and dot | `TBXR_Common.c`:2757-2758, 2815 (quad layers) |
| EGL | ES 3 context (`EGL_CONTEXT_CLIENT_VERSION` 3), RGBA8888, no depth, no stencil, 0 samples | `ovrEgl_CreateContext`, `TBXR_Common.c`:416 |
| gl4es | built with `-DDEFAULT_ES=2`, linked against `libGLESv3.so`: log says `Using GLES 2.0 backend`, "Targeting OpenGL 2.0", full NPOT, FBO in core, `GL_EXT_texture_filter_anisotropic` (max 16), `GL_OES_depth24`, `GL_EXT_color_buffer_float`, sRGB surface supported; Mesa (Zink on Turnip). So the game's GL is gl4es on a GLES 2 backend, in a GLES 3 context, and only the swapchain attachment calls are GLES 3 | `gl4es/Android.mk`:80-84 |
| Texture filtering | `gl_texturemode GL_LINEAR_MIPMAP_LINEAR` (trilinear), `gl_anisotropy 8` (clamped to the driver's 16), `gl_texture_lodbias 0`, `gl_picmip 0`, `gl_skymip 0`, `gl_round_down 0`, `gl_max_size 512` (legacy, no effect), `gl_compress_textures 0`. Eye framebuffer images are `GL_LINEAR` / clamp to edge | `vid_common.c`:886-901; `TBXR_Common.c`:620-624 |
| Other renderer defaults that were in effect | `r_vbo 0`, `r_bump 0`, `r_detailtextures 0` (the device's saved config; the code default is 1), `r_dynamic 1`, `r_lighting_modulate 0.6`, `r_lighting_ambient 0.3`, `gl_overbright 0`, `gl_swapInterval 0`, `gl_finish 0`, `gl_allow_mirrors` forced 0 | the device's `valve/config.cfg`, `vid_android.c`:135 |
| Gamma | `assets/config.cfg` ships `gamma 2.0`, `brightness 0.8` (code default gamma 1.0). The data copy on the Frame had `gamma 2.5` and `fps_max 144`, saved 2026-10-04 21:26, and was read at start of the worn session | `assets/config.cfg`, `valve/config.cfg` on the device |
| Frame cap | `fps_max 144` in the device's config; the compositor's 120 Hz wait paces the loop (`wait_ms` 0.65, `acquire_ms` 2.25) | `[vr-perf]` |

Measured unattended at this configuration (Steam title launch, standby with `force_render`, `scratchpad/step4/summary.txt`; 3 runs per row,
`[vr-perf]` every 10 s):

| Eye size | fps (min..avg) | missed | thread CPU | wait | acquire | `cpu_ms` | Adreno at 903 MHz |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1728 | 120.0..120.0 | 0 | 3.08-3.12 ms | 0.64-0.68 | 2.22-2.26 | 7.43-7.49 | 56-61% |
| 2160 (reference) | 120.0..120.0 | 0 | 3.07-3.11 ms | 0.65-0.66 | 2.24-2.27 | 7.46-7.48 | 100% |
| 2160, `c2a5` (busier map) | 120.0..120.0 | 0 | 3.14-3.18 ms | 1.60-1.66 | 2.25-2.27 | 6.46-6.53 | 100% |
| 2592, 3024 (`c2a5`, 1 run each) | 120.0..120.0 | 0 | 3.17, 3.19 ms | 1.65, 1.82 | 2.24, 2.07 | 6.49, 6.30 | 100% |

Thread CPU is about 3.1 ms of the 8.33 ms budget at every size, and the Adreno is at 100% from 2160 up, so what limits the frame is the GPU side,
not the game thread. The first 10 s window of a run is short of 120 (s125_r1: 107.7 fps, 9 missed, thread CPU 4.06 ms while the map loads) and isn't in the summary. These numbers are from standby: no
display scan-out, so the compositor's cost with the headset worn isn't in them; the user's report is the worn check.
