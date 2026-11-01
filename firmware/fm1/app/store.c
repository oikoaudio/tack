/* SPDX-License-Identifier: GPL-3.0-only */
/* Saving: what SAVE writes, a pattern at a time, and what a backup holds. app.c includes this: it is one file to the compiler, and shares app.c's state */

static int save_settings(void)
{
    set.magic = SET_MAGIC;
    set.bpm = tack_bpm();
    set.kit_now = (uint8_t)tack_kit_now();
    memcpy(set.kit, tack_kit(), tack_kit_size());
    memcpy(set.kit + SET_KIT_NAME, ui.kit_name, 8);
    memcpy(set.song, tack_song(), sizeof set.song);
    set.song_on = tack_song_mode() == TACK_SONG_ON;
    set.keys_quiet = ui.keys_quiet;
    return plat_store_save(OBJ_SETTINGS, &set, sizeof set);
}

/* SAVE: the flash stops everything for a moment, sound too, so never while the sequencer runs. Every changed pattern, and
 * the active one so that SAVE always does something: one a frame (save_frame), as each is an erase and a write */
static void save(void)
{
    if (tack_running()) {
        app_say("STOP FIRST", "");
        return;
    }
    if (ui.save_hold || ui.save_todo)
        return;
    mark('S');
    plat_tick_gap();                     /* what this save held the tick up by: the USB AUDIO row */
    if (save_settings() != 0) {
        app_say("SAVE", "FAILED");
        return;
    }
    ui.save_todo = tack_dirty() | 1u << tack_pattern();
    ui.saved = 0;
}

static int save_one(uint32_t i)
{
    tack_clean((uint8_t)i);                    /* first: an edit landing during the write marks it again, and the next SAVE takes it */
    if (plat_store_save(OBJ_PAT0 + i, pat_buf, tack_pattern_export((uint8_t)i, pat_buf)) != 0) {
        tack_mark_dirty((uint8_t)i);
        return -1;
    }
    return 0;
}

static void save_frame(void)
{
    uint32_t i;
    char t[16];
    if (ui.save_as) {                            /* the copy and the change are in, or the sequencer took over */
        const uint32_t to = ui.save_as - 1u;
        if (tack_running()) {
            ui.save_as = 0;
            app_say("SAVE", "STOPPED");
        } else if ((uint32_t)tack_pattern() == to && (tack_dirty() >> to & 1u)) {
            ui.save_as = 0;
            ui.saved = 0;
            ui.save_todo = save_settings() ? 0u : 1u << to;
        }
    }
    if (!ui.save_todo)
        return;
    if (tack_running()) {              /* started from MIDI: what is left stays changed, for the next SAVE */
        ui.save_todo = 0;
        app_say("SAVE", "STOPPED");
        return;
    }
    for (i = 0; !(ui.save_todo >> i & 1u); i++)
        ;
    if (save_one(i) != 0) {
        ui.save_todo = 0;
        app_say("SAVE", "FAILED");
        return;
    }
    ui.save_todo &= ~(1u << i);
    ui.saved++;
    mark('S');
    if (!ui.save_todo) {
        const uint32_t g = plat_tick_gap();
        ui.save_gap = g > ui.save_gap ? g : ui.save_gap;
        if (ui.saved == 1u) {
            int_text((int32_t)i + 1, text_put(t, "PATTERN "));
        } else {
            text_put(int_put(t, ui.saved), " PATTERNS");
        }
        app_say("SAVED", t);
    }
}

/* a backup or restore of the patterns and settings: fm1-base's device/backup.c */
int app_backup_playing(void) { return tack_running(); }
void app_backup_hold(int on) { ui.save_hold = (uint8_t)on; }
void app_backup_flush(void)              /* the backup holds what is saved: so save, the kit and tempo too */
{
    uint32_t i, todo = tack_dirty();
    int bad = save_settings();
    for (i = 0; i < TACK_PATTERNS; i++)
        if (todo >> i & 1u)
            bad |= save_one(i);
    if (bad)
        app_say("SAVE", "FAILED");
}
void app_backup_restored(void)
{
    ui.bad_patterns = 0;
    load_saved(1);
    if (ui.bad_patterns)                         /* (fm1 restore checks first: this is for another way in) */
        app_say("PATTERNS", "REFUSED");
}
