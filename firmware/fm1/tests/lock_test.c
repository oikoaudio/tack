/* SPDX-License-Identifier: GPL-3.0-only */
/* Parameter locks through the real engine and sequencer, as the firmware links them: a lock must reach the parameter it
 * names on its own step, hold until the lane's next step, and give back the kit's value. seq_trace cannot show this: its
 * engine is a stand-in. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "tack_engine.h"

float probe_filter(int voice);
int probe_param_filter1(void);
int probe_filter_type(int voice);
float probe_fx_settings(void);
int probe_fx_type(void);
int probe_param_vol1(void);
int probe_kit_size(void);
int probe_param_filter_type1(void);
void lxr_host_loadKit(const uint8_t *values);
float probe_lfo_rnd(int voice);
float probe_cymbal_pwm(void);

static int fails;
static void check(const char *name, int ok, double a, double b)
{
    printf("  %s %-52s %.1f %.1f\n", ok ? "ok  " : "FAIL", name, a, b);
    fails += !ok;
}

static void run(uint32_t blocks)
{
    int32_t lr[TACK_BLOCK * 2];
    while (blocks--)
        tack_render(lr, TACK_BLOCK, 4096);
}

/* the filter value at each step of the lane, read halfway through the step, over the second time round */
static void lane(float *seen, int steps)
{
    int done = 0, last = -1, wrapped = 0;
    for (uint32_t b = 0; b < 40000 && done < steps; b++) {
        run(1);
        const int h = tack_playhead(0);
        if (h != last) {
            if (wrapped && h < steps && done == h) {
                run(40);                       /* (58 ms: inside a 125 ms step) */
                seen[h] = probe_filter(0);
                done++;
            }
            if (h == 0 && last > 0)
                wrapped = 1;
            last = h;
        }
    }
}

static double level(uint32_t blocks)     /* mean absolute output */
{
    int32_t lr[TACK_BLOCK * 2];
    double sum = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        tack_render(lr, TACK_BLOCK, 4096);
        for (int i = 0; i < TACK_BLOCK * 2; i++)
            sum += fabs((double)lr[i]);
    }
    return sum / (blocks * TACK_BLOCK * 2);
}

static double hit(void)                  /* one hit of voice 0 */
{
    tack_post_note(0, 60, 120);
    return level(120);
}

/* what is left of a short hit after 170 ms: the delay's tail */
static double tail(void)
{
    run(1500);
    tack_post_note(0, 60, 120);
    run(120);
    return level(600);
}

/* each of a type's two settings has to change the sound: the median of three hits, against the same settings again */
static double hits(void)
{
    double a = hit(), b = hit(), c = hit();
    return a > b ? (b > c ? b : (a > c ? c : a)) : (a > c ? a : (b > c ? c : b));
}

static void fx_tests(void)
{
    static const uint8_t decay = 8;
    tack_post_param(probe_param_filter1() + 0, 127);
    tack_post_param(50, decay);            /* PAR_VELOD1, tests in tack_pages.h */
    tack_post_param(tack_fx_param(0), 4);
    run(2);
    tack_post_param(tack_fx_param(1), 100);
    tack_post_param(tack_fx_param(2), 90);
    tack_post_param(tack_fx_param(3), 110);
    tack_post_param(239, 27);              /* the delay's range: the FX page leaves it to the kit */
    run(2);
    const double off = tail();
    tack_post_fx_send(0, 1);
    run(2);
    const double on = tail();
    check("delay type: a voice not sent has no tail", off < 1.0, off, on);
    check("the send gives the voice a tail", on > 200.0, off, on);
    tack_post_param(tack_fx_param(1), 0);
    run(200);
    const double dry = tail();
    check("mix 0 takes the tail away", dry < on / 8.0, dry, on);
    tack_post_param(tack_fx_param(1), 100);

    for (uint8_t type = 1; type <= 4; type++) {
        tack_post_param(tack_fx_param(0), type);
        tack_post_param(240, 30);          /* the compressor's threshold, or its ratio has nothing to act on */
        run(4);
        for (uint32_t sl = 2; sl < 7; sl++) {
            const uint16_t p = tack_fx_param(sl);
            const uint8_t top = tack_fx_max(sl);
            if (!top)
                continue;
            tack_post_param(p, 0);
            run(20);
            const float at0 = probe_fx_settings();
            const double lo = hits(), lo2 = hits();
            tack_post_param(p, top);
            run(20);
            const float atop = probe_fx_settings();
            const double hi = hits();
            char name[48];
            snprintf(name, sizeof name, "FX type %d, setting %u reaches the sound", type, sl - 1);
            check(name, fabs(hi - lo) > 4.0 * fabs(lo2 - lo) + 1.0 || at0 != atop, lo, hi);
        }
    }
    tack_post_param(tack_fx_param(0), 0);
    tack_post_fx_send(0, 0);
    run(4);
}

/* every page of every voice: top and bottom of each slot, with every voice playing. A bad value would stop the render */
static void page_sweep(void)
{
    int slots = 0, wrong = 0;
    for (uint32_t v = 0; v < TACK_VOICES; v++)
        for (uint32_t g = 0; g < 4; g++)
            for (int sub = 0; sub < tack_pages(v, g); sub++)
                for (uint32_t s = 0; s < 4; s++) {
                    const TackSlot *sl = tack_slot(v, g, (uint32_t)sub, s);
                    if (!sl)
                        continue;
                    const uint8_t was = tack_get_param(sl->par);
                    const uint8_t bottom = tack_slot_step(sl, was, -300);
                    const uint8_t top = tack_slot_step(sl, was, 300);
                    slots++;
                    wrong += bottom != tack_slot_step(sl, bottom, -1) || top != tack_slot_step(sl, top, 1);
                    wrong += sl->en && !tack_slot_text(sl, top);
                    tack_post_param(sl->par, bottom);
                    tack_post_note((uint8_t)v, 60, 120);
                    run(4);
                    tack_post_param(sl->par, top);
                    tack_post_note((uint8_t)v, 60, 120);
                    run(4);
                    wrong += tack_get_param(sl->par) != top;
                    tack_post_param(sl->par, was);
                    run(1);
                }
    check("every page slot steps, clamps and renders at both ends", !wrong && slots > 200, slots, wrong);
}

/* hits in 0.1 s windows over n windows: a window that starts quiet and goes loud is one onset */
static int onsets(uint32_t windows)
{
    int count = 0, was_loud = 1;
    for (uint32_t i = 0; i < windows; i++) {
        const int loud = level(4) > 2000.0;
        count += loud && !was_loud;
        was_loud = loud;
    }
    return count;
}

