#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>
#include <time.h>

#include <common/common.h>
#include <client/client.h>

#include "VrDiag.h"
#include "VrDiagCore.h"
#include "VrEyeMath.h"
#include "VrFrameMap.h"
#include "VrCommon.h"

extern convar_t *vr_worldscale;
extern ovrApp gAppState;
extern float snapTurn;
void TBXR_exit(int exitCode);

#define DIAG_DIR "/sdcard/Documents/Lambda1VR"
#define DIAG_FILE DIAG_DIR "/vr-diag.ini"
#define DUMP_DIR DIAG_DIR "/vr-dumps"

static vrDiagConfig_t config;
static bool enabled = false;

static long frames = 0;			// XR frames since the loop started
static long renderedFrames = 0;	// the ones the game was drawn for
static bool mapStarted = false;
static long mapStartFrame = 0;
static bool cmdDone = false;
static bool dumpDone = false;
static bool dumpEyeDone[2];

// perf window
static double windowStart = 0.0;
static double lastBegin = 0.0;
static double cpuSum = 0.0;
static double waitSum = 0.0, acquireSum = 0.0;	// ms in xrWaitFrame and xrAcquireSwapchainImage (+ its wait)
static double threadStart = 0.0, threadCpuNow = 0.0;	// the game thread's CPU time
static long windowFrames = 0;
static long windowMissed = 0;

void VrDiag_NoteTiming(int what, double ms)
{
	if (what == VRDIAG_TIME_WAIT_FRAME)
		waitSum += ms;
	else
		acquireSum += ms;
}

static double threadCpuMs(void)
{
	struct timespec now;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
	return now.tv_sec * 1000.0 + now.tv_nsec / 1e6;
}

void VrDiag_Init(void)
{
	VrDiagCore_Defaults(&config);

	FILE* file = fopen(DIAG_FILE, "rb");
	if (file == NULL)
	{
		return;
	}

	char text[4096];
	size_t length = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[length] = '\0';

	if (VrDiagCore_Parse(text, &config) == 0)
	{
		return;
	}

	enabled = true;
	ALOGI("[vr-diag] %s: force_render=%d diag_yaw=%.1f autostart=%d map=%s dump_frame=%d exit_after=%d perf=%d probe=%d oneshot=%d cmd=%s diag_roll=%.1f diag_gap=%.2f presses=%d",
		  DIAG_FILE, config.forceRender, config.yaw, config.autostart, config.map[0] ? config.map : "-",
		  config.dumpFrame, config.exitAfter, config.perf, config.probe, config.oneshot, config.cmd[0] ? config.cmd : "-", config.roll, config.gap, config.pressCount);

	if (config.oneshot)
	{
		rename(DIAG_FILE, DIAG_FILE ".last");
		ALOGI("[vr-diag] one-shot: the file is now vr-diag.ini.last");
	}
}

bool VrDiag_ForceRender(void)
{
	return enabled && config.forceRender;
}

bool VrDiag_Injecting(void)
{
	return enabled && config.pressCount > 0;
}

bool VrDiag_InjectInputs(frameRaw_t* raw)
{
	if (!VrDiag_Injecting() || !mapStarted)
	{
		return false;
	}

	bool any = false;
	for (int i = 0; i < config.pressCount; i++)
	{
		const vrDiagPress_t* press = &config.presses[i];
		const long first = mapStartFrame + press->start;
		if (renderedFrames < first || renderedFrames >= first + press->length)
		{
			continue;
		}
		any = true;
		switch (press->input)
		{
			case FI_L_TRIGGER: raw->triggerLeft = 1.0f; break;
			case FI_R_TRIGGER: raw->triggerRight = 1.0f; break;
			case FI_L_GRIP: raw->gripLeft = 1.0f; break;
			case FI_R_GRIP: raw->gripRight = 1.0f; break;
			case FI_L_BUMPER: raw->bumperLeft = true; break;
			case FI_R_BUMPER: raw->bumperRight = true; break;
			case FI_L_STICK_CLICK: raw->stickClickLeft = true; break;
			case FI_R_STICK_CLICK: raw->stickClickRight = true; break;
			case FI_A: raw->a = true; break;
			case FI_B: raw->b = true; break;
			case FI_X: raw->x = true; break;
			case FI_Y: raw->y = true; break;
			case FI_MENU_SHORT: case FI_MENU_HOLD: raw->menu = true; break;
			case FI_VIEW_SHORT: case FI_VIEW_HOLD_1S: case FI_VIEW_HOLD_3S: raw->view = true; break;
			case FI_DPAD_UP: raw->dpadUp = true; break;
			case FI_DPAD_DOWN: raw->dpadDown = true; break;
			case FI_DPAD_LEFT: raw->dpadLeft = true; break;
			case FI_DPAD_RIGHT: raw->dpadRight = true; break;
			case FI_L_STICK_UP: raw->stickLeftY = 0.9f; break;
			case FI_L_STICK_DOWN: raw->stickLeftY = -0.9f; break;
			case FI_L_STICK_LEFT: raw->stickLeftX = -0.9f; break;
			case FI_L_STICK_RIGHT: raw->stickLeftX = 0.9f; break;
			case FI_R_STICK_UP: raw->stickRightY = 0.9f; break;
			case FI_R_STICK_DOWN: raw->stickRightY = -0.9f; break;
			case FI_R_STICK_LEFT: raw->stickRightX = -0.9f; break;
			case FI_R_STICK_RIGHT: raw->stickRightX = 0.9f; break;
			default: break;
		}
	}
	return any;
}

