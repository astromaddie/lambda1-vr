# Lambda1VR on the Steam Frame: how it fits together

Current state of the Frame build (`assembleFrame`, `L1VR_STEAM_FRAME`). The public
description is in [STEAM_FRAME_VR.md](STEAM_FRAME_VR.md); this one is for development.
First unattended device runs 2026-10-04 (headset on a desk, over adb, nobody wearing it); nothing has been worn yet.

## 1. Target and probe facts

| Item | Value |
| --- | --- |
| Device | Valve Steam Frame, Snapdragon 8 Gen 3, Adreno 750 |
| Container | Lepton: Android 11 (API 30), arm64-v8a, GLES 3.2 through Zink on Turnip |
| Runtime | SteamVR OpenXR 2.17.10 at `/vendor/etc/openxr/1/active_runtime.json`; no loader broker, so the Khronos loader 1.1.63 is in the APK |
| Recommended eye size | 1728x1728, asymmetric FOV (left eye about L -1.028, R 0.885, U 0.864, D -1.048 rad) |
| Refresh | the rate set for the app in SteamVR (72 Hz by default); `XR_FB_display_refresh_rate` offers only that |
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
the grip, move stick, torch and screen view on its two buttons, laser on its stick-click bit) and into commands for what the scheme code doesn't have
(alt fire, reload, weapon select, last weapon, crowbar, quick save, menu keys, recentre). `OpenXrInput.c` reads the physical buttons (new `dpad*` actions) and logs the
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

Not kept from the standard: dominant-hand setting beyond `vr_control_scheme`, no VR settings page.

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
| View (short) | quick save | off hand behind the head |
| View (short) | screen view / scoreboard | the scoreboard |
| View (short) | flashlight |  |
| left stick up | move | move |
| left stick down | move | move |
| left stick left | move | move |
| left stick right | move | move |
| right stick left | turn | snap or smooth turn |
| right stick right | turn | snap or smooth turn |
| right stick up | quick load | off hand behind the head |
| right stick up | screen view / scoreboard | flat screen view, the scoreboard in multiplayer |
| right stick down | laser sight / scope steady | laser sight, steadies a scope |
| left bumper | previous weapon |  |
| right bumper | next weapon |  |
| D-pad up | previous weapon | alias of the left bumper |
| D-pad down | next weapon | alias of the right bumper |
| D-pad right | last weapon |  |
| D-pad left | crowbar |  |
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

Aliases (one action, two inputs): use on X and the weapon hand's grip; previous / next weapon on the bumpers and D-pad up / down; flashlight on the right stick click and View;
quick save on Menu held and (hand behind the head) the right stick click and View; menu back on B, Menu and View; menu up/down/left/right on the D-pad and left stick. The host
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
| Comfort set, haptics table, VR settings page | not started |
| Performance at the Frame's eye size | not started |