static void keys_tests(void)
{
    /* every Euclidean setting: the right number of hits, the right length, a lane that stays a lane */
    int wrong = 0, cases = 0;
    const uint8_t v = 1;
    for (int len = 1; len <= 16; len++) {
        while (tack_euclid_get(v, 0) != len) {
            tack_euclid_turn(v, 0, len - tack_euclid_get(v, 0));
            run(1);
        }
        for (int hits = 1; hits <= len; hits++) {
            tack_euclid_turn(v, 1, hits - tack_euclid_get(v, 1));
            for (int rot = 0; rot < len; rot++) {
                tack_euclid_turn(v, 2, rot - tack_euclid_get(v, 2));
                run(1);
                int n = 0;
                for (uint8_t s = 0; s < 64; s++)
                    n += tack_step_on(v, s);
                wrong += n != hits || tack_length(v) != len;
                cases++;
            }
        }
    }
    check("Euclid: right hits and length at every setting", !wrong && cases > 200, cases, wrong);

    /* what it writes plays: main steps are what the sequencer reads. Only this lane plays, so a hit heard is its own */
    tack_post_clear(v);
    tack_post_clear(0);
    run(2);
    tack_post_param(52, 4);                /* drum 2's decay: short hits */
    tack_post_param(89, 127);
    tack_post_bpm(60);                     /* 250 ms a step */
    tack_euclid_turn(v, 0, 16 - tack_euclid_get(v, 0));
    tack_euclid_turn(v, 1, 5 - tack_euclid_get(v, 1));
    tack_euclid_turn(v, 2, -tack_euclid_get(v, 2));
    run(2);
    tack_post_run(1);
    run(40);
    /* two times round from after the first hit, so the first hit of the third round closes it: 4 + 5 + 1 */
    const int heard = onsets(1376);
    tack_post_run(0);
    run(40);
    check("Euclid: five hits a time round are heard", heard == 10, 10, heard);

    /* roll on an empty lane: the voice repeats only while it is held */
    tack_post_clear(v);
    tack_post_roll_rate(5);
    run(2);
    tack_post_run(1);
    run(40);
    const int quiet = onsets(350);           /* 2 s */
    tack_post_roll(v, 1);
    run(1);
    const int rolled = onsets(350);          /* 2 s of 250 ms steps: 8 */
    tack_post_roll(v, 0);
    run(100);
    const int after = onsets(350);
    tack_post_run(0);
    run(40);
    tack_post_bpm(120);
    check("roll: silence, then hits while held, then silence", quiet == 0 && rolled >= 7 && rolled <= 8 && after == 0, rolled, after);

    /* copy to another pattern, clear this lane: the copy keeps it, and the lock goes with the lane here */
    tack_post_step(v, 3, 1);
    tack_post_lock(v, 3, 38, 44);
    run(2);
    tack_post_copy(9);
    run(2);
    tack_post_clear(v);
    run(2);
    const int cleared = !tack_step_on(v, 3) && !tack_locked(v, 3) && tack_length(v) == 16;
    tack_post_pattern(9);
    run(4);
    const int copied = tack_pattern() == 9 && tack_step_on(v, 3) && tack_locked(v, 3);
    tack_post_pattern(0);
    run(4);
    check("clear empties the lane and copy kept it in pattern 10", cleared && copied, cleared, copied);
    check("back on pattern 1 the lane stays clear", tack_pattern() == 0 && !tack_step_on(v, 3), tack_pattern(), 0);
}

#define PULSE 7                          /* blocks in one of the sequencer's 24 pulses a step at 60 BPM: its timing grain */

/* blocks from the start to the first hit, -1 if none. The sequencer starts at its step 0 */
static int first_hit(uint32_t blocks)
{
    int at = -1;
    tack_post_run(1);
    for (uint32_t b = 0; b < blocks; b++)
        if (level(1) > 2000.0 && at < 0)
            at = (int)b;
    tack_post_run(0);
    run(200);
    return at;
}

static void step_tests(void)
{
    const uint8_t v = 1;
    tack_post_clear(v);
    tack_post_clear(0);
    tack_post_bpm(60);                     /* 250 ms a step: 172 blocks */
    tack_post_step(v, 4, 1);
    run(3);
    const int base = first_hit(1200);
    tack_step_turn(v, 4, 0, 10);           /* 12 pulses later, half a step: 125 ms, 86 blocks */
    run(3);
    const int late = first_hit(1200);
    tack_step_turn(v, 4, 0, -20);
    run(3);
    const int early = first_hit(1200);
    tack_step_turn(v, 4, 0, 10);
    tack_lane_nudge_turn(v, 6);            /* 6 pulses: 43 blocks */
    run(3);
    const int lane = first_hit(1200);
    tack_lane_nudge_turn(v, -6);
    run(3);
    check("a nudged hit comes 125 ms late", abs(late - base - 86) <= PULSE, base, late);
    check("and 125 ms early", abs(base - early - 86) <= PULSE, base, early);
    check("the lane nudge moves the hit", abs(lane - base - 43) <= PULSE, base, lane);
    check("the steps go back on the grid", tack_step_get(v, 4, 0) == 0, tack_step_get(v, 4, 0), 0);

    tack_post_run(1);
    run(600);
    const int plain = onsets(180);           /* 1 s, step 4 somewhere in it or not: only the counts relate */
    tack_post_run(0);
    run(40);
    tack_step_turn(v, 4, 1, 1);            /* x2 */
    run(3);
    int flam = 0, once = 0;
    for (int rounds = 0; rounds < 2; rounds++) {
        tack_post_run(1);
        const int n = onsets(180 + 120);
        tack_post_run(0);
        run(200);
        flam = rounds ? flam : n;
        once = rounds ? n : once;
        tack_step_turn(v, 4, 1, -1);
        run(3);
    }
    check("a flam on the step hits twice", flam == 2 && once == 1, flam, once);

    tack_step_turn(v, 4, 2, -100);         /* chance 0 */
    run(3);
    tack_post_run(1);
    const int never = onsets(700);
    tack_post_run(0);
    run(200);
    tack_step_turn(v, 4, 2, 100);
    run(3);
    check("chance 0 never plays", never == 0, never, plain);

    /* a step's edits outlive a pattern change: a change restores from the backup buffer */
    tack_step_turn(v, 4, 3, -60);
    tack_step_turn(v, 4, 0, 2);
    run(3);
    tack_post_pattern(3);
    run(4);
    tack_post_pattern(0);
    run(4);
    check("velocity and nudge survive a pattern change", tack_step_get(v, 4, 3) == 67 && tack_step_get(v, 4, 0) == 2,
          tack_step_get(v, 4, 3), tack_step_get(v, 4, 0));
    tack_post_bpm(120);
    tack_post_clear(v);
    run(3);
}

static uint8_t bank[2][512];
static const uint8_t *bank_find(uint32_t slot) { return slot < 2 ? bank[slot] : 0; }

/* the bank's kit a pattern was left with comes back as the sequencer switches to it, before its first step */
int tack_post_lane(uint8_t voice, uint32_t dir, uint32_t speed);
int tack_bits(void);
int tack_post_bits(uint8_t value);
uint32_t tack_lane_dir(uint8_t voice);
uint32_t tack_lane_speed(uint8_t voice);
uint32_t tack_pattern_export(uint8_t pattern, void *image);
int tack_load_pattern(uint8_t pattern, const void *image, uint32_t len);

/* What a pattern holds beside its steps comes back from its image, and what is out of range is refused */
#define IMAGE_LANES (6u + 3u * 448u + 448u + 4u * 7u * 2u + 7u + 2u)
static uint8_t lane_image[3840];
static void lane_format_tests(void)
{
    uint32_t len;
    tack_post_lane(2, 3, 2);
    tack_post_length(2, 12);
    tack_post_bits(100);
    run(2);
    len = tack_pattern_export(0, lane_image);
    check("the image holds the lane's mode", lane_image[3] == '8' && lane_image[IMAGE_LANES + 2u] == (3u | 2u << 3), lane_image[3], lane_image[IMAGE_LANES + 2u]);
    check("and BITS", lane_image[IMAGE_LANES + 14u] == 27, 27, lane_image[IMAGE_LANES + 14u]);
    check("the image loads", tack_load_pattern(0, lane_image, len) == 0, 0, 0);
    check("and keeps its modes", tack_lane_dir(2) == 3 && tack_lane_speed(2) == 2, tack_lane_dir(2), tack_lane_speed(2));
    check("and BITS", tack_bits() == 100, 100, tack_bits());
    lane_image[IMAGE_LANES + 2u] = 7;
    check("a mode out of range is refused", tack_load_pattern(0, lane_image, len) != 0, 0, 0);
    lane_image[IMAGE_LANES + 2u] = (3u | 2u << 3);
    lane_image[IMAGE_LANES + 14u] = 128;
    check("BITS out of range is refused", tack_load_pattern(0, lane_image, len) != 0, 0, 0);
    lane_image[IMAGE_LANES + 14u] = 27;
    lane_image[IMAGE_LANES + 17u] = 2;
    check("and a kit flag", tack_load_pattern(0, lane_image, len) != 0, 0, 0);
    lane_image[IMAGE_LANES + 17u] = 1;
    lane_image[3] = '6';
    check("and so is an image of an earlier build", tack_load_pattern(0, lane_image, len) != 0, 0, 0);
    tack_post_bits(127);
    tack_post_lane(2, 0, 0);
    tack_post_length(2, 16);
    run(2);
}