static XrQuaternionf yawQuat(float degrees)
{
	XrQuaternionf q = {0.0f, sinf(DEG2RAD(degrees) * 0.5f), 0.0f, cosf(DEG2RAD(degrees) * 0.5f)};
	return q;
}

bool VrDiag_SyntheticHead(XrPosef* pose)
{
	if (!VrDiag_ForceRender())
	{
		return false;
	}
	memset(pose, 0, sizeof(*pose));
	// yaw about up, then roll about the view axis (z)
	const XrQuaternionf y = yawQuat(config.yaw);
	const float half = DEG2RAD(config.roll) * 0.5f;
	const XrQuaternionf r = {0.0f, 0.0f, sinf(half), cosf(half)};
	pose->orientation.x = y.w * r.x + y.x * r.w + y.y * r.z - y.z * r.y;
	pose->orientation.y = y.w * r.y - y.x * r.z + y.y * r.w + y.z * r.x;
	pose->orientation.z = y.w * r.z + y.x * r.y - y.y * r.x + y.z * r.w;
	pose->orientation.w = y.w * r.w - y.x * r.x - y.y * r.y - y.z * r.z;
	pose->position.y = 1.6f;	// standing height above the floor
	return true;
}

bool VrDiag_SyntheticViews(XrView* views)
{
	if (!VrDiag_ForceRender())
	{
		return false;
	}

	// The Frame's usual asymmetric field of view, when the runtime gave none
	// left eye L -1.028 R 0.885 U 0.864 D -1.048, the right eye is its mirror
	for (int eye = 0; eye < ovrMaxNumEyes; eye++)
	{
		XrFovf* fov = &views[eye].fov;
		if (fabsf(fov->angleLeft) + fabsf(fov->angleRight) < 0.1f)
		{
			fov->angleLeft = (eye == 0) ? -1.028f : -0.885f;
			fov->angleRight = (eye == 0) ? 0.885f : 1.028f;
			fov->angleUp = 0.864f;
			fov->angleDown = -1.048f;
		}
		views[eye].pose.orientation.w = 1.0f;
		views[eye].pose.position.x = (eye == 0) ? -0.032f : 0.032f;	// 64 mm
	}
	return true;
}

bool VrDiag_SyntheticHand(int hand, ovrTrackedController* controller)
{
	if (!VrDiag_ForceRender())
	{
		return false;
	}

	// Parked in front of the chest, turned with the head
	const float side = (hand == 0) ? -config.gap * 0.5f : config.gap * 0.5f;
	const float rad = DEG2RAD(config.yaw);
	const float x = side;
	const float z = -0.3f;
	controller->Active = GL_TRUE;
	memset(&controller->Pose, 0, sizeof(controller->Pose));
	{
		// aimed where the head looks, or turned from it: yaw about up, then pitch about the hand's own x
		const XrQuaternionf y = yawQuat(config.yaw + config.handYaw);
		const float half = DEG2RAD(config.handPitch) * 0.5f;
		const XrQuaternionf p = {sinf(half), 0.0f, 0.0f, cosf(half)};
		controller->Pose.orientation.x = y.w * p.x + y.x * p.w + y.y * p.z - y.z * p.y;
		controller->Pose.orientation.y = y.w * p.y - y.x * p.z + y.y * p.w + y.z * p.x;
		controller->Pose.orientation.z = y.w * p.z + y.x * p.y - y.y * p.x + y.z * p.w;
		controller->Pose.orientation.w = y.w * p.w - y.x * p.x - y.y * p.y - y.z * p.z;
	}
	controller->Pose.position.x = x * cosf(rad) + z * sinf(rad);
	controller->Pose.position.y = 1.2f;
	controller->Pose.position.z = -x * sinf(rad) + z * cosf(rad);
	memset(&controller->Velocity, 0, sizeof(controller->Velocity));
	controller->Velocity.type = XR_TYPE_SPACE_VELOCITY;
	return true;
}

