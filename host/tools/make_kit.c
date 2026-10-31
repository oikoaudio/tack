/*
 * make_kit.c
 *
 * Writes a kit file in the device's format: the init kit with some parameters changed (firmware values, as
 * list_params numbers them). For tests that need kits, since none can be bundled.
 *
 *   make_kit out.snd NAME [param=value ...]
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lxr_host.h"

int main(int argc, char** argv)
{
	if(argc < 3) {
		fprintf(stderr, "usage: %s out.snd NAME [param=value ...]\n", argv[0]);
		return 1;
	}
	uint8_t kit[LXR_HOST_NUM_PARAMS];
	lxr_host_initKit(kit);
	for(int i = 3; i < argc; i++) {
		const char* eq = strchr(argv[i], '=');
		const int param = atoi(argv[i]), value = eq ? atoi(eq + 1) : -1;
		if(!eq || param < 0 || param >= LXR_HOST_NUM_PARAMS || value < 0 || value > 255) {
			fprintf(stderr, "bad parameter %s\n", argv[i]);
			return 1;
		}
		kit[param] = (uint8_t)value;
	}
	if(lxr_host_writeKitFile(argv[1], kit, argv[2])) {
		perror(argv[1]);
		return 1;
	}
	return 0;
}