int tack_post_morph(uint8_t amount);
int tack_post_morph_kit(uint8_t kit);
/* Morph, heard: the filter moves to the second kit's, the kit does not change, a lock on a step takes over for that step, and
 * its end hands back the blend and not the kit's own value */
static void morph_tests(void)
{
    const int fp = probe_param_filter1();
    float a, b, locked, after;
    memcpy(bank[1], tack_kit(), (size_t)probe_kit_size());
    bank[1][fp] = 20;
    tack_post_param((uint16_t)fp, 120);
    run(4);
    a = probe_filter(0);
    tack_post_morph_kit(2);
    tack_post_morph(127);
    run(40);
    b = probe_filter(0);
    check("morph moves the filter", b != a, a, b);
    check("and leaves the kit's value", tack_get_param((uint16_t)fp) == 120, 120, tack_get_param((uint16_t)fp));
    tack_post_step(0, 0, 1);
    tack_post_step(0, 1, 1);
    tack_post_length(0, 2);
    tack_post_lock(0, 0, (uint16_t)fp, 60);
    run(2);
    tack_post_run(1);
    run(40);
    locked = probe_filter(0);
    check("a lock takes over under the morph", locked != b && locked != a, locked, b);
    run(86);
    after = probe_filter(0);
    check("and its end gives back the blend", fabsf(after - b) < 1e-6f, after, b);
    tack_post_run(0);
    tack_post_lock_clear(0, 0);
    tack_post_step(0, 0, 0);
    tack_post_step(0, 1, 0);
    tack_post_length(0, 16);
    tack_post_morph(0);
    tack_post_morph_kit(0);
    run(40);
    check("back to the kit's filter", fabsf(probe_filter(0) - a) < 1e-6f, probe_filter(0), a);

    /* across the dial: the engine's own filter value at each amount, not what the kit says */
    tack_post_morph_kit(2);
    run(4);
    {
        static const int AT[5] = {0, 32, 64, 96, 127};
        float f[5];
        for (int i = 0; i < 5; i++) {
            tack_post_morph((uint8_t)AT[i]);
            run(40);
            f[i] = probe_filter(0);
            printf("  morph %3d filter %.4f\n", AT[i], f[i]);
        }
        check("the filter sweeps across the dial", f[0] > f[1] && f[1] > f[2] && f[2] > f[3] && f[3] > f[4], f[1], f[3]);
    }
    tack_post_morph(0);
    tack_post_morph_kit(0);
    tack_post_param((uint16_t)fp, 120);
    run(40);
}

/* A target of the pattern's own: shaped a parameter at a time from the kit, saved with the pattern, and from a KPT7 image (the
 * format before targets) a copy of the bank kit it morphed to */
static uint8_t target_image[3840];
static void morph_target_tests(void)
{
    const int fp = probe_param_filter1(), vol = probe_param_vol1();
    const uint32_t options = TACK_IMG_LENGTHS + 23u, target = TACK_IMG_LOCKS - 256u;
    float a, b;
    uint32_t len;
    tack_post_param((uint16_t)fp, 120);
    run(4);
    a = probe_filter(0);
    check("no target to begin with", !tack_has_target(), 0, tack_has_target());
    tack_post_target((uint16_t)fp, 30);
    run(2);
    check("a value set starts a target", tack_has_target() && tack_target_value((uint16_t)fp) == 30, 30, tack_target_value((uint16_t)fp));
    tack_kit_busy();                             /* the main loop's view of the kit, as a frame takes it */
    check("from the kit as it is", tack_target_value((uint16_t)vol) == tack_get_param((uint16_t)vol), tack_get_param((uint16_t)vol),
          tack_target_value((uint16_t)vol));
    check("and the kit keeps its own", tack_get_param((uint16_t)fp) == 120, 120, tack_get_param((uint16_t)fp));
    tack_post_morph(127);
    run(40);
    b = probe_filter(0);
    check("morph goes to the target", b < a, a, b);
    check("a routing has no target value", !tack_post_target(tack_send_slot(0)->par, 1), 0, 1);

    /* saved and loaded back */
    len = tack_pattern_export(0, target_image);
    check("the image is the new format", !memcmp(target_image, "KPT8", 4) && target_image[options + 4] == 1, 1, target_image[options + 4]);
    tack_post_morph(0);
    tack_post_morph_kit(0);
    run(4);
    check("no bank kit drops the target", !tack_has_target(), 0, tack_has_target());
    check("it loads back", tack_load_pattern(0, target_image, len) == 0, 0, 1);
    run(4);
    check("with its target", tack_has_target() && tack_target_value((uint16_t)fp) == 30, 30, tack_target_value((uint16_t)fp));

    /* KPT7: no target and its flag, the morph going to bank slot 2 */
    memcpy(bank[1], tack_kit(), (size_t)probe_kit_size());
    bank[1][fp] = 11;
    memmove(target_image + target, target_image + target + 256u, len - target - 256u);
    len -= 256u;
    memmove(target_image + options + 4u, target_image + options + 5u, len - options - 5u);
    len -= 1u;
    memcpy(target_image, "KPT7", 4);
    target_image[options + 2u] = 2;
    check("a KPT7 image loads", tack_load_pattern(0, target_image, len) == 0, 0, 1);
    run(4);
    check("its bank kit as its target", tack_has_target() && tack_target_value((uint16_t)fp) == 11 && tack_morph_kit() == 2, 11,
          tack_target_value((uint16_t)fp));
    target_image[options + 2u] = 0;
    check("and none without one", tack_load_pattern(0, target_image, len) == 0 && (run(4), !tack_has_target()), 0, tack_has_target());
    tack_post_morph(0);
    run(4);
}

/* Frankenkit: each section of each voice from one parent, whole, the level from the base kit, a seed always the same, and a
 * voice's combo leaving the others alone */
