/* SPDX-License-Identifier: GPL-3.0-only */
/* The kit bank over SysEx, from tools/fm1_kits.py. app.c includes this: it is one file to the compiler, and shares app.c's state */

/* kits over USB: F0 7D 'K' 'T' cmd ... F7. 1 erases the bank; 2 slot hex..., one slot as hex, in the main loop. Both
 * refuse while the sequencer runs, because a flash write stops the sound */
static int hex_val(uint8_t c) { return c <= '9' ? c - '0' : c - 'A' + 10; }

static void kit_reply(uint8_t cmd, uint8_t status)
{
    const uint8_t f[] = {0xF0, 0x7D, 'K', 'T', 0x7F, cmd, status, 0xF7};
    plat_sysex_send(f, sizeof f);
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (n < 4u || p[0] != 0x7Du || p[1] != 'K' || p[2] != 'T')
        return 0;
    mark('U');
    if (p[3] == 3u && n == 6u && !tack_running()) {     /* 3 slot part: that much of a slot, as hex */
        static const char HEX[] = "0123456789ABCDEF";
        const uint8_t *k = p[4] < KIT_SLOTS ? kit_slot(p[4]) : 0;
        uint8_t f[9 + 2u * KIT_PART + 1u] = {0xF0, 0x7D, 'K', 'T', 0x7F, 3, k ? 0 : 4, p[4], p[5]};
        uint32_t len = 9;
        if (k && p[5] < KIT_SLOT / KIT_PART)
            for (i = 0; i < KIT_PART; i++) {
                const uint8_t b = k[p[5] * KIT_PART + i];
                f[len++] = (uint8_t)HEX[b >> 4];
                f[len++] = (uint8_t)HEX[b & 15u];
            }
        f[len++] = 0xF7;
        plat_sysex_send(f, len);
        return 1;
    }
    if (tack_running()) {
        kit_reply(p[3], 1);
    } else if (p[3] == 1u) {
        int bad = 0;
        for (i = 0; i < KIT_SECTORS; i++)
            bad |= plat_data_erase(KIT_BASE + i * 0x1000u);
        kit_reply(1, bad ? 2 : 0);
    } else if (p[3] == 2u && n == 5u + 2u * KIT_SLOT && p[4] < KIT_SLOTS) {
        for (i = 0; i < KIT_SLOT; i++)
            kit_buf[i] = (uint8_t)(hex_val(p[5 + 2 * i]) << 4 | hex_val(p[6 + 2 * i]));
        kit_reply(2, plat_data_write(KIT_BASE + p[4] * KIT_SLOT, kit_buf, KIT_SLOT) ? 2 : 0);
    } else {
        kit_reply(p[3], 3);
    }
    return 1;
}

#define FLASH_MS 50u
