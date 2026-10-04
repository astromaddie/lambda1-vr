#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>

#include <common/common.h>

#include "VrDiag.h"
#include "VrDiagCore.h"
#include "VrCommon.h"

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
static bool dumpDone = false;
static bool dumpEyeDone[2];

// perf window
static double windowStart = 0.0;
static double lastBegin = 0.0;
static double cpuSum = 0.0;
static long windowFrames = 0;
static long windowMissed = 0;

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
	ALOGI("[vr-diag] %s: force_render=%d diag_yaw=%.1f autostart=%d map=%s dump_frame=%d exit_after=%d perf=%d oneshot=%d",
		  DIAG_FILE, config.forceRender, config.yaw, config.autostart, config.map[0] ? config.map : "-",
		  config.dumpFrame, config.exitAfter, config.perf, config.oneshot);

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
	pose->orientation = yawQuat(config.yaw);
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
	const float side = (hand == 0) ? -0.25f : 0.25f;
	const float rad = DEG2RAD(config.yaw);
	const float x = side;
	const float z = -0.3f;
	controller->Active = GL_TRUE;
	memset(&controller->Pose, 0, sizeof(controller->Pose));
	controller->Pose.orientation = yawQuat(config.yaw);
	controller->Pose.position.x = x * cosf(rad) + z * sinf(rad);
	controller->Pose.position.y = 1.2f;
	controller->Pose.position.z = -x * sinf(rad) + z * cosf(rad);
	memset(&controller->Velocity, 0, sizeof(controller->Velocity));
	controller->Velocity.type = XR_TYPE_SPACE_VELOCITY;
	return true;
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
	fprintf(file, "player_yaw %.2f\nsnap_turn %.2f\ndiag_yaw %.2f\nvr_fov_x %d\n", playerYaw, snapTurn, config.yaw, vrFOV);
	if (gAppState.Projections != NULL)
	{
		for (int eye = 0; eye < ovrMaxNumEyes; eye++)
		{
			const XrFovf* fov = &gAppState.Projections[eye].fov;
			fprintf(file, "fov_eye%d L %.3f R %.3f U %.3f D %.3f\n", eye, fov->angleLeft, fov->angleRight, fov->angleUp, fov->angleDown);
		}
	}
	fprintf(file, "screen_layer %d\n", VR_UseScreenLayer() ? 1 : 0);
	fclose(file);
}

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

	glFinish();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer->FrameBuffers[frameBuffer->TextureSwapChainIndex]);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	const GLenum error = glGetError();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	char path[256];
	snprintf(path, sizeof(path), DUMP_DIR "/vr-eye%d.bmp", eye);
	const bool ok = VrDiagCore_WriteBmp(path, pixels, width, height);
	free(pixels);

	ALOGI("[vr-dump] eye %d at rendered frame %ld: %dx%d, %s, gl error 0x%x", eye, renderedFrames, width, height,
		  ok ? path : "write failed", (unsigned int)error);

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
		ALOGI("[vr-perf] fps=%.1f missed=%ld cpu_ms=%.2f period_ms=%.2f frames=%ld rendered=%ld",
			  windowFrames / seconds, windowMissed, windowFrames ? cpuSum / windowFrames : 0.0,
			  gAppState.FrameState.predictedDisplayPeriod / 1e6, frames, renderedFrames);
		windowStart = now;
		windowFrames = 0;
		windowMissed = 0;
		cpuSum = 0.0;
	}

	// load a map without the menus, once the engine has been running a moment
	if (config.autostart && !mapStarted && renderedFrames >= 150)
	{
		char command[96];
		snprintf(command, sizeof(command), "map %s\n", config.map[0] ? config.map : "c1a0");
		Cbuf_AddText(command);
		mapStarted = true;
		ALOGI("[vr-diag] autostart: %s", command);
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
