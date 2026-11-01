/* SPDX-License-Identifier: GPL-3.0-only */
/* Parameter locks through the real engine and sequencer, as the firmware links them: a lock must reach the parameter it
 * names on its own step, hold until the lane's next step, and give back the kit's value. seq_trace cannot show this: its
 * engine is a stand-in. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define TACK_VOICES 7
#define TACK_BLOCK 64
void tack_init(void);
void tack_render(int32_t *lr, uint32_t n, uint32_t gain_q12);
int tack_post_param(uint16_t param, uint8_t value);
int tack_post_step(uint8_t voice, uint8_t step, uint8_t on);
int tack_post_run(uint8_t on);
int tack_post_lock(uint8_t voice, uint8_t step, uint16_t param, uint8_t value);
int tack_post_lock_clear(uint8_t voice, uint8_t step);
int tack_post_length(uint8_t voice, uint8_t length);
int tack_playhead(uint8_t voice);
float probe_filter(int voice);
int probe_param_filter1(void);
int probe_filter_type(int voice);
float probe_fx_settings(void);
int probe_param_filter_type1(void);
int tack_post_note(uint8_t voice, uint8_t note, uint8_t vel);
int tack_post_fx_send(uint32_t voice, uint8_t on);
uint16_t tack_fx_param(uint32_t slot);
uint8_t tack_fx_max(uint32_t slot);
uint8_t tack_get_param(uint16_t param);
typedef struct { const char *label; uint16_t par; uint8_t max, en, target; } TackSlot;
int tack_pages(uint32_t voice, uint32_t group);
const TackSlot *tack_slot(uint32_t voice, uint32_t group, uint32_t sub, uint32_t slot);
uint8_t tack_slot_step(const TackSlot *s, uint8_t value, int32_t e);
const char *tack_slot_text(const TackSlot *s, uint8_t value);
void lxr_host_loadKit(const uint8_t *values);
int tack_length(uint8_t voice);
int tack_step_on(uint8_t voice, uint8_t step);
int tack_locked(uint8_t voice, uint8_t step);
int tack_euclid_turn(uint8_t voice, uint32_t slot, int32_t e);
uint8_t tack_euclid_get(uint8_t voice, uint32_t slot);
int tack_post_roll(uint8_t voice, uint8_t on);
int tack_post_roll_rate(uint8_t rate);
int tack_post_copy(uint8_t dst);
int tack_post_clear(uint8_t voice);
int tack_post_pattern(uint8_t pattern);
int tack_post_bpm(uint16_t bpm);
int tack_pattern(void);
int tack_step_turn(uint8_t voice, uint8_t step, uint32_t slot, int32_t e);
int tack_step_get(uint8_t voice, uint8_t step, uint32_t slot);
int tack_lane_nudge_turn(uint8_t voice, int32_t e);

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
    tack_step_turn(v, 4, 0, 4);            /* 4 eighths of a step later: 125 ms, 86 blocks */
    run(3);
    const int late = first_hit(1200);
    tack_step_turn(v, 4, 0, -8);
    run(3);
    const int early = first_hit(1200);
    tack_step_turn(v, 4, 0, 4);
    tack_lane_nudge_turn(v, 2);            /* 43 blocks */
    run(3);
    const int lane = first_hit(1200);
    tack_lane_nudge_turn(v, -2);
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

    fx_tests();
    page_sweep();
    keys_tests();
    step_tests();

    printf(fails ? "FAILED\n" : "all passed\n");
    return fails != 0;
}
