/* SPDX-License-Identifier: GPL-3.0-only */
/* The panel, read once a frame. app.c includes this: it is one file to the compiler, and shares app.c's state */


/* the keys: notes, voices, steps, whichever the page and the buttons held make them */
static void input_keys(uint32_t keys, uint32_t btn)
{
    uint32_t down = keys & ~ui.keys, up = ui.keys & ~keys, k;

    if (ui.held >= 0 && ui.held_pattern != (uint8_t)tack_pattern()) {   /* the step is in the pattern that left */
        ui.held = -1;
        ui.redraw = 1;
    }
    const uint32_t ended = up & ui.note_held;
    ui.note_held &= ~up;
    if (ended) {
        note_keys(0, ended, 0);
        up &= ~ended;
    }
    if (keys_notes(btn)) {                       /* SEQ or ARP held, or their own page, and the keys do what they always do */
        note_keys(down, 0, btn >> B_SEL & 1u);   /* (what comes up was held for something else: ROLL, a step, a voice, which end below) */
        ui.note_held |= down;
        down = 0;
    }
    if (ui.held >= 0) {          /* a step held: the black keys are its slots */
        for (k = 0; k < TACK_RECUR_MAX; k++)
            if (down >> BK[k] & 1u) {
                recur_key(k);
                ui.redraw = 1;
            }
        for (k = 0; k < TACK_RECUR_MAX; k++)
            down &= ~(1u << BK[k]);
    }
    for (k = 0; k < TACK_VOICES; k++)          /* the black keys: voice, or with ARP held its mute */
        if (down >> BK[k] & 1u) {
            ui.lane_key = 0;                     /* only a plain press is the lane's key */
            if (btn >> B_ARP & 1u) {
                ui.arp_used = 1;
                tack_post_mute((uint8_t)k, (uint8_t)!tack_muted((uint8_t)k));
            } else if (ui.over == PAGE_FX) {
                mark('F');
                tack_post_fx_send(k, (uint8_t)!tack_fx_send(k));
            } else if (ui.over == PAGE_MIX && (btn >> B_SEL & 1u)) {
                ui.sel_used = 1;
                tack_post_solo((uint8_t)k);
            } else {
                const int screen = screen_now();  /* the same screen, whose pages may be numbered differently */
                ui.voice = (int32_t)k;
                ui.touched_until = 0;
                ui.seq_voice |= (uint8_t)(btn >> B_SEQ & 1u);
                if (ui.group != TACK_G_MAIN)
                    ui.sub = screen_page(screen, 1);
                if (!(btn >> B_SEQ & 1u)) {
                    if (!ui.keys_quiet || tack_recording())   /* what REC is taking in plays, or nothing could be recorded */
                        tack_post_note((uint8_t)k, TACK_NOTE_CENTRE, 110);
                    ui.lane_key = (uint8_t)(k + 1u);
                    ui.lane_at = plat_ms();
                }
            }
            ui.redraw = 1;
        }
    for (k = 0; k < TACK_VOICES; k++)          /* (a key that selected silently has nothing to end: this does nothing) */
        if (up >> BK[k] & 1u)
            tack_post_note_off((uint8_t)k, TACK_NOTE_CENTRE);
    if (down >> BK[KEY_ROLL] & 1u) {
        ui.roll_voice = (uint8_t)ui.voice;
        ui.roll_on = 1;
        tack_post_roll(ui.roll_voice, 1);
        ui.redraw = 1;
    }
    if (ui.roll_on && (up >> BK[KEY_ROLL] & 1u)) {
        ui.roll_on = 0;
        tack_post_roll(ui.roll_voice, 0);
        ui.redraw = 1;
    }
    if ((down | up) >> BK[KEY_COPY] & 1u || (down | up) >> BK[KEY_MORPH] & 1u)
        ui.redraw = 1;
    if (down >> BK[KEY_COPY] & 1u)
        ui.copy_used = 0;
    if (down >> BK[KEY_MORPH] & 1u)              /* what was said gives way to MORPH TO: it may be the hint to hold this key */
        ui.msg_until = 0;
    for (k = 0; k < 16u; k++) {                  /* the white keys: steps, or the voices */
        if (down >> WK[k] & 1u) {
            if (btn >> B_SEQ & 1u) {             /* SEQ held: a pattern, in either mode; more of them a chain */
                if (!ui.seq_used) {
                    ui.seq_used = 1;
                    ui.chain_n = 1;
                    pattern_pick(k);
                } else if (ui.chain_n < TACK_CHAIN_MAX) {
                    char t[12];
                    tack_post_chain(ui.chain_n++, (uint8_t)k);
                    text_put(int_put(t, ui.chain_n), " PATTERNS");
                    app_say("CHAIN", t);
                }
            } else if (keys >> BK[KEY_COPY] & 1u) {
                pattern_copy(k);
            } else if (ui.over == PAGE_SONG) {
                song_key(k);
            } else if (ui.over == PAGE_SAVE) {     /* the keys are the slots, for a pattern */
                if (ui.ks_what == SAVE_PATTERN)
                    ui.ks_pat = (uint8_t)k;
            } else if ((btn >> B_SEL & 1u) || lane_key_held()) {   /* SEL or the lane's key held: the lane ends here */
                lane_length((uint8_t)ui.voice, step_of(k) + 1);
                app_say("LENGTH", "");
            } else if (ui.held >= 0) {           /* a second key: a step at once */
                toggle_step((uint8_t)step_of(k));
            } else {
                hold_step(k);
            }
            ui.redraw = 1;
        }
        if ((up >> WK[k] & 1u) && ui.held == (int32_t)k) {
            if (!ui.held_used)
                toggle_step(ui.held_step);
            ui.held = -1;
            ui.redraw = 1;
        }
    }
    if ((up >> BK[KEY_COPY] & 1u) && !ui.copy_used && ui.over == PAGE_SONG)
        chain_write();
    ui.keys = keys;
}

