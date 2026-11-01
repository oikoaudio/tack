/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware's kit SysEx on stdin and stdout, for kits_page_test.mjs: the handler is app/kit_sysex.c itself, with RAM
 * for the flash. The simulator can't stand in, as its SysEx goes to the mirror and its plat_sysex_send is empty.
 *
 * A line of hex is one message from the computer, F0 to F7, and each reply comes back as a line of hex, then "." ends the
 * answer. The lines "playing 0|1", "erase_fails 0|1", "write_fails SLOT" and "bend SLOT" set up what goes wrong next. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* app.c's */
#define KIT_SLOT 256u
#define KIT_BASE 0xB8000u
#define KIT_SLOTS 128u
#define KIT_SECTORS 8u
#define KIT_PART 64u

static uint8_t flash[KIT_SLOTS * KIT_SLOT], kit_buf[KIT_SLOT];
static int playing, erase_fails, write_fails = -1, bend = -1;

static void mark(char c) { (void)c; }
static int tack_running(void) { return playing; }
static const uint8_t *plat_data(uint32_t off) { return flash + (off - KIT_BASE); }
static const uint8_t *kit_slot(uint32_t i)
{
    const uint8_t *p = plat_data(KIT_BASE + i * KIT_SLOT);
    return p[0] != 0xFFu ? p : 0;
}
static int plat_data_erase(uint32_t off)
{
    if (erase_fails)
        return -1;
    memset(flash + (off - KIT_BASE), 0xFF, 0x1000);
    return 0;
}
static int plat_data_write(uint32_t off, const void *src, uint32_t n)
{
    const uint32_t slot = (off - KIT_BASE) / KIT_SLOT;
    uint8_t *to = flash + (off - KIT_BASE);
    if ((int)slot == write_fails)
        return -1;
    for (uint32_t i = 0; i < n; i++)              /* as flash: bits only go from 1 to 0 */
        to[i] &= ((const uint8_t *)src)[i];
    if ((int)slot == bend)
        to[100] ^= 1u;
    return 0;
}
static void plat_sysex_send(const uint8_t *frame, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        printf("%02X", frame[i]);
    putchar('\n');
}

#include "../app/kit_sysex.c"

int main(void)
{
    static char line[4096];
    static uint8_t msg[2048];
    int v;
    memset(flash, 0xFF, sizeof flash);
    while (fgets(line, sizeof line, stdin)) {
        uint32_t n = 0;
        if (sscanf(line, "playing %d", &v) == 1)
            playing = v;
        else if (sscanf(line, "erase_fails %d", &v) == 1)
            erase_fails = v;
        else if (sscanf(line, "write_fails %d", &v) == 1)
            write_fails = v;
        else if (sscanf(line, "bend %d", &v) == 1)
            bend = v;
        else {
            for (const char *h = line; h[0] > ' ' && h[1] > ' ' && n < sizeof msg; h += 2) {
                unsigned b;
                sscanf(h, "%2x", &b);
                msg[n++] = (uint8_t)b;
            }
            if (n >= 3 && !app_sysex(msg + 1, n - 2))
                return 1;
        }
        puts(".");
        fflush(stdout);
    }
    return 0;
}
