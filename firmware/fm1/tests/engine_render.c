/* SPDX-License-Identifier: GPL-3.0-only */
/* Renders a fixed scenario through the engine, for compare_audio.sh: each voice in turn, then the kit together.
 * Built several ways (desktop arithmetic, the FM-1's, each sample rate) from the same scenario, so a difference
 * between two renders comes from the build alone. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KIT_BUFFER 512                 /* more than any kit: lxr_host_loadKit reads END_OF_SOUND_PARAMETERS bytes */
#define BLOCK 64
void lxr_host_init(void);
void lxr_host_loadKit(const uint8_t *values);
void lxr_host_noteOn(uint8_t voice, uint8_t note, uint8_t vel);
void lxr_host_setParam(uint16_t param, uint8_t value);
void lxr_host_renderBlock(int16_t *dac1, int16_t *dac2);

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

/* usage: out.wav rate-written-in-header [kit.snd] [param=value ...]
 * ENGINE_RENDER_ONLY=v: voice v alone, at the very start: nothing free-running has had time to drift
 * ENGINE_RENDER_LOAD=n: instead n blocks with every voice retriggered each 35, as the firmware's load test; 0 none */
int main(int argc, char **argv)
{
    if (argc < 3)
        return 1;
    const uint32_t rate = (uint32_t)atoi(argv[2]);
    const uint32_t blocks_per_step = 1200, nvoices = 7;      /* 1.74 s a step */
    /* one voice needs one step: a sweep of every kit writes thousands of these */
    const uint32_t total = getenv("ENGINE_RENDER_ONLY") ? blocks_per_step : blocks_per_step * (nvoices + 2);
    int16_t *pcm = calloc(total * BLOCK * 2, 2);

    lxr_host_init();
    if (argc > 3 && strcmp(argv[3], "-")) {
        uint8_t kit[KIT_BUFFER] = {0};
        char name[8];
        FILE *f = fopen(argv[3], "rb");
        if (!f || fread(name, 1, 8, f) != 8)
            return 2;
        fread(kit, 1, KIT_BUFFER, f);
        fclose(f);
        lxr_host_loadKit(kit);
    }
    for (int i = 4; i < argc; i++) {
        int p, v;
        if (sscanf(argv[i], "%d=%d", &p, &v) == 2)
            lxr_host_setParam((uint16_t)p, (uint8_t)v);
    }
    const char *only_s = getenv("ENGINE_RENDER_ONLY");
    const int only = only_s ? atoi(only_s) : -1;
    const char *load_s = getenv("ENGINE_RENDER_LOAD");
    if (load_s) {
        const uint32_t n = (uint32_t)atoi(load_s);
        const int idle = getenv("ENGINE_RENDER_IDLE") != NULL;
        volatile int32_t sink = 0;
        for (uint32_t b = 0; b < n; b++) {
            int16_t d1[BLOCK * 2], d2[BLOCK * 2];
            if (!idle && b % 35u == 0u)
                for (uint8_t v = 0; v < nvoices; v++)
                    lxr_host_noteOn(v, 60, 127);
            lxr_host_renderBlock(d1, d2);
            sink += d1[0];
        }
        return 0;
    }
    for (uint32_t b = 0; b < total; b++) {
        int16_t d1[BLOCK * 2], d2[BLOCK * 2];
        if (only >= 0) {
            if (b == 0)
                lxr_host_noteOn((uint8_t)only, 60, 127);
        } else if (b % blocks_per_step == 0) {
            const uint32_t step = b / blocks_per_step;
            if (step < nvoices)
                lxr_host_noteOn((uint8_t)step, 60, 127);
            else if (step == nvoices)
                for (uint32_t v = 0; v < 6; v++)
                    lxr_host_noteOn((uint8_t)v, 60, 127);
        }
        lxr_host_renderBlock(d1, d2);
        for (int i = 0; i < BLOCK * 2; i++) {
            int32_t s = (int32_t)d1[i] + d2[i];
            pcm[b * BLOCK * 2 + i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
    }
    FILE *f = fopen(argv[1], "wb");
    const uint32_t bytes = total * BLOCK * 4;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 2);
    put32(f, rate); put32(f, rate * 4); put16(f, 4); put16(f, 16); fwrite("data", 1, 4, f); put32(f, bytes);
    fwrite(pcm, 2, total * BLOCK * 2, f);
    fclose(f);
    return 0;
}
