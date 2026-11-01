/* SPDX-License-Identifier: GPL-3.0-only */
/* Writes src/tack_pages.h, the FM-1's sound pages, from the desktop build's own parameter list: numbers, ranges and
 * value names come from the LXR's menu tables, so a page cannot drift from the engine.
 *
 *   firmware/fm1/tools/gen_pages.sh */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lxr_params.h"

enum { MAIN, EDIT, ENV, LFO };
typedef struct { const char *label, *suffix; } Slot;
typedef struct { int group; const char *title; Slot s[4]; int graph; } Page;
enum { G_NONE, G_AMP, G_MOD, G_LFO, G_OSC, G_FM, G_MODWAVE };    /* what the screen draws for a page: tack_engine.h */

static const Page PAGE[] = {
    {MAIN, "", {{"PITCH", "Oscillator Coarse"}, {"FILTER", "Filter Frequency"}, {"DECAY", "Amp EG Decay"}, {"VOLUME", "Volume"}}, G_AMP},
    {EDIT, "OSC", {{"FINE", "Oscillator Fine"}, {"WAVE", "Oscillator Waveform"}, {"PW", "Oscillator Pulse Width"}, {"MODE", "Oscillator Mode"}}, G_OSC},
    {EDIT, "FM", {{"AMOUNT", "FM Amount"}, {"FREQ", "FM Frequency"}, {"WAVE", "FM Waveform"}, {0, 0}}, G_FM},
    {EDIT, "NOISE", {{"NOISE", "Noise Frequency"}, {"N MIX", "Noise/Osc Mix"}, {0, 0}, {0, 0}}},
    {EDIT, "MOD OSC", {{"FREQ 1", "Mod Osc Frequency 1"}, {"FREQ 2", "Mod Osc Frequency 2"}, {"GAIN 1", "Mod Osc Gain 1"}, {"GAIN 2", "Mod Osc Gain 2"}}},
    {EDIT, "MOD WAVE", {{"WAVE 1", "Mod Osc Waveform 1"}, {"WAVE 2", "Mod Osc Waveform 2"}, {0, 0}, {0, 0}}, G_MODWAVE},
    {EDIT, "FILTER", {{"FILTER", "Filter Frequency"}, {"RES", "Filter Resonance"}, {"TYPE", "Filter Type"}, {"DRIVE", "Filter Overdrive"}}},
    {EDIT, "TRANSIENT", {{"VOLUME", "Transient Volume"}, {"SAMPLE", "Transient Waveform"}, {"PITCH", "Transient Frequency"}, {0, 0}}},
    {EDIT, "OUTPUT", {{"PAN", "Panning"}, {"DRIVE", "Overdrive"}, {"RATE", "Sample Rate"}, {0, 0}}},
    {ENV, "AMP EG", {{"ATTACK", "Amp EG Attack"}, {"DECAY", "Amp EG Decay"}, {"CURVE", "Amp EG Slope"}, {"REPEAT", "Amp EG Repeat Count"}}, G_AMP},
    {ENV, "MOD EG", {{"DECAY", "Mod EG Decay"}, {"CURVE", "Mod EG Slope"}, {"PITCH", "Pitch Mod Amount"}, {0, 0}}, G_MOD},
    {ENV, "VELOCITY", {{"TARGET", "Velocity Destination"}, {"AMOUNT", "Velocity Amount"}, {"VOLUME", "Velocity Volume Mod"}, {0, 0}}},
    {LFO, "LFO", {{"TARGET", "LFO Destination"}, {"AMOUNT", "LFO Amount"}, {"FREQ", "LFO Frequency"}, {"WAVE", "LFO Waveform"}}, G_LFO},
    {LFO, "LFO SYNC", {{"SYNC", "LFO Clock Sync"}, {"RETRIG", "LFO Retrigger"}, {"PHASE", "LFO Offset"}, {0, 0}}, G_LFO},
};
#define NPAGE (sizeof PAGE / sizeof PAGE[0])
static const char *const VOICE[7] = {"Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hi-hat", "Hi-hat"};

