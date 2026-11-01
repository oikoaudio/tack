/* SPDX-License-Identifier: GPL-3.0-only */
/* The keys' and buttons' lights. app.c includes this: it is one file to the compiler, and shares app.c's state */

/* A voice's key flashes as it fires: the others to lit, the selected one, lit already, to dark. Which of its keys */
static uint32_t flashing(uint32_t v, uint32_t now)
{
    static uint8_t seen[TACK_VOICES];
    static uint32_t at[TACK_VOICES];
    const uint8_t n = tack_voice_fires((uint8_t)v);
    if (n != seen[v]) {
        seen[v] = n;
        at[v] = now;
    }
    return at[v] && now - at[v] < FLASH_MS;
}

/* A white key as a step: 2 lit, 1 dim, 0 dark. Past the lane's end it is dark: a shorter lane keeps the steps there, and they don't play */
static int step_light(uint32_t k, int head)
{
    const uint32_t s = (uint32_t)ui.bar * 16u + k;
    const int on = tack_step_on((uint8_t)ui.voice, (uint8_t)s);
    if ((int)s >= tack_length((uint8_t)ui.voice))
        return 0;
    if (on && (int)s != head && tack_recur_skips((uint8_t)ui.voice, (uint8_t)s))
        return 1;                                /* not this pass */
    return on != ((int)s == head) ? 2 : 0;
}

/* the keys light the page's steps of the voice, the playhead dim-to-bright against them. The black keys: the voice lit, a
 * voice with steps in this pattern between that and the low glow of an empty lane, a muted one dark */
static void lights(void)
{
    uint32_t keys = 0, dim = 0, btn = 0, k;
    const int head = tack_running() ? tack_playhead((uint8_t)ui.voice) : -1;
    const uint32_t now = plat_ms();
    const int as_notes = keys_notes(ui.btn);
    const int as_slots = ui.held >= 0;
    uint32_t mid = 0;
    if (as_slots) {                              /* lit: it plays on that slot. Mid: a slot it skips */
        const uint8_t s = ui.held_step;
        const uint32_t len = (uint32_t)tack_recur_len((uint8_t)ui.voice, s), mask = tack_recur_mask((uint8_t)ui.voice, s);
        for (k = 0; k < len; k++) {
            if (mask >> k & 1u)
                keys |= 1u << BK[k];
            else
                mid |= 1u << BK[k];
        }
    }
    for (k = 0; k < TACK_VOICES; k++) {
        const uint32_t fire = flashing(k, now);
        if (as_notes || as_slots) {
            continue;
        } else if (ui.over == PAGE_FX) {
            if (tack_fx_send(k))
                keys |= 1u << BK[k];
        } else if ((int32_t)k == ui.voice) {
            if (!fire)
                keys |= 1u << BK[k];
        } else if (fire) {
            keys |= 1u << BK[k];
        } else if (!tack_muted((uint8_t)k)) {
            if (tack_lane_used((uint8_t)k))
                mid |= 1u << BK[k];
            else
                dim |= 1u << BK[k];
        }
    }
    if (as_notes) {                              /* the step's note on the keys: the one playing, or the one to write */
        const uint32_t s = head >= 0 ? (uint32_t)head : note_cursor();
        dim = (1u << NKEYS) - 1u;
        if (tack_step_on((uint8_t)ui.voice, (uint8_t)s))
            for (k = 0; k < NKEYS; k++)
                if ((int)key_note(k) == tack_step_get((uint8_t)ui.voice, (uint8_t)s, TACK_ST_NOTE))
                    keys |= 1u << k;
    } else if (ui.over == PAGE_SAVE && ui.ks_what == SAVE_PATTERN) {   /* the slots, as SEQ held has them */
        for (k = 0; k < 16u; k++) {
            if (k == ui.ks_pat)
                keys |= 1u << WK[k];
            else
                dim |= 1u << WK[k];
        }
    } else if (sel_length()) {                   /* the lane at mid, so it is not taken for its steps, and its last step lit */
        for (k = 0; k < 16u; k++) {
            if (sel_light(k) == 2)
                keys |= 1u << WK[k];
            else if (sel_light(k))
                mid |= 1u << WK[k];
        }
    } else if ((ui.btn >> B_SEQ & 1u) || (ui.keys >> BK[KEY_COPY] & 1u) || ui.over == PAGE_SONG) {   /* the keys are the patterns */
        for (k = 0; k < 16u; k++) {
            if ((int)k == tack_pattern())
                keys |= 1u << WK[k];
            else
                dim |= 1u << WK[k];
        }
    } else {
        for (k = 0; k < 16u; k++) {
            const int light = step_light(k, head);
            if (light == 2)
                keys |= 1u << WK[k];
            else if (light)
                dim |= 1u << WK[k];
        }
    }
    if (seq_in_view())
        btn |= 1u << (ui.voice_view ? B_SEQ : B_HOME);
    if (ui.over == PAGE_FX)
        btn |= 1u << B_FX;
    else if (ui.group == TACK_G_EDIT)
        btn |= 1u << B_EDIT;
    else if (ui.group == TACK_G_ENV)
        btn |= 1u << B_ENV;
    else if (ui.group == TACK_G_LFO)
        btn |= 1u << B_LFO;
    if (tack_recording())
        btn |= 1u << B_REC;
    if (ui.over == PAGE_SET)
        btn |= 1u << B_GLO;
    if (arp_page() || ui.over == PAGE_SONG)
        btn |= 1u << (arp_page() ? B_ARP : B_SEQ);
    if (ui.over == PAGE_SAVE)
        btn |= 1u << B_SAVE;
    if (tack_running())
        btn |= 1u << B_PLAY | LED_PLAY_GREEN;
    plat_lights(btn, keys, 0, dim, 1);
    plat_lights_mid((1u << NB) - 1u, mid);               /* the unlit buttons: the glow is too low to read a label by */
}

void app_input_only(void) { input(); }