static const char* connectionName(void)
{
	switch (cls.state)
	{
		case ca_uninitialized:	return "uninitialized";
		case ca_disconnected:	return "disconnected";
		case ca_connecting:		return "connecting";
		case ca_connected:		return "connected";
		case ca_active:			return "active";
		case ca_cinematic:		return "cinematic";
		default:				return "unknown";
	}
}

// Ground truth for the stereo check: rays from the left eye through a grid of pixels, traced
// into the world, so each gets the depth along the view axis and the disparity a correct
// stereo pair should show there (the eye separation times the focal length over the depth).
static void dumpDepthProbes(FILE* file)
{
	if (gAppState.Projections == NULL || cls.state != ca_active)
	{
		return;
	}

	const XrView* v0 = &gAppState.Projections[0];
	const XrView* v1 = &gAppState.Projections[1];
	const int width = gAppState.Renderer.FrameBuffer[0].Width;
	const int height = gAppState.Renderer.FrameBuffer[0].Height;
	const float fx = VrEye_FocalPixels(v0->fov.angleLeft, v0->fov.angleRight, width);
	const float fy = (float)height / (tanf(v0->fov.angleUp) - tanf(v0->fov.angleDown));
	const float axisX = VrEye_AxisPixel(v0->fov.angleLeft, v0->fov.angleRight, width);
	const float axisY = tanf(v0->fov.angleUp) / (tanf(v0->fov.angleUp) - tanf(v0->fov.angleDown)) * (float)height;
	const float ipd = v1->pose.position.x - v0->pose.position.x;

	vec3_t forward, right, up;
	AngleVectors(cl.refdef.viewangles, forward, right, up);

	// the left eye: the middle of the head, moved by the eye's offset in the head's frame
	const float position[3] = {v0->pose.position.x, v0->pose.position.y, v0->pose.position.z};
	float offset[3];
	VrEye_ToEngineOffset(position, vr_worldscale->value, offset);
	vec3_t eye;
	for (int i = 0; i < 3; i++)
	{
		eye[i] = cl.refdef.vieworg[i] + forward[i] * offset[0] - right[i] * offset[1] + up[i] * offset[2];
	}

	fprintf(file, "probe_setup units_per_m %.2f ipd_m %.5f focal_px %.2f axis_px %.2f %.2f eye0_world %.2f %.2f %.2f view_angles %.2f %.2f %.2f\n",
			vr_worldscale->value, ipd, fx, axisX, axisY, eye[0], eye[1], eye[2],
			cl.refdef.viewangles[0], cl.refdef.viewangles[1], cl.refdef.viewangles[2]);

	for (float fv = 0.20f; fv < 0.97f; fv += 0.08f)
	{
		for (float fu = 0.15f; fu < 0.86f; fu += 0.1f)
		{
			const float u = fu * width;
			const float v = fv * height;
			const float left = -(u - axisX) / fx;	// to the left of the view axis, per unit forward
			const float upward = (axisY - v) / fy;

			vec3_t dir, end;
			for (int i = 0; i < 3; i++)
			{
				dir[i] = forward[i] - right[i] * left + up[i] * upward;
				end[i] = eye[i] + dir[i] * 8192.0f;
			}

			pmtrace_t trace = CL_TraceLine(eye, end, PM_WORLD_ONLY);
			if (trace.fraction >= 1.0f || trace.allsolid)
			{
				continue;
			}
			vec3_t hit;
			for (int i = 0; i < 3; i++)
			{
				hit[i] = eye[i] + (end[i] - eye[i]) * trace.fraction;
			}
			const float depthUnits = (hit[0] - eye[0]) * forward[0] + (hit[1] - eye[1]) * forward[1] + (hit[2] - eye[2]) * forward[2];
			const float depthM = depthUnits / vr_worldscale->value;
			if (depthM < 0.25f)
			{
				continue;
			}
			fprintf(file, "probe %.0f %.0f depth_m %.3f disparity_px %.2f\n", u, v, depthM, ipd * fx / depthM);
		}
	}
}

