// Unattended test switches for the Steam Frame build, the parts that need no
// OpenXR or GL so they can be tested on the host (tools/steam_frame/tests).
#ifndef VRDIAGCORE_H
#define VRDIAGCORE_H

#include <stdbool.h>

#define VRDIAG_MAX_PRESSES 24

typedef struct {
	int input;			// FI_* of VrFrameMap.h
	int start;			// frames after the map has started
	int length;			// frames held
} vrDiagPress_t;

typedef struct {
	int forceRender;	// draw and run the game while the runtime says shouldRender=false
	float yaw;			// synthetic head yaw in degrees (turning left is positive)
	float handYaw, handPitch;	// the synthetic hands' aim, turned from the head's (degrees)
	float gap;			// metres between the synthetic hands (they're parked either side of the chest)
	vrDiagPress_t presses[VRDIAG_MAX_PRESSES];	// buttons to hold down for a while
	int pressCount;
	float roll;			// synthetic head roll in degrees (tilting the head to the left is positive)
	int autostart;		// load a map without touching the menus
	char load[64];		// a save to load instead of starting a map (a load key implies autostart)
	int exitSeconds;	// leave after this many seconds of rendering, 0 = off
	char map[64];		// which one (a map set here starts it as well)
	char cmd[128];		// console commands (; between them) to run once, about 4 s after the map starts
	int dumpFrame;		// dump both eyes on this rendered frame, 0 = off
	int exitAfter;		// leave through the normal shutdown after this many frames, 0 = off
	int perf;			// the [vr-perf] line every 10 s
	int probe;			// clear each eye to a colour (red, blue) right before the dump, to test the readback
	int oneshot;		// rename the file once it has been read
	int keysSeen;		// how many known keys were in the file
} vrDiagConfig_t;

void VrDiagCore_Defaults(vrDiagConfig_t* config);

// "key = value" lines, # or ; for comments, keys in any case. Unknown keys and
// values that don't parse are ignored. Returns how many known keys it found.
int VrDiagCore_Parse(const char* text, vrDiagConfig_t* config);

// A 24-bit BMP from RGBA rows as glReadPixels gives them (bottom row first).
bool VrDiagCore_WriteBmp(const char* path, const unsigned char* rgba, int width, int height);

#endif