static void franken_tests(void)
{
    static uint8_t one[256], two[256], out[256];
    const uint8_t *cells[TACK_FRANKEN_CELLS];
    uint8_t pick[TACK_FRANKEN_CELLS], again[TACK_FRANKEN_CELLS];
    const int fp = probe_param_filter1(), vol = probe_param_vol1();
    int both = 1, cymbal = 1, drum = 0, hat = 0, alone = 1;
    memset(one, 40, sizeof one);
    memset(two, 50, sizeof two);
    two[vol] = 99;
    for (uint32_t seed = 0; seed < 64u; seed++) {
        int used = 0;
        tack_franken_pick(seed, 2, -1, pick);
        for (uint32_t c = 0; c < TACK_FRANKEN_CELLS; c++) {
            used |= 1 << pick[c];
            cells[c] = pick[c] ? two : one;
        }
        both &= used == 3;
        tack_franken_kit(cells, one, out);
        cymbal &= out[16] == out[6] && out[29] == out[6] && out[32] == out[6] && out[23] == out[6];   /* pitch, wave, modulators */
        drum |= out[1] != out[82];
        hat |= out[7] != out[33];
        if (seed == 12345u % 64u) {
            check("drum 1's filter from its cell's kit", out[fp] == (pick[3] ? 50 : 40), pick[3] ? 50 : 40, out[fp]);
            check("the level from the base kit", out[vol] == 40, 40, out[vol]);
        }
    }
    check("every combo takes from both kits", both, 1, both);
    check("the cymbal's tone comes from one kit", cymbal, 1, cymbal);
    check("a drum's oscillator and FM can come from two kits", drum, 1, drum);
    check("so can a hat's oscillator and its modulators", hat, 1, hat);
    tack_franken_pick(12345u, 2, -1, pick);
    tack_franken_pick(12345u, 2, -1, again);
    check("a seed picks the same", !memcmp(pick, again, sizeof pick), 1, 0);
    memset(pick, 7, sizeof pick);
    tack_franken_pick(9u, 2, 3, pick);
    for (uint32_t c = 0; c < TACK_FRANKEN_CELLS; c++)
        alone &= c / TACK_FRANKEN_SECTIONS == 3u ? pick[c] < 2u : pick[c] == 7u;
    check("a voice's combo picks for that voice alone", alone, 1, alone);
}

int tack_step_turn(uint8_t voice, uint8_t step, uint32_t slot, int32_t e);
int tack_step_get(uint8_t voice, uint8_t step, uint32_t slot);
int tack_post_ext_sync(uint8_t on);
int tack_post_bpm(uint16_t bpm);
int tack_post_shuffle(uint8_t amount);
void tack_midi_realtime(uint8_t status);
uint8_t tack_voice_fires(uint8_t voice);

/* Every nudge fires, once a pass. A lane of 27 hits, each with its own nudge from all the way early to all the way late and a
 * rest between them, so that no two touch. Counted over one whole pass, after a pass to settle */
#define NUDGE_STEPS 54u
static int32_t ext_left;
static uint32_t ext_at;
static void nudge_blocks(uint32_t n, uint32_t period, int jitter)
{
    for (uint32_t i = 0; i < n; i++) {
        run(1);
        if (!period)
            continue;
        ext_left += TACK_BLOCK;
        while (ext_left >= (int32_t)period) {       /* a jittery clock: a block early, on time, a block late, averaging out */
            const int32_t wobble = jitter ? (int32_t)(ext_at++ % 3u) - 1 : 0;
            ext_left -= (int32_t)period + wobble * TACK_BLOCK;
            tack_midi_realtime(0xF8);
        }
    }
}

static uint32_t nudge_steps = NUDGE_STEPS, nudge_want = 27u;
static int nudge_slack;
int tack_post_lane_start(uint8_t voice, uint8_t start);
static void nudge_case(const char *name, uint32_t bpm, int external, uint32_t shuffle, int jitter)
{
    const uint32_t step_frames = 44100u * 15u / bpm, period = external ? step_frames / 6u : 0u;
    const uint32_t pass = nudge_steps * step_frames / TACK_BLOCK;
    uint8_t a, b;
    char label[80];
    tack_post_bpm((uint16_t)bpm);
    tack_post_shuffle((uint8_t)shuffle);
    tack_post_ext_sync((uint8_t)external);
    run(4);
    ext_left = 0;
    if (external)
        tack_midi_realtime(0xFA);
    else
        tack_post_run(1);
    nudge_blocks(pass + 40u, period, jitter);
    a = tack_voice_fires(0);
    nudge_blocks(pass, period, jitter);
    b = tack_voice_fires(0);
    if (external)
        tack_midi_realtime(0xFC);
    else
        tack_post_run(0);
    run(4);
    snprintf(label, sizeof label, "every nudge fires: %s", name);
    check(label, abs((int)(uint8_t)(b - a) - (int)nudge_want) <= nudge_slack, nudge_want, (uint8_t)(b - a));
}

/* Hits that cross, an early hit landing before a late one: across two lanes, and on one. Neither silences the other */
static void nudge_cross(const char *name, uint8_t early_voice, uint8_t late_voice)
{
    uint8_t a0, a1, b0, b1;
    tack_post_length(early_voice, 16);
    tack_post_length(late_voice, 16);
    tack_post_step(late_voice, 4, 1);
    tack_post_step(early_voice, 5, 1);
    run(2);
    tack_step_turn(late_voice, 4, 0, 12);      /* 18 pulses late */
    tack_step_turn(early_voice, 5, 0, -13);    /* 21 pulses early, so 3 pulses into step 4 */
    run(2);
    tack_post_run(1);
    run(400);
    a0 = tack_voice_fires(early_voice);
    a1 = tack_voice_fires(late_voice);
    run(1378);                                   /* 2 s, a pass */
    b0 = tack_voice_fires(early_voice);
    b1 = tack_voice_fires(late_voice);
    tack_post_run(0);
    run(4);
    tack_step_turn(late_voice, 4, 0, -12);
    tack_step_turn(early_voice, 5, 0, 13);
    tack_post_step(late_voice, 4, 0);
    tack_post_step(early_voice, 5, 0);
    run(2);
    check(name, early_voice == late_voice ? (uint8_t)(b0 - a0) == 2u : (uint8_t)(b0 - a0) + (uint8_t)(b1 - a1) == 2u, 2,
          (uint8_t)(b0 - a0) + (early_voice == late_voice ? 0 : (uint8_t)(b1 - a1)));
}

/* every field a pattern holds: a copy has them all, and a clear leaves none. New per-pattern state belongs in this list */
static void pattern_state_tests(int filter)
{
    static uint8_t one[4096], two[4096], blank[4096];
    tack_clear_pattern(15);
    const uint32_t blank_len = tack_pattern_export(15, blank);
    tack_post_pattern(12);
    run(4);
    tack_post_step(3, 5, 1);
    tack_post_step(3, 9, 1);
    run(2);
    tack_step_turn(3, 5, 0, 4);
    tack_post_lock(3, 5, (uint16_t)filter, 20);
    tack_post_recur(3, 9, 3, 5);
    tack_post_length(3, 24);
    tack_post_lane(3, 2, 1);
    tack_post_lane_start(3, 4);
    tack_post_shuffle(40);
    tack_post_bits(90);
    tack_post_morph_kit(2);
    tack_morph_now(30);
    run(4);
    const uint32_t one_len = tack_pattern_export(12, one);
    tack_post_copy(14);
    run(4);
    const uint32_t two_len = tack_pattern_export(14, two);
    check("a copy has everything the pattern holds", one_len == two_len && !memcmp(one, two, one_len), one_len, two_len);
    tack_clear_pattern(14);
    const uint32_t cleared_len = tack_pattern_export(14, two);
    check("a clear leaves nothing of it", cleared_len == blank_len && !memcmp(two, blank, blank_len), cleared_len, blank_len);
    tack_morph_now(0);
    tack_post_morph_kit(0);
    tack_post_shuffle(0);
    tack_post_bits(127);
    run(2);
    tack_clear_pattern(12);
    tack_post_pattern(0);
    run(4);
}