static const LxrParamInfo *find(int voice, const char *suffix)
{
    char name[96], alt[96];
    snprintf(name, sizeof name, "%s %s", VOICE[voice], suffix);
    snprintf(alt, sizeof alt, "%s %s %s", VOICE[voice], suffix, voice == 6 ? "Open" : "Closed");
    for (int i = 0; i < lxr_params_count(); i++) {
        const LxrParamInfo *p = lxr_params_info(i);
        if (!strcmp(p->name, name) || !strcmp(p->name, alt))
            return p;
    }
    return 0;
}

#define MAX_ENUM 64
static char *enum_key[MAX_ENUM];

/* a modulation target's name in what the screen's value line holds */
static void shorten(char *t)
{
    static const char *const R[][2] = {
        {"Drum 1", "D1"}, {"Drum 2", "D2"}, {"Drum 3", "D3"}, {"Snare", "SN"}, {"Cymbal", "CY"}, {"Hi-hat", "HH"},
        {"Oscillator", "OSC"}, {"Frequency", "FRQ"}, {"Filter", "FLT"}, {"Amp EG", "AMP"}, {"Mod EG", "MOD"},
        {"Velocity", "VEL"}, {"Transient", "TRN"}, {"Resonance", "RES"}, {"Waveform", "WAV"}, {"Overdrive", "DRV"},
        {"Amount", "AMT"}, {"Decay", "DEC"}, {"Attack", "ATK"}, {"Slope", "SLP"}, {"Pulse Width", "PW"},
        {"Panning", "PAN"}, {"Volume", "VOL"}, {"Sample Rate", "SR"}, {"Coarse", "CRS"}, {"Destination", "DST"},
        {"Repeat Count", "RPT"}, {"Clock Sync", "SYN"}, {"Retrigger", "RTG"}, {"Offset", "OFS"},
    };
    for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) {
        char *c;
        const size_t a = strlen(R[i][0]), b = strlen(R[i][1]);
        while ((c = strstr(t, R[i][0])) != 0) {
            memcpy(c, R[i][1], b);
            memmove(c + b, c + a, strlen(c + a) + 1);
        }
    }
}
static int enums;

/* the panel's words for the LXR's three letters, where a box has room for them */
static const char *const WORD[][2] = {
    {"REC", "SQR"}, {"PWM", "PULSE"}, {"NOI", "NOISE"}, {"SUP", "SAW UP"}, {"SDN", "SAW DN"}, {"XUP", "EXP UP"}, {"XDN", "EXP DN"},
    {"12", "1/12"}, {"16", "1/16"}, {"32", "1/32"}, {"NCH", "NOTCH"}, {"PEK", "PEAK"}, {"SNP", "SNAP"}, {"OFS", "OFFSET"},
    {"CLK", "CLICK"}, {"CK2", "CLICK2"}, {"TIK", "TICK"}, {"KIK", "KICK"}, {"DRP", "DROP"}, {"CLP", "CLAP"}, {"KK2", "KICK2"},
    {"SNR", "SNARE"}, {"SP2", "SNAP2"},
};

/* the value that reads as 0, from the text the desktop shows and the LXR's menu: its lowest value reads minus that */
static int centre_of(const LxrParamInfo *p)
{
    char t[32];
    if (p->isEnum || lxr_params_isTarget(p->param))
        return 0;
    lxr_params_valueText(p->param, p->min, t, sizeof t);
    return t[0] == '-' ? atoi(t + 1) + (int)p->min : 0;
}

