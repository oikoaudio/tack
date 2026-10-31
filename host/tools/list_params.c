/*
 * list_params.c
 *
 * Prints the parameters a plugin exposes: number, range, init kit value and name.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include "lxr_host.h"
#include "lxr_params.h"

int main(void)
{
	uint8_t kit[LXR_HOST_NUM_PARAMS];
	lxr_host_initKit(kit);

	for(int i = 0; i < lxr_params_count(); i++)
	{
		const LxrParamInfo* p = lxr_params_info(i);
		const uint16_t def = lxr_params_fromFirmware(p->param, kit[p->param]);
		char text[64];
		lxr_params_valueText(p->param, def, text, sizeof text);
		printf("%3u  %3u..%-3u %s  %-50s = %s\n", p->param, p->min, p->max, p->isEnum ? "enum" : "    ", p->name, text);
	}
	printf("%d parameters\n", lxr_params_count());
	return 0;
}