static void dumpState(void)
{
	char path[256];
	snprintf(path, sizeof(path), DUMP_DIR "/vr-state.txt");
	FILE* file = fopen(path, "w");
	if (file == NULL)
	{
		return;
	}

	fprintf(file, "xr_frame %ld\nrendered_frame %ld\nshould_render %d\nforced %d\n", frames, renderedFrames,
			(int)gAppState.FrameState.shouldRender, VrDiag_ForceRender() ? 1 : 0);
	fprintf(file, "eye_size %dx%d\n", gAppState.Renderer.FrameBuffer[0].Width, gAppState.Renderer.FrameBuffer[0].Height);
	fprintf(file, "head_position %.3f %.3f %.3f\n", hmdPosition[0], hmdPosition[1], hmdPosition[2]);
	fprintf(file, "head_angles pitch %.2f yaw %.2f roll %.2f\n", hmdorientation[PITCH], hmdorientation[YAW], hmdorientation[ROLL]);
	fprintf(file, "player_yaw %.2f\nsnap_turn %.2f\ndiag_yaw %.2f\ndiag_roll %.2f\nview_roll %.2f\nvr_fov_x %d\n", playerYaw, snapTurn,
			config.yaw, config.roll, cl.refdef.viewangles[ROLL], vrFOV);
	if (gAppState.Projections != NULL)
	{
		for (int eye = 0; eye < ovrMaxNumEyes; eye++)
		{
			const XrFovf* fov = &gAppState.Projections[eye].fov;
			fprintf(file, "fov_eye%d L %.3f R %.3f U %.3f D %.3f\n", eye, fov->angleLeft, fov->angleRight, fov->angleUp, fov->angleDown);
			const XrPosef* pose = &gAppState.Projections[eye].pose;
			fprintf(file, "eye%d_in_head_mm %.2f %.2f %.2f quat %.5f %.5f %.5f %.5f\n", eye, pose->position.x * 1000.0f,
					pose->position.y * 1000.0f, pose->position.z * 1000.0f, pose->orientation.x, pose->orientation.y,
					pose->orientation.z, pose->orientation.w);
		}
	}
	dumpDepthProbes(file);
	{
		float pu = 0.0f, pv = 0.0f;
		const bool aimed = TBXR_ScreenPointer(&pu, &pv);
		fprintf(file, "pointer %s u %.4f v %.4f (the cursor at %.0f, %.0f in the screen image)\n", aimed ? "aimed" : "none", pu, pv,
				pu * gAppState.Renderer.FrameBuffer[0].Width, pv * gAppState.Renderer.FrameBuffer[0].Height);
	}
	fprintf(file, "screen_layer %d\nconnection %s\nkey_dest %d\nmaxclients %d\n", VR_UseScreenLayer() ? 1 : 0,
			connectionName(), (int)cls.key_dest, (int)cl.maxclients);
	fclose(file);
}

// The driver's GL for the readback: the plain gl* names in this library are gl4es's,
// which doesn't know the swapchain's framebuffers
typedef void (GL_APIENTRYP PFN_BindFramebuffer)(GLenum target, GLuint framebuffer);
typedef void (GL_APIENTRYP PFN_ReadPixels)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels);
typedef GLenum (GL_APIENTRYP PFN_GetError)(void);
typedef void (GL_APIENTRYP PFN_Finish)(void);
typedef void (GL_APIENTRYP PFN_GetIntegerv)(GLenum pname, GLint* data);
typedef GLboolean (GL_APIENTRYP PFN_IsEnabled)(GLenum cap);
typedef void (GL_APIENTRYP PFN_ClearColor)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
typedef void (GL_APIENTRYP PFN_Clear)(GLbitfield mask);
typedef void (GL_APIENTRYP PFN_Disable)(GLenum cap);