/* a hit nudged early plays its own step's locks, ahead of the step they belong to */
static void nudged_lock_tests(int filter)
{
    extern float seq_shuffle;
    static uint8_t image[4096];
    float low = 1.f;
    tack_post_pattern(6);
    run(4);
    tack_post_step(0, 4, 1);
    tack_post_step(0, 12, 1);
    run(4);
    tack_post_lock(0, 4, (uint16_t)filter, 10);
    run(4);
    tack_step_turn(0, 4, 0, -6);
    run(4);
    tack_post_run(1);
    for (uint32_t b = 0; b < 1200; b++) {
        run(1);
        low = probe_filter(0) < low ? probe_filter(0) : low;
    }
    tack_post_run(0);
    run(4);
    check("a step nudged early plays its lock", low < 0.05f, low, 0.05);
    /* the shuffle a pattern is saved with plays when it comes back from the store */
    tack_post_shuffle(100);
    run(2);
    const uint32_t len = tack_pattern_export(6, image);
    seq_shuffle = 0.f;
    tack_load_pattern(6, image, len);
    check("a pattern loaded brings its shuffle", seq_shuffle > 0.78f && seq_shuffle < 0.79f, seq_shuffle, 100.0 / 127.0);
    tack_post_shuffle(0);
    run(2);
    tack_post_clear(0);
    tack_post_pattern(0);
    run(4);
}

static void nudge_tests(void)
{
    static const int PULSES[27] = {-21, -18, -15, -12, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 18, 21};
    nudge_cross("an early hit leaves another voice's late hit", 0, 1);
    nudge_cross("and so does it on its own lane", 0, 0);
    tack_post_length(0, NUDGE_STEPS);
    run(2);
    for (int i = 0; i < 27; i++) {
        tack_post_step(0, (uint8_t)(2 * i), 1);
        run(1);
        tack_step_turn(0, (uint8_t)(2 * i), 0, i - 13);
        run(1);
    }
    int ok = 1;
    for (int i = 0; i < 27; i++)
        ok &= tack_step_get(0, (uint8_t)(2 * i), 0) == PULSES[i];
    check("a nudge's clicks reach these pulses", ok, 1, ok);
    nudge_case("internal 60 BPM", 60, 0, 0, 0);
    nudge_case("internal 120 BPM", 120, 0, 0, 0);
    nudge_case("internal 200 BPM", 200, 0, 0, 0);
    nudge_case("internal 120 BPM with shuffle", 120, 0, 60, 0);
    nudge_case("external 60 BPM", 60, 1, 0, 0);
    nudge_case("external 120 BPM", 120, 1, 0, 0);
    nudge_case("external 200 BPM", 200, 1, 0, 0);
    nudge_case("external 120 BPM with shuffle", 120, 1, 60, 0);
    nudge_case("external 120 BPM, an uneven clock", 120, 1, 0, 1);
    nudge_case("external 180 BPM, an uneven clock", 180, 1, 0, 1);

    /* round the lane from another step, and the same with each direction that changes which step is next */
    tack_post_lane_start(0, 7);
    run(2);
    nudge_case("internal, from step 8", 120, 0, 0, 0);
    nudge_case("external, from step 8", 120, 1, 0, 0);
    tack_post_lane(0, 1, 0);
    run(2);
    nudge_case("reversed, from step 8", 120, 0, 0, 0);
    /* random, over 8 passes: a pass's hits move about inside it, so the ends of the count can each gain or lose one, and two hits
     * of a lane can land on one pulse, which is one hit */
    tack_post_lane(0, 4, 0);
    run(2);
    nudge_steps = 8u * NUDGE_STEPS;
    nudge_want = 216u;
    nudge_slack = 5;                             /* it depends on the draw: twelve seeds gave 213 to 217, with one build at 212 */
    nudge_case("random, from step 8", 120, 0, 0, 0);
    nudge_case("random on external clock", 120, 1, 0, 0);
    nudge_steps = NUDGE_STEPS;
    nudge_want = 27u;
    nudge_slack = 0;

    /* bounce: there and back is 106 steps, step 1 played once and the other 26 hits twice */
    tack_post_lane_start(0, 0);
    tack_post_lane(0, 3, 0);
    run(2);
    nudge_steps = 106u;
    nudge_want = 53u;
    nudge_case("bounce", 120, 0, 0, 0);
    nudge_case("bounce on external clock", 120, 1, 0, 0);
    tack_post_lane_start(0, 9);
    run(2);
    nudge_case("bounce from step 10", 120, 0, 0, 0);
    nudge_steps = NUDGE_STEPS;
    nudge_want = 27u;
    tack_post_lane_start(0, 0);
    tack_post_lane(0, 0, 0);
    run(2);
    tack_post_ext_sync(0);
    tack_post_bpm(120);
    tack_post_shuffle(0);
    for (int i = 0; i < 27; i++) {
        tack_step_turn(0, (uint8_t)(2 * i), 0, 13 - i);
        tack_post_step(0, (uint8_t)(2 * i), 0);
        run(1);
    }
    tack_post_length(0, 16);
    run(2);
}

/* The hats are one voice, so each cuts the other. An open hat's sound, a long way into its decay, by the steps that hit it: -1 none.
 * A step is 86 blocks at 120 BPM, and this measures the fifth */
static double hat_tail(int open_step, int closed_step)
{
    int32_t lr[TACK_BLOCK * 2];
    double sum = 0.0;
    if (open_step >= 0)
        tack_post_step(6, (uint8_t)open_step, 1);
    if (closed_step >= 0)
        tack_post_step(5, (uint8_t)closed_step, 1);
    run(2);
    tack_post_run(1);
    run(4u * 86u);
    for (uint32_t b = 0; b < 86u; b++) {
        tack_render(lr, TACK_BLOCK, 4096);
        for (uint32_t i = 0; i < TACK_BLOCK * 2u; i++)
            sum += (double)lr[i] * (double)lr[i];
    }
    tack_post_run(0);
    if (open_step >= 0)
        tack_post_step(6, (uint8_t)open_step, 0);
    if (closed_step >= 0)
        tack_post_step(5, (uint8_t)closed_step, 0);
    run(600);
    return sqrt(sum / (86.0 * TACK_BLOCK * 2.0));
}

static void hat_tests(void)
{
    const uint16_t closed = tack_slot(5, 0, 0, 2)->par, open = tack_slot(6, 0, 0, 2)->par;
    const uint8_t was_closed = tack_get_param(closed), was_open = tack_get_param(open);
    double alone, cut, rings, both;
    tack_post_param(closed, 20);
    tack_post_param(open, 115);
    run(4);
    alone = hat_tail(0, -1);
    cut = hat_tail(0, 2);
    rings = hat_tail(2, 0);
    both = hat_tail(0, 0);
    check("an open hat rings on alone", alone > 100.0, 100, alone);
    check("a closed hat cuts it", cut < alone * 0.1, alone * 0.1, cut);
    check("an open hat after a closed one rings on", rings > alone * 0.5, alone * 0.5, rings);
    check("on one pulse the open hat is the one heard", both > alone * 0.5, alone * 0.5, both);
    tack_post_param(closed, was_closed);
    tack_post_param(open, was_open);
    run(4);
}

