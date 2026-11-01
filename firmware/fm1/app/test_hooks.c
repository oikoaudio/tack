/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's expect command. app.c includes this: it is one file to the compiler, and shares app.c's state */

#ifdef PLAT_HOST
uint8_t tack_heard(uint16_t param);
int tack_morph_kept(uint16_t param);
int tack_morph_differs(void);
int tack_mutate_moved(int outside);

/* word, then a number if any: heldval0, step12. A misspelt name must fail as unknown, not read as a shorter one */
static int numbered(const char *what, const char *word)
{
    while (*word)
        if (*what++ != *word++)
            return 0;
    while (*what >= '0' && *what <= '9')
        what++;
    return !*what;
}

int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "screen"))                 /* the page showing, for tests/pages.py */
        *got = (ui.over == PAGE_FX ? 6 : ui.over == PAGE_SET ? 7 : ui.over) * 1000 + ui.group * 100 + ui.sub * 10 + (ui.voice_view ? 1 + (ui.note_mode != 0) : 0);
    else if (!strcmp(what, "held"))
        *got = ui.held;
    else if (!strcmp(what, "heldused"))
        *got = ui.held_used;
    else if (numbered(what, "heldval"))
        *got = ui.held_val[atoi(what + 7)];
    else if (!strncmp(what, "slotlock", 8)) {    /* slotlockS_K: what step S locks slot K of the page to, -1 for nothing */
        uint8_t val = 0;
        const TackSlot *sl = slot((uint32_t)atoi(strchr(what, '_') + 1));
        *got = sl && tack_lock_get((uint8_t)ui.voice, (uint8_t)atoi(what + 8), sl->par, &val) ? val : -1;
    }
    else if (!strcmp(what, "rollon"))
        *got = ui.roll_on;
    else if (!strcmp(what, "row"))
        *got = row_now();
    else if (!strcmp(what, "voice"))
        *got = ui.voice;
    else if (!strcmp(what, "setpage"))
        *got = ui.over == PAGE_SET;
    else if (numbered(what, "fires"))     /* firesN: voice N's triggers */
        *got = tack_voice_fires((uint8_t)atoi(what + 5));
    else if (numbered(what, "used"))      /* usedN: voice N has steps here */
        *got = tack_lane_used((uint8_t)atoi(what + 4));
    else if (!strcmp(what, "tall"))
        *got = full_page();
    else if (!strcmp(what, "badpatterns"))
        *got = ui.bad_patterns;
    else if (!strcmp(what, "lockcount"))
        *got = tack_lock_count();
    else if (numbered(what, "recurlen"))  /* recurlenN, recurmaskN: the selected voice's step N */
        *got = tack_recur_len((uint8_t)ui.voice, (uint8_t)atoi(what + 8));
    else if (numbered(what, "recurmask"))
        *got = tack_recur_mask((uint8_t)ui.voice, (uint8_t)atoi(what + 9));
    else if (!strcmp(what, "noteview"))
        *got = note_view();
    else if (!strcmp(what, "row"))
        *got = row_now();
    else if (!strcmp(what, "touched"))
        *got = ui.touched_until != 0;
    else if (!strcmp(what, "popup"))
        *got = ui.popup;
    else if (!strcmp(what, "voiceview"))
        *got = voice_page();
    else if (!strcmp(what, "selstep"))       /* the bars view's selected step */
        *got = (int)note_cursor();
    else if (numbered(what, "selslot"))   /* selslotN: its slot N */
        *got = tack_step_get((uint8_t)ui.voice, (uint8_t)note_cursor(), (uint32_t)atoi(what + 7));
    else if (numbered(what, "vel") && what[3] >= '0' && what[3] <= '9')   /* velN: the selected voice's step N */
        *got = tack_step_get((uint8_t)ui.voice, (uint8_t)atoi(what + 3), TACK_ST_VEL);
    else if (!strcmp(what, "homenext"))
        *got = tack_pattern_next();
    else if (!strcmp(what, "velpage"))
        *got = vel_page();
    else if (numbered(what, "skips"))     /* skipsN: the selected voice's step N rests this pass */
        *got = tack_recur_skips((uint8_t)ui.voice, (uint8_t)atoi(what + 5));
    else if (!strcmp(what, "usb10"))
        *got = ui.usb10;
    else if (!strcmp(what, "songpage"))
        *got = song_pages() ? ui.over : 0;
    else if (!strcmp(what, "songmode"))
        *got = tack_song_mode();
    else if (!strcmp(what, "songpos"))
        *got = tack_song_pos();
    else if (!strcmp(what, "songedit"))
        *got = ui.song_row;
    else if (!strcmp(what, "songlen"))
        *got = (int)song_len();
    else if (numbered(what, "songrow"))   /* songrowN: what the row holds */
        *got = tack_song_get((uint32_t)atoi(what + 7));
    else if (!strcmp(what, "fxsends")) {
        *got = 0;
        for (int v = 0; v < TACK_FX_SENDS; v++)
            *got |= tack_fx_send((uint32_t)v) << v;
    } else if (!strcmp(what, "fxpage"))
        *got = ui.over == PAGE_FX;
    else if (!strcmp(what, "crush"))
        *got = tack_get_param(tack_rate_param());
    else if (!strcmp(what, "savepage"))
        *got = ui.over == PAGE_SAVE;
    else if (!strcmp(what, "savewhat"))
        *got = ui.ks_what;
    else if (!strcmp(what, "savepat"))
        *got = ui.ks_pat;
    else if (!strcmp(what, "kitpage"))
        *got = ui.over == PAGE_SAVE && ui.ks_what != SAVE_PATTERN;
    else if (!strcmp(what, "kitmorph"))
        *got = ui.ks_morph;
    else if (!strcmp(what, "kitslot"))
        *got = ui.ks_slot;
    else if (!strcmp(what, "kitempty"))       /* the kit page's name has no letters */
        *got = save_name_empty();
    else if (numbered(what, "kitletter"))  /* kitletterN: the kit page's name */
        *got = ui.ks_name[atoi(what + 9)];
    else if (numbered(what, "kitname"))    /* kitnameN: the name of what is playing */
        *got = ui.kit_name[atoi(what + 7)];
    else if (numbered(what, "bankname"))   /* bankname0: the first letter in that slot, 0 empty */
        *got = kit_slot((uint32_t)atoi(what + 8)) ? kit_slot((uint32_t)atoi(what + 8))[0] : 0;
    else if (!strcmp(what, "morph"))
        *got = tack_morph();
    else if (!strcmp(what, "morphkit"))
        *got = tack_morph_kit();
    else if (!strcmp(what, "morphdiff"))     /* the parameters heard that are not the kit's */
        *got = tack_morph_differs();
    else if (!strcmp(what, "lanekey"))
        *got = lane_key_held();
    else if (!strcmp(what, "selhelp"))
        *got = ui.help_on;
    else if (!strcmp(what, "mutmoved"))
        *got = tack_mutate_moved(0);
    else if (!strcmp(what, "mutwide"))        /* a step moves many parameters; how many depends on when the turn came */
        *got = tack_mutate_moved(0) >= 20;
    else if (!strcmp(what, "mutoutside"))
        *got = tack_mutate_moved(1);
    else if (numbered(what, "heard"))     /* heardN: parameter N as the voices have it */
        *got = tack_heard((uint16_t)atoi(what + 5));
    else if (numbered(what, "kept"))      /* keptN: morph leaves parameter N alone */
        *got = tack_morph_kept((uint16_t)atoi(what + 4));
    else if (numbered(what, "kitv"))      /* kitvN: parameter N as the kit has it */
        *got = tack_get_param((uint16_t)atoi(what + 4));
    else if (!strcmp(what, "bits"))
        *got = tack_bits();
    else if (!strcmp(what, "bitslabel"))
        *got = 2 + (tack_bits() * 14 + 63) / 127;
    else if (!strcmp(what, "lanepage"))
        *got = ui.over == PAGE_LANE;
    else if (!strcmp(what, "eupage"))
        *got = ui.over == PAGE_EUCLID;
    else if (!strcmp(what, "lanedir"))
        *got = (int)tack_lane_dir((uint8_t)ui.voice);
    else if (!strcmp(what, "lanestart"))
        *got = tack_lane_start((uint8_t)ui.voice);
    else if (!strcmp(what, "lanespeed"))
        *got = (int)tack_lane_speed((uint8_t)ui.voice);
    else if (!strcmp(what, "head"))          /* the selected lane's step */
        *got = tack_playhead((uint8_t)ui.voice);
    else if (!strcmp(what, "patused"))
        *got = (int)tack_patterns_used();
    else if (!strcmp(what, "keyword"))       /* 1 ROLL, 2 COPY TO */
        *got = !key_word() ? 0 : key_word()[0] == 'R' ? 1 : key_word()[0] == 'C' ? 2 : key_word()[0] == 'L' ? 4 : 3;
    else if (numbered(what, "msgkit"))     /* the first letter of the kit the top bar is naming */
        *got = ui.msg_until && !strncmp(ui.msg, "KIT ", 4) ? ui.msg[4] : 0;
    else if (!strcmp(what, "msgext"))         /* the top bar is saying the clock is external */
        *got = ui.msg_until && !strncmp(ui.msg, "EXT", 3);
    else if (numbered(what, "steplight"))  /* steplightN: white key N as a step, stopped */
        *got = step_light((uint32_t)atoi(what + 9), -1);
    else if (numbered(what, "sellight"))   /* sellightN: white key N with SEL held */
        *got = sel_light((uint32_t)atoi(what + 8));
    else if (numbered(what, "st_"))   /* st_N: step 0's slot N on the selected voice */
        *got = tack_step_get((uint8_t)ui.voice, 0, (uint32_t)atoi(what + 3));
    else if (!strcmp(what, "lanenudge"))
        *got = tack_lane_nudge_get((uint8_t)ui.voice);
    else if (!strcmp(what, "roll"))
        *got = tack_roll_rate();
    else if (numbered(what, "eu"))        /* euN: the selected voice's Euclidean setting N */
        *got = tack_euclid_get((uint8_t)ui.voice, (uint32_t)atoi(what + 2));
    else if (!strcmp(what, "page"))          /* group * 10 + page */
        *got = ui.group * 10 + ui.sub;
    else if (numbered(what, "side"))      /* sideN: how far the open page's slot N is from its middle */
        *got = slot((uint32_t)atoi(what + 4)) ? slot_side(slot((uint32_t)atoi(what + 4)), tack_get_param(slot((uint32_t)atoi(what + 4))->par)) : -999;
    else if (numbered(what, "sl"))        /* slN: the value of the open page's slot N */
        *got = slot((uint32_t)atoi(what + 2)) ? tack_get_param(slot((uint32_t)atoi(what + 2))->par) : -1;
    else if (!strcmp(what, "echo"))
        *got = tack_fx_delay_ms();
    else if (!strcmp(what, "stress"))
        *got = ui.stress;
    else if (!strcmp(what, "dirty"))
        *got = (int)tack_dirty();
    else if (!strcmp(what, "saved"))
        *got = ui.saved;
    else if (numbered(what, "fxv"))       /* fxvN: the FX page's slot N */
        *got = tack_get_param(tack_fx_param((uint32_t)atoi(what + 3)));
    else if (numbered(what, "fxp"))       /* fxpN: the parameter behind it */
        *got = tack_fx_param((uint32_t)atoi(what + 3));
    else if (numbered(what, "send"))      /* sendN: voice N goes through the FX */
        *got = tack_fx_send((uint32_t)atoi(what + 4));
    else if (numbered(what, "len"))       /* lenN: voice N's lane */
        *got = tack_length((uint8_t)atoi(what + 3));
    else if (numbered(what, "locks"))     /* locksN: the selected voice's step N has a lock */
        *got = tack_locked((uint8_t)ui.voice, (uint8_t)atoi(what + 5));
    else if (!strcmp(what, "kit"))
        *got = ui.kit;
    else if (!strcmp(what, "bar"))
        *got = ui.bar;
    else if (!strcmp(what, "notemode"))
        *got = ui.note_mode;
    else if (!strcmp(what, "notestep"))
        *got = ui.note_step;
    else if (!strcmp(what, "noteoct"))
        *got = ui.note_oct;
    else if (numbered(what, "note"))      /* noteN: the selected voice's step N */
        *got = tack_step_get((uint8_t)ui.voice, (uint8_t)atoi(what + 4), TACK_ST_NOTE);
    else if (!strcmp(what, "stepmode"))
        *got = !ui.note_mode;
    else if (!strcmp(what, "running"))
        *got = tack_running();
    else if (!strcmp(what, "pattern"))
        *got = tack_pattern();
    else if (!strcmp(what, "bpm"))
        *got = tack_bpm();
    else if (!strcmp(what, "shuffle"))
        *got = tack_shuffle();
    else if (!strcmp(what, "record"))
        *got = tack_recording();
    else if (!strcmp(what, "ext"))
        *got = tack_ext_sync();
    else if (numbered(what, "muted"))     /* mutedN: voice N */
        *got = tack_muted((uint8_t)atoi(what + 5));
    else if (numbered(what, "step"))      /* stepN: the selected voice's step N */
        *got = tack_step_on((uint8_t)ui.voice, (uint8_t)atoi(what + 4));
    else if (numbered(what, "param"))     /* paramN: the selected voice's slot N */
        *got = tack_get_param(tack_voice_param((uint32_t)ui.voice, (uint32_t)atoi(what + 5) % TACK_NP));
    else if (!strcmp(what, "dropped"))
        *got = (int)tack_dropped_events();
    else if (!strcmp(what, "midi"))
        *got = (int)midi_seen;
    else
        return 0;
    return 1;
}
#endif