/* the buttons: pages, modes, what is held a second */
static void input_buttons(uint32_t keys, uint32_t btn)
{
    if (pressed(btn, B_SEQ)) {
        ui.seq_used = ui.seq_voice = 0;
        ui.redraw = 1;
    }
    if (((ui.btn & ~btn) >> B_SEQ & 1u) && !ui.seq_used && !ui.seq_voice) {   /* the detail last seen; there, the other one */
        ui.held = -1;
        if (seq_in_view() && ui.voice_view) {
            ui.note_mode = !ui.note_mode;
        } else {
            to_main();
            if (!ui.voice_view)
                ui.note_mode = ui.note_last;
            ui.voice_view = 1;
        }
        ui.redraw = 1;
    }
    if (pressed(btn, B_FX)) {
        ui.over = PAGE_FX;
        ui.redraw = 1;
    }
    if (pressed(btn, B_GLO))
        ui.glo_used = 0;
    if (((ui.btn & ~btn) >> B_GLO & 1u) && !ui.glo_used) {
        ui.over = ui.over == PAGE_MIX ? PAGE_SET : PAGE_MIX;   /* round its two screens, as EDIT and ARP go round theirs */
        ui.redraw = 1;
    }
    if (pressed(btn, B_ARP)) {
        ui.arp_used = 0;
        if (btn >> B_SEQ & 1u) {                 /* SEQ held is the patterns, and this is what plays them in order */
            ui.arp_used = ui.seq_voice = 1;
            ui.over = PAGE_SONG;
            ui.redraw = 1;
        }
    }
    if (((ui.btn & ~btn) >> B_ARP & 1u) && !ui.arp_used) {
        ui.over = arp_page() && !vel_page() ? ui.over + 1u : PAGE_LANE;   /* as EDIT goes through its screens */
        ui.redraw = 1;
    }
    if (pressed(btn, B_SEL)) {
        ui.sel_used = 0;
        ui.sel_at = plat_ms();
    }
    if ((btn >> B_SEL & 1u) && ((btn & ~(1u << B_SEL)) || keys))
        ui.sel_used = 1;
    if (((ui.btn & ~btn) >> B_SEL & 1u) && !ui.sel_used) {
        if (seq_detail() && plat_ms() - ui.sel_at < SEL_SHOW_MS)   /* the next step; a hold was to read the length or the list */
            vv_walk(1);
    }
    if (ui.over == PAGE_SAVE) {             /* no page, so nothing to lose: the name's two shortcuts */
        if (ui.ks_what != SAVE_PATTERN && pressed(btn, B_OCTDN))
            save_name_clear();
        else if (ui.ks_what != SAVE_PATTERN && pressed(btn, B_OCTUP))
            save_name_make_up();
        ui.redraw = 1;
    } else if (full_page() && !seq_detail() && !arp_page()) {   /* an OCT held is a row, which the next frame's knobs start from */
        if ((btn ^ ui.btn) & (1u << B_OCTDN | 1u << B_OCTUP))
            ui.redraw = 1;
    } else if ((btn >> B_SEL & 1u) && (pressed(btn, B_OCTUP) || pressed(btn, B_OCTDN))) {
        lane_bar(pressed(btn, B_OCTUP) ? 1 : -1);
        ui.redraw = 1;
    } else if (ui.note_mode && !arp_page()) {    /* the range of the keys; the knob walks the bars */
        ui.note_oct = (int8_t)clamp((int32_t)ui.note_oct + (int32_t)pressed(btn, B_OCTUP) - (int32_t)pressed(btn, B_OCTDN), -3, 3);
        if (pressed(btn, B_OCTUP) || pressed(btn, B_OCTDN))
            ui.redraw = 1;
    } else if (pressed(btn, B_OCTDN) && ui.bar > 0) {
        ui.bar--;
        ui.held_used = 1;
        ui.redraw = 1;
    } else if (pressed(btn, B_OCTUP)) {          /* the bars the lanes reach */
        if (ui.bar + 1 < bars_shown()) {
            ui.bar++;
            ui.held_used = 1;
            ui.redraw = 1;
        } else if (ui.bar < (int32_t)TACK_BARS - 1) {
            app_say("SEL OCT+", "COPY BAR");
        }
    }
    if (!(btn >> B_SEL & 1u) && ui.bar >= bars_shown()) {   /* a shorter lane, or nothing set past the end; SEL held, a new length is on its way */
        ui.bar = bars_shown() - 1;
        ui.redraw = 1;
    }
    if (pressed(btn, B_PLAY) && !ui.save_todo) {
        if ((btn >> B_GLO & 1u) || ui.stress) {  /* the load test: not a thing to find by accident */
            ui.glo_used = 1;
            ui.stress = !ui.stress;
            ui.cpu_peak = 0;
            ui.xrun_base = plat_xruns();
            ui.xrun_act = 0;
            tack_post_stress(ui.stress);
            app_say("LOAD TEST", ui.stress ? "ON" : "OFF");
        } else if (!tack_ext_sync()) {
            tack_post_run((uint8_t)!tack_running());
            ui.redraw = 1;
        } else {                                 /* the clock's Start and Stop run it: a button that seems dead says why */
            app_say("EXT CLOCK", "SEE GLO");
        }
    }
    if (pressed(btn, B_REC)) {
        tack_post_record((uint8_t)!tack_recording());
        ui.redraw = 1;
    }
    if (pressed(btn, B_ENV))
        page_press(TACK_G_ENV);
    if (pressed(btn, B_LFO))
        page_press(TACK_G_LFO);
    clear_hold(btn);
    if (held_second(btn, 1u << B_SEL | 1u << B_HOME, &ui.revert_at, "TO REVERT"))
        kit_revert();
    if (pressed(btn, B_EDIT) && ui.held < 0 && !(btn >> B_SEL & 1u))
        page_press(TACK_G_EDIT);
    if (pressed(btn, B_EDIT) && ui.held >= 0) {  /* a step held: its locks go */
        tack_post_lock_clear((uint8_t)ui.voice, ui.held_step);
        ui.held_used = 1;
        held_read(0);
        app_say("LOCKS", "CLEARED");
    }
    if (pressed(btn, B_SAVE)) {
        if (btn >> B_SEL & 1u) {                 /* no questions: all that SAVE's page would list */
            save();
            if (ui.over == PAGE_SAVE && !tack_running())
                ui.over = 0;
        } else if (ui.over == PAGE_SAVE) {
            save_commit();
        } else {
            save_open();
        }
        ui.redraw = 1;
    }
    if (pressed(btn, B_HOME) && (btn >> B_GLO & 1u)) {
        ui.glo_used = 1;
        ui.cpu_peak = 0;
        ui.xrun_base = plat_xruns();
        ui.xrun_act = 0;
        app_say("LOAD", "ZEROED");
    } else if (pressed(btn, B_HOME) && !(btn >> B_SEL & 1u)) {
        if (ui.voice_view || ui.note_mode)
            ui.note_last = ui.note_mode;
        ui.note_mode = 0;
        to_main();
        ui.voice_view = 0;
        if (ui.held >= 0)
            held_read(1);
        ui.redraw = 1;
    }
    ui.btn = btn;
    if (ui.held >= 0 && ui.row_seen != row_now()) {   /* a step held: the row reached starts from its own values */
        held_read(1);
        ui.redraw = 1;
    }
}