static void pattern_tests(void)
{
    const int vol = probe_param_vol1();
    tack_kit();
    memcpy(bank[0], tack_kit(), (size_t)probe_kit_size());
    memcpy(bank[1], tack_kit(), (size_t)probe_kit_size());
    bank[0][vol] = 100;
    bank[1][vol] = 50;
    tack_set_kit_find(bank_find);
    tack_post_kit(bank[0], 0, 0);
    run(2);
    tack_kit_busy();
    check("a kit taken on pattern 1 is playing", tack_get_param((uint16_t)vol) == 100 && tack_kit_now() == 1,
          tack_get_param((uint16_t)vol), tack_kit_now());
    tack_post_pattern(4);
    run(2);
    tack_post_kit(bank[1], 0, 1);
    run(2);
    tack_post_pattern(0);
    run(2);
    tack_kit_busy();
    check("back on pattern 1 its kit returns", tack_get_param((uint16_t)vol) == 100 && tack_kit_now() == 1,
          tack_get_param((uint16_t)vol), tack_kit_now());

    /* an edit on the kit playing survives a pattern with the same kit, or with none */
    tack_post_param((uint16_t)vol, 77);
    run(2);
    tack_post_pattern(7);                  /* no kit of its own */
    run(2);
    tack_post_pattern(0);
    run(2);
    tack_kit_busy();
    check("a pattern without a kit leaves the edit alone", tack_get_param((uint16_t)vol) == 77, 77, tack_get_param((uint16_t)vol));

    /* the sequencer turns the FX off around a switch: it must come back, kit or no kit */
    tack_post_param(234, 4);
    tack_post_param(236, 50);
    run(2);
    tack_post_pattern(7);
    run(2);
    check("the FX stays on across a pattern without a kit", probe_fx_type() == 4, 4, probe_fx_type());
    tack_post_pattern(0);
    run(2);
    tack_post_param(234, 0);
    run(2);

    /* running: the switch is at the end of the bar, and the kit is already in on the first step of the new pattern */
    tack_post_pattern(4);
    tack_post_step(0, 0, 1);
    run(2);
    tack_post_pattern(0);
    run(2);
    tack_post_step(0, 0, 1);
    run(2);
    tack_post_run(1);
    run(20);
    tack_post_pattern(4);
    int waited = 0;
    while (tack_pattern() != 4 && waited++ < 8000)
        run(1);
    const int now = tack_kit_now();
    tack_post_run(0);
    run(2);
    tack_kit_busy();
    check("running, the kit is in the block the pattern starts", now == 2 && waited < 8000, now, waited);
    tack_post_pattern(0);
    run(4);

    /* what a SAVE writes: any pattern's image, which loads back */
    static uint8_t img[3840], copy[3840];
    tack_post_step(1, 9, 1);
    run(2);
    const uint32_t n = tack_pattern_export(0, img);
    tack_post_pattern(4);
    run(2);
    tack_pattern_export(0, copy);
    check("and can be taken with another pattern active", !memcmp(img, copy, n), n, 0);
    tack_post_pattern(0);
    run(2);
    tack_post_step(1, 9, 0);
    run(2);
    check("edit marks the pattern changed", (tack_dirty() & 1u) != 0, tack_dirty(), 0);
    tack_clean(0);
    tack_post_copy(11);
    run(2);
    check("a copy marks the one it wrote", (tack_dirty() & (1u << 11)) && !(tack_dirty() & 1u), tack_dirty(), 0);
    tack_load_pattern(0, img, n);
    run(2);
    check("an image loads back", tack_step_on(1, 9), tack_step_on(1, 9), 1);
    tack_post_clear(1);
    tack_post_clear(0);
    run(2);
}


/* the list of locks: more than the sequencer's two slots, in any order, and only good images come back from the store */
static uint8_t image[3840], broken[3840];
#define IMG_LOCKS TACK_IMG_LOCKS
#define IMG_LEN TACK_IMG_LENGTHS

static uint32_t mutate(uint32_t len, void (*f)(uint8_t *))
{
    memcpy(broken, image, len);
    f(broken);
    return len;
}
static void bad_magic(uint8_t *p) { p[0] ^= 1; }
static void bad_count(uint8_t *p) { p[4] = 0xFF; p[5] = 0xFF; }
static void bad_note(uint8_t *p) { p[6 + 2] = 200; }
static void bad_chance(uint8_t *p) { p[6 + 1] = 200; }
static void bad_timing(uint8_t *p) { p[6 + 1344] = 0xFF; }
static void bad_length(uint8_t *p) { p[IMG_LEN] = 65; }
static void bad_shuffle(uint8_t *p) { p[IMG_LEN + 7] = 200; }
static void bad_kit(uint8_t *p) { p[IMG_LEN + 8] = 200; }
static void bad_voice(uint8_t *p) { p[IMG_LOCKS + 3 * 2] |= 0xC0; p[IMG_LOCKS + 3 * 2 + 2] |= 0x80; }
static void bad_dest(uint8_t *p) { p[IMG_LOCKS + 3 * 2 + 1] = 0xFF; }
static void bad_order(uint8_t *p)
{
    for (int i = 0; i < 3; i++) {
        const uint8_t t = p[IMG_LOCKS + i];
        p[IMG_LOCKS + i] = p[IMG_LOCKS + 3 * 2 + i];
        p[IMG_LOCKS + 3 * 2 + i] = t;
    }
}
static void bad_twice(uint8_t *p)
{
    for (int i = 0; i < 3; i++)
        p[IMG_LOCKS + 3 + i] = p[IMG_LOCKS + i];
}