/* the names and firmware values of an enum, once for each distinct list: its index + 1 */
static int enum_of(const LxrParamInfo *p)
{
    static char key[16384];
    char t[64];
    const int dest = lxr_params_isTarget(p->param);
    size_t n = 0;
    for (unsigned v = p->min; v <= p->max; v++) {
        lxr_params_valueText(p->param, (uint16_t)v, t, sizeof t);
        n += (size_t)snprintf(key + n, sizeof key - n, "%s|%u\n", t, dest ? lxr_params_toFirmware(p->param, (uint16_t)v) : v);
    }
    for (int i = 0; i < enums; i++)
        if (!strcmp(enum_key[i], key))
            return i + 1;
    if (enums == MAX_ENUM)
        abort();
    enum_key[enums] = strdup(key);
    printf("static const char *const EN_NAME_%d[] = {", enums);
    for (unsigned v = p->min; v <= p->max; v++) {
        lxr_params_valueText(p->param, (uint16_t)v, t, sizeof t);
        shorten(t);
        for (char *c = t; *c; c++)
            *c = *c == '"' || *c == '\\' ? ' ' : (char)toupper((unsigned char)*c);
        t[18] = 0;
        for (size_t w = 0; w < sizeof WORD / sizeof WORD[0]; w++)
            if (!strcmp(t, WORD[w][0])) {
                strcpy(t, WORD[w][1]);
                break;
            }
        if (t[0] == 'V' && t[1] >= '1' && t[1] <= '6' && !t[2])   /* the voice an LFO restarts with: lfo_retrigger() */
            strcpy(t, (const char *const[]){"D1", "D2", "D3", "SN", "CY", "HH"}[t[1] - '1']);
        printf("\"%s\",", t);
    }
    printf("};\n");
    if (dest) {
        printf("static const uint8_t EN_FW_%d[] = {", enums);
        for (unsigned v = p->min; v <= p->max; v++)
            printf("%u,", lxr_params_toFirmware(p->param, (uint16_t)v));
        printf("};\n#define EN_FW_OR_0_%d EN_FW_%d\n", enums, enums);
    } else {
        printf("#define EN_FW_OR_0_%d 0\n", enums);
    }
    return ++enums;
}

int main(void)
{
    static int en[7][NPAGE][4];
    static int used[7][NPAGE];
    int top = 0;
    printf("/* Generated by tools/gen_pages.c from the desktop parameter list. Do not edit. */\n#pragma once\n\n");
    for (int v = 0; v < 7; v++)
        for (unsigned g = 0; g < NPAGE; g++)
            for (int s = 0; s < 4; s++) {
                if (!PAGE[g].s[s].label)
                    continue;
                const LxrParamInfo *p = find(v, PAGE[g].s[s].suffix);
                if (p) {
                    used[v][g] = 1;
                    en[v][g][s] = p->isEnum ? enum_of(p) : 0;
                }
            }
    printf("\nstatic const TackEnum TACK_ENUM[] = {\n");
    for (int i = 0; i < enums; i++)
        printf("    {EN_NAME_%d, EN_FW_OR_0_%d, sizeof EN_NAME_%d / sizeof EN_NAME_%d[0]},\n", i, i, i, i);
    printf("};\n\nstatic const TackPage TACK_PAGE[TACK_VOICES][%d] = {\n", (int)NPAGE);
    int count[7] = {0};
    for (int v = 0; v < 7; v++) {
        printf("  {\n");
        for (unsigned g = 0; g < NPAGE; g++) {
            if (!used[v][g])
                continue;
            count[v]++;
            printf("    {%d, \"%s\", %d, {", PAGE[g].group, PAGE[g].title, PAGE[g].graph);
            for (int s = 0; s < 4; s++) {
                const LxrParamInfo *p = PAGE[g].s[s].label ? find(v, PAGE[g].s[s].suffix) : 0;
                if (p)
                    printf("{\"%s\", %u, %u, %d, %d, %d},", PAGE[g].s[s].label, p->param, p->max, en[v][g][s],
                           lxr_params_isTarget(p->param), centre_of(p));
                else
                    printf("{0, 0, 0, 0, 0, 0},");
            }
            printf("}},\n");
        }
        printf("  },\n");
        if (count[v] > top)
            top = count[v];
    }
    printf("};\nstatic const uint8_t TACK_PAGE_N[TACK_VOICES] = {");
    for (int v = 0; v < 7; v++)
        printf("%d,", count[v]);
    printf("};\n");
    return 0;
}
