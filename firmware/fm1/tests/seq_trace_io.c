/* SPDX-License-Identifier: GPL-3.0-only */
/* seq_trace's file output, apart from it: the FM-1 configuration's shim has no stdio. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int seq_trace_run(uint32_t rate);
static FILE *out;

void trace_scenario(const char *name) { fprintf(out, "S %s\n", name); }
void trace_event(uint32_t block, uint32_t ticks, char kind, int a, int b, int c)
{
    fprintf(out, "%u %u %c %d %d %d\n", block, ticks, kind, a, b, c);
}

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    out = fopen(argv[1], "w");
    if (!out)
        return 2;
    int r = seq_trace_run((uint32_t)atoi(argv[2]));
    fclose(out);
    return r;
}