static void list_tests(void)
{
    uint8_t v = 0;
    tack_post_run(0);
    tack_post_pattern(3);
    run(8);
    tack_clear_pattern(3);
    check("a cleared pattern has no locks", tack_lock_count() == 0, 0, tack_lock_count());

    /* in descending order, so that each goes in at the front */
    tack_post_step(0, 4, 1);
    for (int i = 7; i >= 0; i--) {
        tack_post_lock(0, 4, (uint16_t)(30 + i * 3), (uint8_t)(10 + i));
        run(2);
    }
    check("a step holds eight locks", tack_lock_count() == 8, 8, tack_lock_count());
    int all = 1;
    for (int i = 0; i < 8; i++)
        all &= tack_lock_get(0, 4, (uint16_t)(30 + i * 3), &v) && v == 10 + i;
    check("each reads back from a list built backwards", all, 1, all);

    /* the ninth, and a change to one already there */
    check("the ninth finds no room", tack_lock_room(0, 4, 99) == TACK_LOCK_STEP_FULL, TACK_LOCK_STEP_FULL, tack_lock_room(0, 4, 99));
    check("one already locked can still change", tack_lock_room(0, 4, 36) == TACK_LOCK_ROOM, TACK_LOCK_ROOM, tack_lock_room(0, 4, 36));
    tack_post_lock(0, 4, 99, 50);
    tack_post_lock(0, 4, 36, 77);
    run(2);
    tack_lock_get(0, 4, 36, &v);
    check("the ninth is refused and a change is not", tack_lock_count() == 8 && v == 77, 77, v);

    /* neighbours, and what goes when */
    tack_post_step(0, 5, 1);
    tack_post_step(1, 4, 1);
    tack_post_lock(0, 5, 40, 1);
    tack_post_lock(1, 4, 41, 2);
    tack_post_lock(5, 63, 42, 3);
    run(2);
    check("other steps have their own", tack_lock_count() == 11 && tack_locked(0, 5) && tack_locked(1, 4) && tack_locked(5, 63), 11, tack_lock_count());
    uint32_t len = tack_pattern_export(3, image);
    tack_post_lock_clear(0, 4);
    run(2);
    check("clearing a step leaves its neighbours", !tack_locked(0, 4) && tack_locked(0, 5) && tack_locked(1, 4) && tack_lock_count() == 3, 3, tack_lock_count());

    /* an image and back, into another pattern */
    tack_post_pattern(4);
    run(8);
    tack_clear_pattern(4);
    check("a good image loads", tack_load_pattern(4, image, len) == 0, 0, 0);
    tack_lock_get(0, 4, 36, &v);
    check("and has its locks", tack_lock_count() == 11 && v == 77 && tack_step_on(1, 4), 11, tack_lock_count());

    /* lane clear takes that voice's only */
    tack_post_clear(0);
    run(2);
    check("a cleared lane loses its own locks", !tack_locked(0, 4) && !tack_locked(0, 5) && tack_locked(1, 4) && tack_locked(5, 63), 2, tack_lock_count());

    /* faulty images, against one pattern that must not change */
    tack_post_pattern(5);
    run(8);
    tack_clear_pattern(5);
    tack_post_step(2, 2, 1);
    tack_post_lock(2, 2, 44, 9);
    run(2);
    const struct { const char *name; void (*f)(uint8_t *); } BAD[] = {
        {"the wrong magic", bad_magic}, {"a count past the limit", bad_count}, {"a note out of range", bad_note},
        {"a chance out of range", bad_chance}, {"timing out of range", bad_timing}, {"a length out of range", bad_length},
        {"shuffle out of range", bad_shuffle}, {"a kit out of range", bad_kit}, {"a lock on a voice that is not there", bad_voice},
        {"a lock with no destination", bad_dest}, {"locks out of order", bad_order}, {"the same lock twice", bad_twice}};
    for (uint32_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        char name[80];
        snprintf(name, sizeof name, "refused: %s", BAD[i].name);
        mutate(len, BAD[i].f);
        check(name, tack_load_pattern(5, broken, len) == -1, -1, 0);
    }
    check("refused: cut short", tack_load_pattern(5, image, len - 1) == -1, -1, 0);
    check("refused: too long", tack_load_pattern(5, image, len + 1) == -1, -1, 0);
    check("refused: empty", tack_load_pattern(5, image, 0) == -1, -1, 0);
    memset(broken, 0x37, 3650);
    check("refused: an older build's pattern", tack_load_pattern(5, broken, 3650) == -1, -1, 0);
    check("a refused image changes nothing", tack_lock_count() == 1 && tack_locked(2, 2) && tack_step_on(2, 2), 1, tack_lock_count());

    /* recurrence: kept with the pattern, sharing the locks' room, and held to the same standard */
    tack_post_recur(2, 2, 3, 5);
    tack_post_recur(2, 7, 2, 2);
    run(2);
    check("recurrence reads back", tack_recur_len(2, 2) == 3 && tack_recur_mask(2, 2) == 5, 3, tack_recur_len(2, 2));
    len = tack_pattern_export(5, image);
    {
        tack_clear_pattern(6);
        const int loaded = tack_load_pattern(6, image, len);
        tack_post_pattern(6);
        run(8);
        check("an image keeps recurrence", loaded == 0 && tack_recur_len(2, 2) == 3 && tack_recur_mask(2, 7) == 2 && tack_recur_len(2, 3) == 1, 3, tack_recur_len(2, 2));
        tack_post_pattern(5);
        run(8);
    }
    {
        const uint32_t last = len - 3;        /* the second entry: key, mask, length - 1 and the key's top bit */
        const struct { const char *name; uint32_t at; uint8_t value; } RECUR[] = {
            {"nine slots", 2, 8}, {"one slot", 2, 0}, {"no slot set", 1, 0}, {"a slot past the cycle", 1, 0xFF},
            {"the same step twice", 0, 0x82}};
        for (uint32_t i = 0; i < sizeof RECUR / sizeof RECUR[0]; i++) {
            char name[80];
            memcpy(broken, image, len);
            broken[last + RECUR[i].at] = RECUR[i].value;
            snprintf(name, sizeof name, "refused: %s", RECUR[i].name);
            check(name, tack_load_pattern(5, broken, len) == -1, -1, 0);
        }
        memcpy(broken, image, len);
        broken[last + 2] |= 0x80;
        broken[last] = 0xC0;
        check("refused: recurrence on a voice that is not there", tack_load_pattern(5, broken, len) == -1, -1, 0);
    }
    /* a saved image with no recurrence is one with the locks alone */
    tack_post_recur(2, 2, 1, 0);
    tack_post_recur(2, 7, 1, 0);
    run(2);
    check("one slot takes recurrence off", tack_recur_len(2, 2) == 1, 1, tack_recur_len(2, 2));

    /* nine on a step, from outside */
    {
        uint8_t *q = image + IMG_LOCKS;
        tack_clear_pattern(5);
        for (uint8_t i = 0; i < 9; i++)
            tack_post_lock(6, 9, (uint16_t)(30 + i), 5);
        run(2);
        len = tack_pattern_export(5, image);
        memmove(q + 3, q, 8 * 3);
        q[0] = q[3];
        q[1] = 20;
        q[2] = q[5];
        image[4] = 9;
        check("refused: nine on a step", tack_load_pattern(5, image, len + 3) == -1, -1, 0);
    }

    /* the pattern's limit */
    tack_clear_pattern(5);
    int put = 0;
    for (uint8_t t = 0; t < TACK_VOICES; t++)
        for (uint8_t s = 0; s < 64; s++)
            for (uint8_t i = 0; i < 8 && put < 660; i++, put++) {
                tack_post_lock(t, s, (uint16_t)(30 + i), 5);
                if (put % 64 == 0)
                    run(2);
            }
    run(4);
    check("a pattern holds its limit", tack_lock_count() == TACK_LOCKS_MAX, TACK_LOCKS_MAX, tack_lock_count());
    check("and then says so", tack_lock_room(6, 60, 80) == TACK_LOCK_PATTERN_FULL, TACK_LOCK_PATTERN_FULL, tack_lock_room(6, 60, 80));
    check("recurrence shares that room", !tack_recur_room(6, 60), 0, tack_recur_room(6, 60));
    len = tack_pattern_export(5, image);
    check("a full pattern fits one store object", len <= 3840, 3840, (int)len);
    check("and loads", tack_load_pattern(6, image, len) == 0, 0, 0);
    tack_clear_pattern(5);
    tack_clear_pattern(6);
    tack_post_pattern(0);
    run(8);
}

/* velocity cannot be aimed at its own settings, on the way up or down, and a kit that holds one keeps it */
static void target_tests(void)
{
    const TackSlot *t = 0;
    for (uint32_t i = 0; i < 4 && !t; i++) {
        const TackSlot *sl = tack_slot(0, 2, 2, i);
        if (sl && sl->target)
            t = sl;
    }
    check("the velocity row has a target", t != 0, 1, t != 0);
    if (!t)
        return;
    uint8_t v = 0;
    int stops = 0, self = 0;
    for (int i = 0; i < 80; i++) {
        const uint8_t next = tack_slot_step(t, v, 1);
        if (next == v)
            break;
        v = next;
        stops++;
        const char *name = tack_slot_text(t, v);
        self += strstr(name, "VEL ") != 0;
    }
    check("turning up passes every target but velocity's own", stops == 35 && !self, 35, stops);
    int down = 0;
    for (int i = 0; i < 80; i++) {
        const uint8_t next = tack_slot_step(t, v, -1);
        if (next == v)
            break;
        v = next;
        down++;
        self += strstr(tack_slot_text(t, v), "VEL ") != 0;
    }
    check("and so does turning down", down == 35 && v == 0 && !self, 35, down);
    check("a kit's own still reads", strstr(tack_slot_text(t, 12), "VEL ") != 0, 1, 0);
}

/* the PULSE dial reaches the voice: the cymbal's, on its page */
static void pulse_tests(void)
{
    const TackSlot *p = tack_slot(4, 1, 0, 2);
    tack_post_param(p->par, 20);
    run(2);
    const float a = probe_cymbal_pwm();
    tack_post_param(p->par, 110);
    run(2);
    const float b = probe_cymbal_pwm();
    check("PULSE reaches the cymbal's oscillator", b > a + 0.3f, a, b);
}

