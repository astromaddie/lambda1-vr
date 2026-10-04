// Host test for the vr-diag.ini parser and the BMP writer (VrDiagCore.c).
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "VrDiagCore.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main(void)
{
	vrDiagConfig_t c;

	// nothing in the file: everything off, the perf line on
	VrDiagCore_Defaults(&c);
	CHECK(VrDiagCore_Parse("", &c) == 0);
	CHECK(!c.forceRender && !c.autostart && c.dumpFrame == 0 && c.exitAfter == 0 && c.perf == 1 && !c.oneshot);

	// a normal file, with spaces, other case, comments, CRLF
	VrDiagCore_Defaults(&c);
	const char* text =
		"# an unattended run\r\n"
		"force_render = 1\r\n"
		"  DIAG_YAW=90.5  \r\n"
		"; another comment\n"
		"autostart = on\n"
		"dump_frame = 600\n"
		"exit_after=1800\n"
		"perf = 0\n"
		"probe = 1\n"
		"cmd = sv_cheats 1; impulse 101\n"
		"oneshot = true\n";
	CHECK(VrDiagCore_Parse(text, &c) == 9);
	CHECK(c.probe == 1);
	CHECK(strcmp(c.cmd, "sv_cheats 1; impulse 101") == 0);
	CHECK(c.forceRender == 1);
	CHECK(fabsf(c.yaw - 90.5f) < 0.001f);
	CHECK(c.autostart == 1 && c.map[0] == '\0');
	CHECK(c.dumpFrame == 600 && c.exitAfter == 1800);
	CHECK(c.perf == 0 && c.oneshot == 1);

	// a map starts the game by itself
	VrDiagCore_Defaults(&c);
	CHECK(VrDiagCore_Parse("map = c1a0\n", &c) == 1);
	CHECK(c.autostart == 1 && strcmp(c.map, "c1a0") == 0);

	// junk is ignored, a bad value leaves the default, negatives don't turn things on
	VrDiagCore_Defaults(&c);
	CHECK(VrDiagCore_Parse("bogus = 1\nforce_render = maybe\nno equals sign\ndump_frame = -5\nexit_after = abc\n= 3\n", &c) == 1);
	CHECK(c.forceRender == 0 && c.dumpFrame == 0 && c.exitAfter == 0);

	// a long map name is cut, not overrun
	VrDiagCore_Defaults(&c);
	char longName[200];
	memset(longName, 'a', sizeof(longName));
	strcpy(longName + 150, "\n");
	char line[300];
	snprintf(line, sizeof(line), "map = %s", longName);
	VrDiagCore_Parse(line, &c);
	CHECK(strlen(c.map) == sizeof(c.map) - 1);

	// the BMP: 2x2, rows bottom to top, RGBA in, BGR out, rows padded to 4 bytes
	const unsigned char rgba[2 * 2 * 4] = {
		10, 20, 30, 255,   40, 50, 60, 255,		// bottom row
		70, 80, 90, 255,   100, 110, 120, 255,	// top row
	};
	char path[] = "/tmp/vrdiag-test.bmp";
	CHECK(VrDiagCore_WriteBmp(path, rgba, 2, 2));
	FILE* f = fopen(path, "rb");
	CHECK(f != NULL);
	if (f)
	{
		unsigned char b[70];
		size_t n = fread(b, 1, sizeof(b), f);
		fclose(f);
		CHECK(n == 54 + 16);
		CHECK(b[0] == 'B' && b[1] == 'M');
		CHECK(b[2] == 70 && b[10] == 54 && b[14] == 40);
		CHECK(b[18] == 2 && b[22] == 2 && b[26] == 1 && b[28] == 24);
		CHECK(b[54] == 30 && b[55] == 20 && b[56] == 10);			// first pixel, bottom left, as BGR
		CHECK(b[57] == 60 && b[58] == 50 && b[59] == 40);
		CHECK(b[60] == 0 && b[61] == 0);							// row padding
		CHECK(b[62] == 90 && b[63] == 80 && b[64] == 70);			// next row
	}
	remove(path);
	CHECK(!VrDiagCore_WriteBmp("/tmp/no-such-dir/x.bmp", rgba, 2, 2));
	CHECK(!VrDiagCore_WriteBmp(path, rgba, 0, 2));

	printf(failures ? "diag core: FAILED (%d)\n" : "diag core: ok\n", failures);
	return failures ? 1 : 0;
}