// One synchronous readback on the chosen frame, it's a test dump
void VrDiag_AfterEye(int eye)
{
	if (!enabled || config.dumpFrame <= 0 || dumpDone || renderedFrames < config.dumpFrame || eye < 0 || eye > 1 || dumpEyeDone[eye])
	{
		return;
	}

	ovrFramebuffer* frameBuffer = &gAppState.Renderer.FrameBuffer[eye];
	const int width = frameBuffer->Width;
	const int height = frameBuffer->Height;
	unsigned char* pixels = (unsigned char*)malloc((size_t)width * (size_t)height * 4);
	if (pixels == NULL)
	{
		return;
	}

	mkdir(DUMP_DIR, 0777);

	PFN_BindFramebuffer driverBind = (PFN_BindFramebuffer)eglGetProcAddress("glBindFramebuffer");
	PFN_ReadPixels driverRead = (PFN_ReadPixels)eglGetProcAddress("glReadPixels");
	PFN_GetError driverError = (PFN_GetError)eglGetProcAddress("glGetError");
	PFN_Finish driverFinish = (PFN_Finish)eglGetProcAddress("glFinish");
	if (driverBind == NULL || driverRead == NULL || driverError == NULL || driverFinish == NULL)
	{
		free(pixels);
		dumpDone = true;
		ALOGE("[vr-dump] the driver's GL functions weren't found");
		return;
	}

	glFinish();		// gl4es flushes what it holds back, and finishes
	const GLenum drawError = driverError();	// whatever the game left behind

	// what the driver thinks the state is after the game drew this eye
	{
		PFN_GetIntegerv getInt = (PFN_GetIntegerv)eglGetProcAddress("glGetIntegerv");
		PFN_IsEnabled isEnabled = (PFN_IsEnabled)eglGetProcAddress("glIsEnabled");
		if (getInt != NULL && isEnabled != NULL)
		{
			GLint drawFb = -1, readFb = -1, viewport[4] = {0}, scissor[4] = {0};
			getInt(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &drawFb);
			getInt(0x8CAA /*GL_READ_FRAMEBUFFER_BINDING*/, &readFb);
			getInt(GL_VIEWPORT, viewport);
			getInt(GL_SCISSOR_BOX, scissor);
			ALOGI("[vr-dump] eye %d state: draw fb %d, read fb %d, eye fb %u, viewport %d %d %d %d, scissor %s %d %d %d %d, gl error 0x%x",
				  eye, drawFb, readFb, frameBuffer->FrameBuffers[frameBuffer->TextureSwapChainIndex],
				  viewport[0], viewport[1], viewport[2], viewport[3], isEnabled(GL_SCISSOR_TEST) ? "on" : "off",
				  scissor[0], scissor[1], scissor[2], scissor[3], (unsigned int)drawError);
		}
	}

	if (config.probe)
	{
		// not the game's picture: a flat colour, to see if the readback works at all
		PFN_ClearColor driverClearColor = (PFN_ClearColor)eglGetProcAddress("glClearColor");
		PFN_Clear driverClear = (PFN_Clear)eglGetProcAddress("glClear");
		PFN_Disable driverDisable = (PFN_Disable)eglGetProcAddress("glDisable");
		if (driverClearColor != NULL && driverClear != NULL && driverDisable != NULL)
		{
			driverBind(GL_FRAMEBUFFER, frameBuffer->FrameBuffers[frameBuffer->TextureSwapChainIndex]);
			driverDisable(GL_SCISSOR_TEST);
			driverClearColor(eye == 0 ? 1.0f : 0.0f, 0.0f, eye == 0 ? 0.0f : 1.0f, 1.0f);
			driverClear(GL_COLOR_BUFFER_BIT);
		}
	}

	driverError();	// clear
	driverBind(GL_READ_FRAMEBUFFER, frameBuffer->FrameBuffers[frameBuffer->TextureSwapChainIndex]);
	driverRead(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	const GLenum error = driverError();
	driverBind(GL_READ_FRAMEBUFFER, 0);
	driverFinish();

	// how much of the picture isn't black, for the log
	long lit = 0;
	for (long px = 0; px < (long)width * height; px++)
	{
		if (pixels[px * 4] | pixels[px * 4 + 1] | pixels[px * 4 + 2])
		{
			lit++;
		}
	}

	char path[256];
	snprintf(path, sizeof(path), DUMP_DIR "/vr-eye%d.bmp", eye);
	const bool ok = VrDiagCore_WriteBmp(path, pixels, width, height);
	free(pixels);

	ALOGI("[vr-dump] eye %d at rendered frame %ld: %dx%d, %s, gl error 0x%x, %.1f%% not black, game %s", eye, renderedFrames,
		  width, height, ok ? path : "write failed", (unsigned int)error, 100.0 * lit / ((double)width * height), connectionName());

	dumpEyeDone[eye] = true;
	if (dumpEyeDone[0] && dumpEyeDone[1])
	{
		dumpState();
		dumpDone = true;
		ALOGI("[vr-dump] state written to " DUMP_DIR "/vr-state.txt");
	}
}

void VrDiag_FrameBegin(void)
{
	if (!enabled)
	{
		return;
	}

	const double now = TBXR_GetTimeInMilliSeconds();
	if (threadStart == 0.0)
		threadStart = threadCpuMs();
	frames++;
	if (TBXR_ShouldRender())
	{
		renderedFrames++;
	}

	// perf bookkeeping
	if (windowStart == 0.0)
	{
		windowStart = now;
	}
	else if (lastBegin != 0.0)
	{
		const double period = gAppState.FrameState.predictedDisplayPeriod / 1e6;	// ns to ms
		if (period > 0.0 && (now - lastBegin) > period * 1.5)
		{
			windowMissed++;
		}
	}
	lastBegin = now;
	windowFrames++;

	if (config.perf && (now - windowStart) >= 10000.0)
	{
		const double seconds = (now - windowStart) / 1000.0;
		// the thread's own CPU time over the window, against the wall time: what's left is waiting
		const double threadMs = threadCpuMs();
		const double cpuPerFrame = windowFrames ? (threadMs - threadStart) / windowFrames : 0.0;
		ALOGI("[vr-perf] fps=%.1f missed=%ld cpu_ms=%.2f thread_cpu_ms=%.2f wait_ms=%.2f acquire_ms=%.2f period_ms=%.2f frames=%ld rendered=%ld size=%d game=%s",
			  windowFrames / seconds, windowMissed, windowFrames ? cpuSum / windowFrames : 0.0, cpuPerFrame,
			  windowFrames ? waitSum / windowFrames : 0.0, windowFrames ? acquireSum / windowFrames : 0.0,
			  gAppState.FrameState.predictedDisplayPeriod / 1e6, frames, renderedFrames,
			  gAppState.Renderer.FrameBuffer[0].Width, connectionName());
		threadStart = threadMs;
		waitSum = acquireSum = 0.0;
		windowStart = now;
		windowFrames = 0;
		windowMissed = 0;
		cpuSum = 0.0;
	}

	// load a map without the menus, once the engine has been running a moment
	if (config.autostart && !mapStarted && renderedFrames >= 150)
	{
		char command[96];
		if (config.load[0])
			snprintf(command, sizeof(command), "load %s\n", config.load);
		else
			snprintf(command, sizeof(command), "map %s\n", config.map[0] ? config.map : "c1a0");
		Cbuf_AddText(command);
		mapStarted = true;
		mapStartFrame = renderedFrames;
		ALOGI("[vr-diag] autostart: %s", command);
	}

	// console commands once the map is up (cheats, a cvar, a give)
	if (config.cmd[0] && !cmdDone && mapStarted && renderedFrames >= mapStartFrame + 300)
	{
		char command[sizeof(config.cmd) + 2];
		snprintf(command, sizeof(command), "%s\n", config.cmd);
		Cbuf_AddText(command);
		cmdDone = true;
		ALOGI("[vr-diag] cmd: %s", config.cmd);
	}

	static double firstFrameMs = 0.0;
	if (firstFrameMs == 0.0)
		firstFrameMs = now;
	if (config.exitSeconds > 0 && (now - firstFrameMs) >= config.exitSeconds * 1000.0)
	{
		ALOGI("[vr-diag] exit_seconds %d reached, shutting down", config.exitSeconds);
		config.exitSeconds = 0;
		TBXR_exit(0);
	}

	if (config.exitAfter > 0 && frames >= config.exitAfter)
	{
		ALOGI("[vr-diag] exit_after %d reached, shutting down", config.exitAfter);
		config.exitAfter = 0;
		TBXR_exit(0);
	}
}

void VrDiag_FrameEnd(void)
{
	if (enabled && lastBegin != 0.0)
	{
		cpuSum += TBXR_GetTimeInMilliSeconds() - lastBegin;
	}
}