/* USB audio's 10 channels: each voice on its own, and only then. Word w of a frame, its low or high half */
extern volatile uint8_t app_ul_want;
extern uint32_t app_ul_out[];
static long ul_energy(uint32_t ch)
{
    long sum = 0;
    for (uint32_t f = 0; f < 64u; f++) {
        const uint32_t word = app_ul_out[f * 5u + ch / 2u];
        const int16_t v = (int16_t)(ch & 1u ? word >> 16 : word & 0xFFFFu);
        sum += v < 0 ? -v : v;
    }
    return sum;
}

static void ul_tests(void)
{
    static const uint8_t VOICE_CH[6] = {2, 3, 4, 5, 6, 7};
    for (uint32_t v = 0; v < 6u; v++) {
        long own, most = 0;
        for (int i = 0; i < 12; i++)
            run(800);                            /* the last voice's tail gone */
        app_ul_want = 1;
        tack_post_note((uint8_t)v, 60, 127);
        run(6);
        own = ul_energy(VOICE_CH[v]);
        for (uint32_t k = 0; k < 6u; k++)
            if (k != v && ul_energy(VOICE_CH[k]) > most)
                most = ul_energy(VOICE_CH[k]);
        printf("  voice %u: its channel %ld, the loudest other %ld\n", (unsigned)v, own, most);
        check("a voice has its own channel", own > 0 && most * 100 < own, 1, (int)own);
        check("and the master has it", ul_energy(0) + ul_energy(1) > 0, 1, 0);
        check("with no FX the return is silent", ul_energy(8) == 0 && ul_energy(9) == 0, 0, (int)ul_energy(8));
        app_ul_want = 0;
    }
}

/* a random LFO sent by its voice's hit: a new value at each hit, held between them, and only then */
static void random_lfo_tests(void)
{
    const TackSlot *wave = tack_slot(0, 3, 0, 3), *retrig = tack_slot(0, 3, 1, 1);
    float a, b, c;
    int changed = 0;
    tack_post_param(wave->par, 5);
    tack_post_param(retrig->par, 1);
    run(2);
    tack_post_note(0, 60, 100);
    run(1);
    a = probe_lfo_rnd(0);
    run(40);
    b = probe_lfo_rnd(0);
    check("a random LFO holds between hits", a == b, a, b);
    for (int i = 0; i < 8; i++) {
        const float was = probe_lfo_rnd(0);
        tack_post_note(0, 60, 100);
        run(2);
        changed += probe_lfo_rnd(0) != was;
    }
    check("and takes a new value on each hit", changed == 8, 8, changed);
    tack_post_note(1, 60, 100);
    run(2);
    c = probe_lfo_rnd(0);
    tack_post_note(1, 60, 100);
    run(2);
    check("another voice's hit leaves it", c == probe_lfo_rnd(0), c, probe_lfo_rnd(0));
    tack_post_param(retrig->par, 0);
    run(2);
    c = probe_lfo_rnd(0);
    tack_post_note(0, 60, 100);
    run(2);
    check("with no source a hit leaves it", c == probe_lfo_rnd(0), c, probe_lfo_rnd(0));
    tack_post_param(wave->par, 0);
    run(2);
}

/* a voice's eight locks all reach the engine at its step, and go back at the next */
static void many_locks_play(int fp, float base)
{
    float b[16];
    tack_post_lock(0, 4, (uint16_t)fp, 20);
    for (int i = 1; i < 8; i++)
        tack_post_lock(0, 4, (uint16_t)(fp + 20 + i), 40);
    run(2);
    tack_post_run(1);
    lane(b, 16);
    tack_post_run(0);
    run(4);
    check("among eight locks the filter's still works", fabsf(b[4] - base) > 0.1f * fabsf(base), base, b[4]);
    check("and goes back at the next hit", fabsf(b[8] - base) < 1e-3f * fabsf(base) + 1e-6f, base, b[8]);
    tack_post_lock_clear(0, 4);
    run(2);
}

int main(void)
{
    const int fp = probe_param_filter1();
    float a[16], b[16];
    float other0, other1;

    tack_init();
    tack_post_param((uint16_t)fp, 100);
    for (uint8_t s = 0; s < 16; s += 4)
        tack_post_step(0, s, 1);
    run(4);
    const float base = probe_filter(0);
    other0 = probe_filter(1);

    /* no lock: the filter stays at the kit's value */
    tack_post_run(1);
    lane(a, 16);
    int flat = 1;
    for (int i = 0; i < 16; i++)
        flat &= fabsf(a[i] - base) < 1e-3f * fabsf(base) + 1e-6f;
    check("without a lock every step reads the kit's filter", flat, base, a[8]);
    tack_post_run(0);
    run(4);

    /* a lock on step 4 */
    tack_post_lock(0, 4, (uint16_t)fp, 20);
    run(2);
    tack_post_run(1);
    lane(b, 16);
    tack_post_run(0);
    run(4);
    other1 = probe_filter(1);
    check("before the locked step, the kit's filter", fabsf(b[0] - base) < 1e-3f * fabsf(base) + 1e-6f, base, b[0]);
    check("the locked step changes the filter", fabsf(b[4] - base) > 0.1f * fabsf(base), base, b[4]);
    check("it holds until the lane's next step", b[5] == b[4] && b[7] == b[4], b[4], b[7]);
    check("the next step gives the kit's filter back", fabsf(b[8] - base) < 1e-3f * fabsf(base) + 1e-6f, base, b[8]);
    check("the lock changes only its own voice", other0 == other1, other0, other1);

    /* the kit's value edited while a lock is in the pattern: the step returns to the new value */
    tack_post_param((uint16_t)fp, 60);
    run(4);
    const float base2 = probe_filter(0);
    tack_post_run(1);
    lane(b, 16);
    tack_post_run(0);
    run(4);
    check("an edited kit value is what the lock returns to", fabsf(b[8] - base2) < 1e-3f * fabsf(base2) + 1e-6f, base2, b[8]);

    /* cleared */
    tack_post_lock_clear(0, 4);
    run(2);
    tack_post_run(1);
    lane(b, 16);
    tack_post_run(0);
    check("a cleared lock leaves the kit's filter", fabsf(b[4] - base2) < 1e-3f * fabsf(base2) + 1e-6f, base2, b[4]);

    /* a lock on a parameter above 127, which automation sends another way */
    const int tp = probe_param_filter_type1();
    tack_post_param((uint16_t)tp, 0);
    run(4);
    const int type0 = probe_filter_type(0);
    tack_post_lock(0, 4, (uint16_t)tp, 2);
    run(2);
    tack_post_run(1);
    int seen = type0;
    for (uint32_t b2 = 0; b2 < 12000 && seen == type0; b2++) {
        run(1);
        seen = probe_filter_type(0);
    }
    tack_post_run(0);
    run(4);
    check("a lock reaches a parameter above 127", seen != type0, type0, seen);
    tack_post_lock_clear(0, 4);
    tack_post_param((uint16_t)tp, 0);
    run(4);
    tack_post_param((uint16_t)fp, 100);
    run(4);
    many_locks_play(fp, probe_filter(0));
    list_tests();
    target_tests();
    random_lfo_tests();
    pulse_tests();

    fx_tests();
    page_sweep();
    keys_tests();
    step_tests();
    pattern_tests();
    lane_format_tests();
    morph_tests();
    morph_target_tests();
    franken_tests();
    nudge_tests();
    nudged_lock_tests(fp);
    pattern_state_tests(fp);
    hat_tests();

    ul_tests();

    printf(fails ? "FAILED\n" : "all passed\n");
    return fails != 0;
}
