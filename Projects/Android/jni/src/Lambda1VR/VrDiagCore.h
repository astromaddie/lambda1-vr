// Unattended test switches for the Steam Frame build, the parts that need no
// OpenXR or GL so they can be tested on the host (tools/steam_frame/tests).
#ifndef VRDIAGCORE_H
#define VRDIAGCORE_H

#include <stdbool.h>

typedef struct {
	int forceRender;	// draw and run the game while the runtime says shouldRender=false
	float yaw;			// synthetic head yaw in degrees (turning left is positive)
	int autostart;		// load a map without touching the menus
	char map[64];		// which one (a map set here starts it as well)
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
