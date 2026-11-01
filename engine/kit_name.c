/* SPDX-License-Identifier: GPL-3.0-only */
/* Names for a kit, at most eight capitals. A roll picks the shape of name, then fills it in. firmware/fm1/tests/names_test.c walks
 * every name this can make.
 *
 * The English words say nothing about how a kit sounds, as a random name can't know: no metals, no IRON or HISS. The lists are strings,
 * as a word apiece in a table would take a fixed width and half as much flash again */
#include "kit_name.h"
#include <string.h>

static const char NAME_WHEN[] = "LATE EARLY LAST NEXT SPARE OLD NEW OFF ODD HALF LEFT TOP MORE EXTRA NIGHT DAWN AFTER HOUSE GUEST FIRST "
                                "NOON HOME SIDE SOME THAT YOUR OUR MY";
static const char NAME_KITCHEN[] = "TEA TOAST CRUMB JAM SOUP BREAD APRON SHELF BOIL SUGAR HONEY RIND PEEL BROTH CRUST SCONE CAKE EGG MUG FLOUR "
                                   "LOAF MILK PLUM OATS YOLK BEAN PEAR BUN PIE RYE HAM FIG LIME SODA RUSK KALE LEEK";
static const char NAME_SYLLABLES[] = "VE LU NA KO BA RI ZO TE MI DA SU NE RO LA PI GA JU TO FE SA MO RE KI DU NO LI VA ZU THE SHA KRO DRE";
static const char NAME_ENDS[] = "KNRMSL  ";          /* a space: the name ends on its vowel */
/* Swedish, as a word and a word. Names have no A with a ring or O with dots: words that read wrongly without them stay out */
static const char NAME_SV_FIRST[] = "BAS POP SUS PYS BUS TOK FET FIN FUL LAT OS EK AL ALM ASP ASK BOK EN LAV SAV ROT MYR SOL IS DIS DAG "
                                    "HAV VIK UV MES AND LO BI VIT GUL NY";
static const char NAME_SV_LAST[] = "DUNK PANG BONG PLING PLONG KLANG KNARR KNAK BRUS SURR PIP TJUT TJOFF PUFF KLAPP TAKT FUNK JAZZ RIFF LOOP GUNG "
                                   "SVAJ STUDS SNURR DANS TRALL DRAG KNAS FLUM STOJ SKOG LUND GRAN TALL BARK BARR KVIST MOSSA SVAMP KOTTE MOSSE "
                                   "BERG STEN TRAST SPARV FINK KORP KRAKA UGGLA SKATA DUVA SVALA TRANA SVAN VIPA RIPA ORRE HARE HJORT VARG SORK "
                                   "IGEL GRODA PADDA HUMLA VIND BRIS STORM REGN FROST DIMMA MOLN DAGG FORS";
static const char NAME_ODD[] = "DESCALED TEABREAK LEFTOVER WASHINUP ONESUGAR MILKLAST TEAFIRST NOSPOONS SPILLAGE WHOSMUG";

/* parts of a word, English or Swedish, that no name should have in it */
static const char NAME_BAD[] = "FUK FUC SHIT CUNT COCK DICK PISS TWAT TITS PENIS VAGIN ANUS ANAL RAPE SLUT FART NAZI HITLER NIGG NIGER NEGER "
                               "KUK FITTA HORA BAJS SKIT KNUL KISS PORN HOMO BOOB WANK";

static uint32_t name_count(const char *list)
{
    uint32_t n = 1;
    for (; *list; list++)
        n += *list == ' ';
    return n;
}

/* the word at i, round the list; its length */
static uint32_t name_word(const char *list, uint32_t i, char *to)
{
    uint32_t n = 0;
    i %= name_count(list);
    while (i)
        i -= *list++ == ' ';
    while (*list && *list != ' ')
        to[n++] = *list++;
    return n;
}

static void name_pair(const char *first, const char *second, uint32_t r, char *t)
{
    char w[8];
    const uint32_t a = name_word(first, r, t), all = name_count(second);
    uint32_t fits = 0, pick;
    for (uint32_t i = 0; i < all; i++)           /* evenly among the words that fit: taking the next that fits favours those after long ones */
        fits += a + name_word(second, i, w) <= 8u;
    pick = (r >> 8) % fits;
    for (uint32_t i = 0; i < all; i++) {
        const uint32_t n = name_word(second, i, w);
        if (a + n <= 8u && !pick--) {
            memcpy(t + a, w, n);
            return;
        }
    }
}

/* syllables, as Elite named its planets: the bits of r are three of them, how many, and the end */
#define NAME_NONSENSE_BITS 19u
static void name_nonsense(uint32_t r, char *t)
{
    const uint32_t count = (r >> 15 & 3u) ? 3u : 2u;
    const char end = NAME_ENDS[r >> 17 & 7u];
    uint32_t n = 0;
    for (uint32_t s = 0; s < count; s++) {
        char w[8];
        const uint32_t len = name_word(NAME_SYLLABLES, r >> (5u * s), w);
        if (n + len <= 8u) {
            memcpy(t + n, w, len);
            n += len;
        }
    }
    if (n < 8u)
        t[n] = end;
}

static int name_bad(const char *t)
{
    const char *bad = NAME_BAD;
    while (*bad) {
        uint32_t len = 0;
        while (bad[len] && bad[len] != ' ')
            len++;
        for (uint32_t at = 0; at + len <= 8u; at++) {
            uint32_t same = 0;
            while (same < len && t[at + same] == bad[same])
                same++;
            if (same == len)
                return 1;
        }
        bad += len;
        bad += *bad == ' ';
    }
    return 0;
}

/* eight characters, padded with spaces: half the rolls a pair of words, a third nonsense, the rest Swedish or an odd one */
static void name_shape(uint32_t shape, uint32_t r, char *t)
{
    memset(t, ' ', 8u);
    if (shape < 10u)
        name_pair(NAME_WHEN, NAME_KITCHEN, r, t);
    else if (shape < 17u)
        name_nonsense(r, t);
    else if (shape < 19u)
        name_pair(NAME_SV_FIRST, NAME_SV_LAST, r, t);
    else
        name_word(NAME_ODD, r, t);
}

void kit_name_make_up(uint32_t seed, char *t)
{
    do {
        name_shape((seed >> 24) % 20u, seed >> 6, t);
        seed = seed * 1664525u + 1013904223u;
    } while (name_bad(t));
}
