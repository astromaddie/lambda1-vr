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
4. Both eyes use the head pose as the layer pose and the located FOV; the engine adds the eye separation itself.
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
`vr_worldscale * VR_IPD / 2` (a fixed 0.065 IPD, not the runtime's eye poses); the projection is the headset's
asymmetric per-eye FOV (`VR_GetVRProjection`); 2D content gets `R_Get2DOffset` (`E/client/gl_draw.c`), the pixel
shift from the optical axis; `GetStereoDepthOffset` adds +-width/36 per eye for HUD items (two copies, `E/client/cl_scrn.c`
and the client DLL's `hud.cpp`).

| Category | Where | Rule it uses now | Notes |
| --- | --- | --- | --- |
| HUD | `E/client/cl_view.c` `V_PostRender`, `cl_game.c` `CL_DrawHUD`, client DLL | 2D ortho with the axis shift; most elements add the depth offset (crossed disparity, about 0.6 m) | hud_scale 1.3; centerprint, messages, scoreboard and VGUI have no offset (infinity); the offset isn't turned off on the screen layer; the HUD redraws once per eye |
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

`OpenXrInput.c`: actions are the Quest ones plus `viewkey` and `bumperkey`. Profiles suggested on the Frame build,
each logged with the runtime's answer: Frame, Index, Touch (Pico isn't touched). `TBXR_UpdateFrameLayout` maps the Frame's
buttons into the Quest shape when the runtime reports the Frame profile active: A crouch, B jump, left bumper or right X
torch, View tap or right Y screen view, right Menu pause, View held 1 s / 3 s recentre. Left-handed schemes swap which
state the bits land on. Compared with `standard.md`: Menu and View follow it; A and B keep the game's crouch and jump
rather than the standard's jump/back; there's no VR settings page and no dominant-hand setting beyond `vr_control_scheme`.

## 5. Diagnostics (Frame build, `vr-diag.ini` in the data folder)

Set with `tools/steam_frame/deploy.sh --diag KEY=VAL`, removed with `--diag-reset`. No file, no effect.

| Key | Effect |
| --- | --- |
| `force_render` | draw and run the game while `shouldRender` is false, synthetic head at 1.6 m, hands parked in front of the chest, FOV and 64 mm eye offsets when the runtime gives none |
| `diag_yaw` | synthetic head yaw in degrees (turning left is positive); the hands follow |
| `autostart`, `map` | load a map without the menus after 150 rendered frames (`map` implies `autostart`; default `c1a0`) |
| `dump_frame` | on rendered frame N: `vr-dumps/vr-eye0.bmp`, `vr-eye1.bmp` (one synchronous readback each, with the driver's GL) and `vr-state.txt` (poses, FOVs, connection state). The log gets the driver's framebuffer bindings, viewport and scissor and the percentage of pixels that aren't black |
| `probe` | right before the dump, clear each eye to a flat colour (red, blue) instead: tests the readback and the swapchain, not the game |
| `exit_after` | after N XR frames, leave through the normal shutdown |
| `perf` | `[vr-perf]` every 10 s, on by default when the file exists: fps, missed frames (period over 1.5x the display period), cpu ms from after the wait to after the submit, and the connection state (`game=active` once a map runs) |
| `oneshot` | rename the file to `vr-diag.ini.last` when it has been read |

Log prefixes: `[openxr]` (setup and state), `[vr-diag]` (what was read, autostart, exit), `[vr-dump]`, `[vr-perf]`, and the engine's own
messages under the tag `Xash` (developer level 3 by default on this build).

Yaw 0 and 90 dumps of `c1a0` (the first corridor): the eyes differ by about the asymmetric-frustum axis offset, 90 turns the head
to the side wall. The repo's `assets` have no `.mdl` files, so `v_hand`, `v_torch` and the replacement weapons are missing
(`Mod_ForName ... couldn't load` in the log).

Host tests: `tools/steam_frame/tests/run_tests.sh` (binding tables against the profiles' component lists, unique action names,
the ini parser, the BMP writer, the booking check in `deploy.sh`).

## 6. Status

| Area | State |
| --- | --- |
| Gradle `frame` build, APK content, manifest | built, not run |
| Khronos loader finds SteamVR | verified unattended (SteamVR/OpenXR 2.17.10) |
| Instance, session, swapchains, frame loop | verified unattended: 72 fps, 1728x1728, session SYNCHRONIZED |
| Standby handling | built, not run (force_render hides it); exit_after: verified unattended, clean |
| Exit from the SteamVR menu (EXITING) | built, not run |
| Data folder in Documents | verified unattended (Steam Half-Life loose files, no pak0.pak, load fine) |
| Bindings: Frame, Index, Touch | suggestions `XR_SUCCESS` unattended; host-checked against the component lists; no input tested |
| View hold recentre | built, not run |
| Diagnostics (force render, autostart, dumps, perf, exit_after, probe) | verified unattended |
| Eye dumps at yaw 0 and 90 | verified unattended (yaw 180 not done); stereo parity test not started |
| HUD placement (depth offset, quad, legibility) | not started |
| Menu laser along the aim pose, quad layout | not started |
| Depth layer, motion vectors, foveation | not started |
| Comfort set, haptics table, VR settings page | not started |
| Performance at the Frame's eye size | not started |
