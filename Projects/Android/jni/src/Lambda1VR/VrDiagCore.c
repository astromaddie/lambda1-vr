#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "VrDiagCore.h"

void VrDiagCore_Defaults(vrDiagConfig_t* config)
{
	memset(config, 0, sizeof(*config));
	config->perf = 1;
}

static char* trim(char* text)
{
	while (isspace((unsigned char)*text))
	{
		text++;
	}
	char* end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1]))
	{
		*--end = '\0';
	}
	return text;
}

static bool parseInt(const char* value, int* out)
{
	if (strcasecmp(value, "true") == 0 || strcasecmp(value, "on") == 0 || strcasecmp(value, "yes") == 0)
	{
		*out = 1;
		return true;
	}
	if (strcasecmp(value, "false") == 0 || strcasecmp(value, "off") == 0 || strcasecmp(value, "no") == 0)
	{
		*out = 0;
		return true;
	}
	char* end = NULL;
	long number = strtol(value, &end, 10);
	if (end == value || *end != '\0')
	{
		return false;
	}
	*out = (int)number;
	return true;
}

static bool parseFloat(const char* value, float* out)
{
	char* end = NULL;
	float number = strtof(value, &end);
	if (end == value || *end != '\0')
	{
		return false;
	}
	*out = number;
	return true;
}

int VrDiagCore_Parse(const char* text, vrDiagConfig_t* config)
{
	int found = 0;
	char* copy = strdup(text);
	if (copy == NULL)
	{
		return 0;
	}

	for (char* line = strtok(copy, "\r\n"); line != NULL; line = strtok(NULL, "\r\n"))
	{
		line = trim(line);
		if (*line == '\0' || *line == '#' || *line == ';')
		{
			continue;
		}

		char* equals = strchr(line, '=');
		if (equals == NULL)
		{
			continue;
		}
		*equals = '\0';
		const char* key = trim(line);
		const char* value = trim(equals + 1);

		int number = 0;
		float real = 0.0f;
		if (strcasecmp(key, "force_render") == 0 && parseInt(value, &number))
		{
			config->forceRender = number != 0;
		}
		else if (strcasecmp(key, "diag_yaw") == 0 && parseFloat(value, &real))
		{
			config->yaw = real;
		}
		else if (strcasecmp(key, "autostart") == 0 && parseInt(value, &number))
		{
			config->autostart = number != 0;
		}
		else if (strcasecmp(key, "map") == 0 && *value != '\0')
		{
			snprintf(config->map, sizeof(config->map), "%s", value);
			config->autostart = 1;
		}
		else if (strcasecmp(key, "dump_frame") == 0 && parseInt(value, &number))
		{
			config->dumpFrame = number > 0 ? number : 0;
		}
		else if (strcasecmp(key, "exit_after") == 0 && parseInt(value, &number))
		{
			config->exitAfter = number > 0 ? number : 0;
		}
		else if (strcasecmp(key, "probe") == 0 && parseInt(value, &number))
		{
			config->probe = number != 0;
		}
		else if (strcasecmp(key, "perf") == 0 && parseInt(value, &number))
		{
			config->perf = number != 0;
		}
		else if (strcasecmp(key, "oneshot") == 0 && parseInt(value, &number))
		{
			config->oneshot = number != 0;
		}
		else
		{
			continue;
		}
		found++;
	}

	free(copy);
	config->keysSeen = found;
	return found;
}

static void put16(unsigned char* p, unsigned int v) { p[0] = v & 255; p[1] = (v >> 8) & 255; }
static void put32(unsigned char* p, unsigned int v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

bool VrDiagCore_WriteBmp(const char* path, const unsigned char* rgba, int width, int height)
{
	if (width <= 0 || height <= 0)
	{
		return false;
	}

	const int stride = (width * 3 + 3) & ~3;
	const unsigned int imageSize = (unsigned int)stride * (unsigned int)height;

	unsigned char header[54];
	memset(header, 0, sizeof(header));
	header[0] = 'B';
	header[1] = 'M';
	put32(header + 2, 54 + imageSize);
	put32(header + 10, 54);
	put32(header + 14, 40);
	put32(header + 18, (unsigned int)width);
	put32(header + 22, (unsigned int)height);	// positive: rows are bottom to top, as given
	put16(header + 26, 1);
	put16(header + 28, 24);
	put32(header + 34, imageSize);

	FILE* file = fopen(path, "wb");
	if (file == NULL)
	{
		return false;
	}

	bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
	unsigned char* row = (unsigned char*)calloc(1, (size_t)stride);
	if (row == NULL)
	{
		fclose(file);
		return false;
	}
	for (int y = 0; y < height && ok; y++)
	{
		const unsigned char* src = rgba + (size_t)y * (size_t)width * 4;
		for (int x = 0; x < width; x++)
		{
			row[x * 3 + 0] = src[x * 4 + 2];
			row[x * 3 + 1] = src[x * 4 + 1];
			row[x * 3 + 2] = src[x * 4 + 0];
		}
		ok = fwrite(row, 1, (size_t)stride, file) == (size_t)stride;
	}
	free(row);
	return fclose(file) == 0 && ok;
}
