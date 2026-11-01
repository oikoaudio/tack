/* SPDX-License-Identifier: GPL-3.0-only */
/* Every name engine/kit_name.c can make: eight capitals or spaces, a letter first, and none the roll would have to throw back
 * unless it does throw it back. Prints a sample to read, as a test can't tell whether a name is any good */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../engine/kit_name.c"

static int failed;

static void fail(const char *why, const char *t)
{
    printf("FAIL %s: %.8s\n", why, t);
    failed = 1;
}

static void check(const char *t, int thrown_back_is_fine)
{
    if (t[0] < 'A' || t[0] > 'Z')
        fail("does not start with a letter", t);
    for (uint32_t i = 0; i < 8u; i++)
        if ((t[i] < 'A' || t[i] > 'Z') && t[i] != ' ')
            fail("a character the name page can't show", t);
    if (!thrown_back_is_fine && name_bad(t))
        fail("a word list has a bad word in it", t);
}

/* the words in a list, as the page shows them */
static void check_list(const char *list, uint32_t longest)
{
    for (uint32_t i = 0, n = name_count(list); i < n; i++) {
        char t[9] = "        ";
        if (name_word(list, i, t) > longest)
            fail("too long", t);
        check(t, 0);
    }
}

int main(void)
{
    uint32_t pairs = 0, nonsense = 0, thrown = 0, swedish = 0, thrown_sv = 0;
    const uint32_t first = name_count(NAME_WHEN), second = name_count(NAME_KITCHEN);
    char t[9];
    t[8] = 0;
    if (name_count(NAME_SYLLABLES) != 32u)
        fail("the syllables are five bits each", NAME_SYLLABLES);
    if (name_count(NAME_ENDS) != 3u || sizeof NAME_ENDS != 9u)
        fail("the ends are three bits", NAME_ENDS);
    check_list(NAME_WHEN, 5u);
    check_list(NAME_KITCHEN, 5u);
    check_list(NAME_SV_FIRST, 3u);
    check_list(NAME_SV_LAST, 5u);
    check_list(NAME_ODD, 8u);
    for (uint32_t a = 0; a < first; a++)
        for (uint32_t b = 0; b < second; b++) {
            char w[8];
            const uint32_t la = name_word(NAME_WHEN, a, t), lb = name_word(NAME_KITCHEN, b, w);
            if (la + lb > 8u)
                continue;
            memset(t, ' ', 8u);
            name_word(NAME_WHEN, a, t);
            memcpy(t + la, w, lb);
            check(t, 0);
            pairs++;
        }
    for (uint32_t r = 0; r < 1u << NAME_NONSENSE_BITS; r++) {
        memset(t, ' ', 8u);
        name_nonsense(r, t);
        check(t, 1);
        if (name_bad(t))
            thrown++;
        nonsense++;
    }
    {   /* Swedish: every first word with every last, as all of them fit. A rude one is the roll's to throw back */
        const uint32_t n = name_count(NAME_SV_FIRST), m = name_count(NAME_SV_LAST);
        for (uint32_t a = 0; a < n; a++)
            for (uint32_t b = 0; b < m; b++) {
                char w[8];
                memset(t, ' ', 8u);
                {
                    const uint32_t la = name_word(NAME_SV_FIRST, a, t), lb = name_word(NAME_SV_LAST, b, w);
                    memcpy(t + la, w, lb);
                }
                check(t, 1);
                thrown_sv += (uint32_t)name_bad(t);
                swedish++;
            }
    }
    {   /* every pair comes up, about as often as the others of its first word */
        static uint32_t seen[64][64];
        uint32_t seed = 99u, low = 0xFFFFFFFFu, high = 0;
        for (uint32_t i = 0; i < 400000u; i++) {
            const uint32_t r = seed >> 6, a = r % first;
            char w[8], u[8];
            const uint32_t la = name_word(NAME_WHEN, a, u);
            memset(t, ' ', 8u);
            name_pair(NAME_WHEN, NAME_KITCHEN, r, t);
            for (uint32_t b = 0; b < second; b++) {
                const uint32_t lb = name_word(NAME_KITCHEN, b, w);
                if (la + lb <= 8u && (la + lb == 8u || t[la + lb] == ' ') && !memcmp(t + la, w, lb))
                    seen[a][b]++;
            }
            seed = seed * 1664525u + 1013904223u + i;
        }
        for (uint32_t a = 0; a < first; a++)
            for (uint32_t b = 0; b < second; b++) {
                char w[8], u[8];
                if (name_word(NAME_WHEN, a, u) + name_word(NAME_KITCHEN, b, w) <= 8u) {
                    low = seen[a][b] < low ? seen[a][b] : low;
                    high = seen[a][b] > high ? seen[a][b] : high;
                }
            }
        if (!low || high > low * 8u)
            fail("the pairs come up unevenly", "");
    }
    {   /* the roll itself: it never hands back what it should throw away, and the shapes come in the shares it says */
        uint32_t seed = 12345u, shape[4] = {0, 0, 0, 0};
        for (uint32_t i = 0; i < 200000u; i++) {
            const uint32_t s = (seed >> 24) % 20u;
            memset(t, 0, 8u);
            kit_name_make_up(seed, t);
            check(t, 0);
            shape[s < 10u ? 0 : s < 17u ? 1 : s < 19u ? 2 : 3]++;
            seed = seed * 1664525u + 1013904223u + i;
        }
        for (uint32_t i = 0; i < 4u; i++) {
            const uint32_t want = i == 0u ? 50u : i == 1u ? 35u : i == 2u ? 10u : 5u;
            const uint32_t got = shape[i] / 2000u;
            if (got + 2u < want || got > want + 2u)
                fail("a shape off its share", "");
        }
        seed = 2026u;
        for (uint32_t i = 0; i < 48u; i++) {
            kit_name_make_up(seed, t);
            printf("%s%s", t, i % 6u == 5u ? "\n" : "  ");
            seed = seed * 1664525u + 1013904223u + 977u * i;
        }
    }
    printf("%u word pairs, %u nonsense rolls, %u thrown back; %u Swedish pairs, %u thrown back\n", pairs, nonsense, thrown, swedish, thrown_sv);
    puts(failed ? "FAILED" : "names passed");
    return failed;
}