/* the dials: K1 to K4 are the page's, and the three on the left mostly the same everywhere */
static void input_dials(uint32_t btn)
{
    uint32_t k;
    int32_t e;

    for (k = 0; k < TACK_NP; k++)
        if ((e = plat_enc(EN_K1 + (int)k)) != 0) {
            ui.sel_used |= (uint8_t)(btn >> B_SEL & 1u);
            if (ui.over == PAGE_SET) {
                set_turn(k, e);
            } else if (vel_page()) {
                vel_turn(k, e);
            } else if (ui.over == PAGE_SAVE) {
                save_turn(k, e);
            } else if (ui.over == PAGE_EUCLID) {
                eu_turn(k, e);
            } else if (ui.over == PAGE_LANE) {
                lane_turn(k, e);
            } else if (song_pages()) {
                song_turn(k, e, btn >> B_SEL & 1u);
            } else if (note_view()) {
                note_turn(k, e);
            } else if (ui.over == PAGE_FX) {
                fx_turn(k, e);
            } else if (voice_page()) {
                vv_turn(k, e);
            } else if (ui.held >= 0) {
                lock_turn(k, e);
                touch(k);
            } else {
                const TackSlot *sl = slot(k);
                if (sl == tack_send_slot((uint32_t)ui.voice)) {
                    mark('F');
                    tack_post_fx_send((uint32_t)ui.voice, e > 0);
                } else if (sl && !tack_slot_turn(sl, e)) {
                    tack_post_param(sl->par, tack_slot_step(sl, tack_get_param(sl->par), e));
                }
                touch(k);
            }
            ui.redraw = 1;
        }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        ui.sel_used |= (uint8_t)(btn >> B_SEL & 1u);
        if (ui.over == PAGE_SAVE)
            save_slot(e);
        else if (ui.held >= 0)
            recur_turn(e);
        else if (seq_detail() && (btn >> B_SEL & 1u))
            vv_bar_jump(e);
        else if (seq_detail())
            vv_walk(e);
        else {
            if (btn >> B_SEL & 1u)
                tack_post_shuffle((uint8_t)clamp((int32_t)tack_shuffle() + e, 0, 127));
            else
                tack_post_bpm((uint16_t)clamp((int32_t)tack_bpm() + e, 30, 300));
            tempo_show((int)(btn >> B_SEL & 1u));
        }
        ui.redraw = 1;
    }
    if (ui.popup && ui.held < 0) {               /* the step let go: the page again */
        ui.popup = POP_NONE;
        ui.redraw = 1;
    }
    if (ui.held >= 0 && (e = plat_enc(EN_ALGO)) != 0) {
        step_turn(TACK_ST_NUDGE, POP_NUDGE, e);
        ui.redraw = 1;
    } else if ((e = plat_enc(EN_ALGO)) != 0) {
        ui.sel_used |= (uint8_t)(btn >> B_SEL & 1u);
        if (ui.over == PAGE_SAVE) {
            ;
        } else if (btn >> B_SEL & 1u) {
            ui.sel_used = 1;
            mutate_turn((uint32_t)ui.voice, e);
        } else if (seq_detail()) {
            vv_turn(1u, e);
        } else if (ui.over == PAGE_LANE) {  /* steps on screen, and all of them */
            mark('N');
            tack_lane_nudge_turn((uint8_t)ui.voice, e);
        } else {
            morph_turn(e);
        }
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        ui.sel_used |= (uint8_t)(btn >> B_SEL & 1u);
        if (ui.over == PAGE_SAVE)
            ;
        else if (ui.held >= 0)
            step_turn(TACK_ST_PROB, POP_CHANCE, e);
        else if (btn >> B_SEL & 1u) {
            ui.sel_used = 1;
            mutate_turn(TACK_VOICES, e);
        }
        else if (morph_held())
            morph_kit_step(e);
        else
            kit_step(e);
        ui.redraw = 1;
    }
}

static void input(void)
{
    const uint32_t keys = plat_keys(), btn = plat_buttons();
    input_keys(keys, btn);
    input_buttons(keys, btn);
    input_dials(btn);
}
