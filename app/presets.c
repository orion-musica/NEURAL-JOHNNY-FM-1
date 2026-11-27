/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "instrument.h"
#include "fm_patch.h"
#include "drum808.h"
#include "hypno.h"
#include <string.h>

/* 3310 hypnotic layers. Each of the eight tracks is one layer of a hypnotic
 * techno track, in the order a live set brings them in:
 *   1 ATMOS  DX7 drone      2 KICK  808        3 RUMBLE 303 sub
 *   4 HATS   808 offbeats   5 PERC  pluck poly 6 TOPS   808 ride/shaker
 *   7 SEQ    DX7 loop       8 CLAP  808
 * Every layer has its own preset list. A preset is a small recipe (sound and
 * pattern) that expands into the normal editable project format, so the same
 * code builds the sixteen factory projects. Everything is in A minor.
 * Drum masks read from bit 0 = first sixteenth. */
enum { ROOT = 45 }; /* A2 */
static const uint8_t counts[HYP_LAYERS] = {16, 16, 14, 16, 16, 14, 16, 12};
static const char names[HYP_LAYERS][7] = {"ATMOS", "KICK", "RUMBLE", "HATS", "PERC", "TOPS", "SEQ", "CLAP"};
unsigned hyp_count(unsigned layer) { return layer < HYP_LAYERS ? counts[layer] : 0; }
const char *hyp_name(unsigned layer) { return layer < HYP_LAYERS ? names[layer] : "---"; }

/* ---- 808 kit ------------------------------------------------------------ */
/* Stored layout: 11 sound selectors, then two complete parameter sets (one per
 * Sound variant). Offsets of each part inside one set, from drum808.c. */
enum { BD, SD, LT, MT, HT, RS, CP, CB, CY, OH, CH, KIT, SET = 99 };
static const uint8_t part_at[12] = {0, 8, 18, 27, 36, 45, 54, 64, 72, 80, 88, 96};
static void kv(struct groove_preset *p, unsigned part, unsigned i, unsigned value)
{
    p->data[D8_NUM + part_at[part] + i] = p->data[D8_NUM + SET + part_at[part] + i] = (uint8_t)value;
}
static void kit(struct groove_preset *p)
{
    unsigned at = D8_NUM;
    for (unsigned variant = 0; variant < 2; ++variant)
        for (unsigned part = 0; part <= D8_NUM; ++part)
            for (int i = 0; i < drum808_nparams((int)part); ++i) {
                const fm1_param_t *d = drum808_param((int)part, i);
                p->data[at++] = (uint8_t)(strcmp(d->name, "Sound") ? d->def : variant);
            }
    /* Each layer plays one or two parts: quiet the rest of the kit. */
    for (unsigned part = 0; part < D8_NUM; ++part) kv(p, part, 0, 0);
}
/* Kit settings as (part, parameter, value) triples ending in 0xff; a value
 * of 200 + n takes byte n of the preset's own sound row. */
enum { V0 = 200, V1, V2, V3, V4, V5 };
static void kset(struct groove_preset *p, const uint8_t *list, const uint8_t *row)
{
    for (; *list != 0xffU; list += 3) kv(p, list[0], list[1], list[2] >= V0 ? row[list[2] - V0] : list[2]);
}
static void lane(struct groove_step *s, unsigned part, uint16_t mask, unsigned accent)
{
    for (unsigned i = 0; i < 64; ++i)
        if ((mask >> (i & 15U)) & 1U) {
            s[i].drums |= (uint16_t)(1U << part);
            unsigned v = (i & 3U) == 2U ? 108U : (i & 3U) ? 64U + accent : 96U;
            if (v > s[i].velocity) s[i].velocity = (uint8_t)v;
        }
}
/* Polymeter: a hit every `period` sixteenths over all 64 steps. */
static void poly(struct groove_step *s, unsigned part, unsigned period, unsigned phase)
{
    for (unsigned i = phase; i < 64; i += period) {
        s[i].drums |= (uint16_t)(1U << part);
        if (s[i].velocity < 84) s[i].velocity = 84;
    }
}

/* ---- Melodic steps ------------------------------------------------------ */
static void note(struct groove_step *s, unsigned pitch, unsigned velocity, unsigned gate, unsigned chord, unsigned tie)
{
    s->note = (uint8_t)pitch; s->velocity = (uint8_t)velocity;
    s->drums = (uint16_t)(chord | (gate << 5) | (tie << 12));
}
/* A cycle of `period` steps (hits in `mask`), with four notes rotating per
 * hit: two loops of different length drift against each other. */
static void cycle(struct groove_step *s, unsigned period, unsigned mask, unsigned notes, unsigned base, unsigned gate)
{
    unsigned hit = 0;
    for (unsigned i = 0; i < 64; ++i)
        if ((mask >> (i % period)) & 1U) {
            unsigned n = (notes >> (4U * (hit++ & 3U))) & 15U;
            if (n != 1U) note(&s[i], base + n, (i % period) ? 92 : 112, gate, 0, 0);
        }
}

/* ---- Layer recipes ------------------------------------------------------ */
/* 1 ATMOS: DX7 factory program, chord, movement of the fourth bar. */
static const uint8_t atmos[16][3] = {
    {3, 0, 0}, {7, 2, 0}, {104, 0, 1}, {66, 10, 0}, {11, 0, 2}, {99, 0, 0}, {67, 2, 1}, {15, 10, 3},
    {124, 0, 0}, {19, 2, 2}, {96, 10, 0}, {74, 0, 1}, {23, 0, 3}, {75, 2, 0}, {27, 10, 2}, {31, 0, 1}};
/* 2 KICK: tone, decay, tune, attack, drive, dist, extra hits. */
static const uint8_t kicks[16][7] = {
    {30, 95, 52, 20, 0, 0, 0}, {50, 70, 60, 50, 12, 2, 0}, {24, 122, 46, 14, 0, 0, 1}, {62, 48, 66, 72, 18, 1, 0},
    {36, 104, 50, 30, 64, 0, 0}, {42, 82, 55, 40, 44, 5, 2}, {46, 76, 58, 34, 34, 6, 0}, {20, 112, 40, 10, 8, 2, 3},
    {82, 58, 64, 104, 0, 0, 0}, {56, 66, 62, 62, 84, 3, 1}, {26, 86, 56, 10, 0, 0, 2}, {44, 92, 52, 26, 52, 4, 0},
    {34, 98, 54, 22, 38, 2, 3}, {50, 72, 60, 46, 72, 1, 0}, {28, 118, 48, 18, 20, 2, 2}, {62, 88, 50, 84, 96, 5, 1}};
/* 3 RUMBLE: 303 cutoff, reso, envmod, decay, drive, drive type, note, mask. */
static const uint8_t rumble[14][8] = {
    {26, 30, 20, 34, 0, 0, 33, 0xee}, {34, 46, 30, 26, 40, 1, 33, 0x66}, {20, 20, 12, 50, 0, 0, 28, 0x44},
    {40, 60, 44, 22, 70, 2, 33, 0xcc}, {30, 36, 26, 30, 24, 1, 31, 0xaa}, {24, 26, 18, 44, 0, 0, 33, 0x22},
    {46, 70, 52, 18, 90, 2, 33, 0xee}, {28, 40, 24, 36, 30, 1, 36, 0x6e}, {22, 18, 10, 56, 10, 1, 28, 0x88},
    {36, 54, 38, 24, 56, 1, 33, 0xe6}, {32, 44, 30, 28, 0, 0, 31, 0x77}, {42, 64, 48, 20, 80, 2, 33, 0x66},
    {26, 32, 22, 40, 20, 1, 33, 0xea}, {30, 50, 34, 26, 44, 2, 28, 0xee}};
/* 4 HATS: closed mask, open mask, closed decay, open decay, tune, drive, dist. */
static const uint16_t hat_masks[16][2] = {
    {0x0000, 0x4444}, {0x4444, 0x0000}, {0xffff, 0x0000}, {0xbbbb, 0x4444}, {0x5555, 0x0000}, {0x6666, 0x0000},
    {0xeeee, 0x0000}, {0x4444, 0x4000}, {0xaaaa, 0x0000}, {0x7777, 0x0000}, {0x4444, 0x8080}, {0xdddd, 0x2222},
    {0x1111, 0x4444}, {0xfbfb, 0x0404}, {0x0000, 0x4444}, {0x6666, 0x8888}};
static const uint8_t hat_sound[16][5] = {
    {40, 60, 64, 0, 0}, {30, 70, 70, 0, 0}, {22, 60, 66, 0, 0}, {28, 74, 64, 10, 2}, {36, 60, 58, 0, 0},
    {26, 60, 72, 30, 6}, {24, 60, 64, 0, 0}, {34, 90, 60, 0, 0}, {20, 60, 76, 20, 1}, {28, 60, 62, 0, 0},
    {32, 96, 68, 40, 2}, {30, 66, 64, 0, 0}, {44, 78, 56, 0, 0}, {18, 50, 70, 60, 5}, {40, 104, 52, 50, 3},
    {26, 84, 66, 24, 6}};
/* 5 PERC and 7 SEQ: period, hit mask, four notes (-100 rest), sound bytes. */
/* Notes: four offsets in nibbles, first in the low nibble; 1 is a rest. */
/* SEQ cd: reverb | delay << 4. PERC cd: tune 12 x (1 + bits 0-1) | release / 2 << 2. */
struct line { uint16_t mask, notes; uint8_t period, a, b, cd; };
static const struct line perc[16] = {
    {0x1, 0xa370, 3, 34, 96, 41}, {0x5, 0x307c, 5, 50, 110, 61},
    {0x9, 0x5700, 7, 26, 80, 36}, {0x9, 0x7ca3, 6, 40, 120, 49},
    {0x3, 0xc710, 3, 30, 70, 42}, {0x1, 0xa037, 5, 60, 100, 81},
    {0x2492, 0xa730, 16, 36, 90, 49}, {0x25, 0x37ac, 7, 24, 116, 34},
    {0x49, 0x7c70, 9, 44, 84, 60}, {0x3, 0x0a31, 5, 32, 104, 41},
    {0x1, 0x7afc, 3, 20, 127, 30}, {0x125, 0x7530, 11, 52, 90, 73},
    {0x21, 0x0307, 6, 28, 100, 44}, {0x1248, 0x037a, 16, 40, 108, 53},
    {0x3, 0x70c0, 7, 34, 76, 41}, {0x249, 0xea73, 13, 46, 96, 62}};
static const struct line seq[16] = {
    {0x0404, 0x0000, 16, 112, 12, 18}, {0x1, 0xa37c, 3, 117, 6, 51},
    {0x4924, 0x3730, 16, 100, 9, 34}, {0x5, 0xa7c0, 5, 105, 6, 33},
    {0x0808, 0x0000, 16, 101, 12, 52}, {0x13, 0x3ca7, 7, 118, 6, 34},
    {0x2222, 0x0300, 16, 113, 8, 17}, {0x5, 0x57ac, 6, 120, 7, 51},
    {0x0444, 0xa070, 16, 35, 9, 16}, {0x2, 0xc730, 3, 98, 5, 34},
    {0x9090, 0x7010, 16, 106, 8, 18}, {0x9, 0xea73, 5, 112, 6, 51},
    {0x0410, 0x0707, 16, 103, 10, 33}, {0x25, 0x37a0, 9, 117, 6, 50},
    {0xa4a4, 0x0c00, 16, 107, 6, 1}, {0x5, 0x07ca, 7, 100, 7, 35}};
/* 6 TOPS: ride mask, closed-hat mask, aux mask, ride decay, ride tune, aux. */
static const uint16_t top_masks[14][3] = {
    {0x4444, 0x0000, 0x0000}, {0x1111, 0x0000, 0x0000}, {0x5555, 0x0000, 0x0000}, {0xffff, 0x0000, 0x0000},
    {0x0000, 0x0000, 0xffff}, {0x4444, 0x0000, 0xbbbb}, {0x4444, 0x0000, 0x0000}, {0x0000, 0xeeee, 0x4444},
    {0x5555, 0x0000, 0x2222}, {0x1111, 0xaaaa, 0x0000}, {0x0000, 0x0000, 0x6666}, {0x4444, 0xffff, 0x0000},
    {0x0000, 0x0000, 0x0000}, {0x5555, 0x0000, 0xaaaa}};
static const uint8_t top_sound[14][3] = {
    {80, 64, 0}, {110, 60, 0}, {70, 70, 0}, {60, 66, 0}, {70, 64, 1}, {90, 62, 1}, {120, 56, 0},
    {70, 64, 1}, {84, 68, 2}, {96, 60, 0}, {70, 64, 2}, {76, 72, 0}, {100, 58, 3}, {66, 64, 1}};
/* 8 CLAP: clap mask, snare mask, rim mask, clap decay, clap tune, rev, dly. */
static const uint16_t clap_masks[12][3] = {
    {0x1010, 0, 0}, {0x1000, 0, 0}, {0, 0x1010, 0}, {0x1010, 0, 0}, {0x1010, 0x1010, 0}, {0x1010, 0, 0x4444},
    {0x1090, 0, 0}, {0, 0, 0x0000}, {0x1010, 0, 0x8282}, {0x0010, 0x1000, 0}, {0x5010, 0, 0}, {0x1010, 0, 0}};
static const uint8_t clap_sound[12][4] = {
    {64, 64, 24, 0}, {90, 58, 60, 20}, {64, 64, 20, 0}, {70, 62, 40, 50}, {60, 66, 30, 0}, {64, 64, 24, 0},
    {56, 70, 30, 20}, {64, 64, 30, 30}, {72, 60, 50, 0}, {80, 56, 70, 40}, {50, 72, 20, 10}, {110, 50, 96, 60}};

void hyp_build(unsigned layer, unsigned index, struct groove_preset *p, struct groove_step s[64])
{
    memset(p, 0, sizeof *p);
    memset(s, 0, 64 * sizeof *s);
    index %= counts[layer % HYP_LAYERS];
    p->modified = 1; p->volume = 100; p->sequence_pages = 1; /* 16 steps */
    p->controls.envelope[2] = 100;
    if (layer == 0 || layer == 6) {
        /* DX7 voices come from the M-VAVE factory bank, so the recipes stay tiny. */
        const uint8_t *a = atmos[index];
        const struct line *l = &seq[index];
        unsigned program = layer ? l->a : a[0];
        p->type = GROOVE_DX7; p->program = (uint8_t)program; p->modified = 0;
        groove_fm_factory(p->data, program);
        if (layer) {
            cycle(s, l->period, l->mask, l->notes, ROOT + 12U, l->b);
            p->volume = 84; p->data[209] = (uint8_t)(l->cd * 3U); /* each nibble x3 */
            if (l->period != 16) p->sequence_pages = 0;
        } else {
            static const int8_t move[4][4] = {{0, 0, 0, 0}, {0, 0, 0, -2}, {0, 0, 3, 0}, {0, 5, 0, 3}};
            for (unsigned bar = 0; bar < 4; ++bar)
                note(&s[bar * 16U], (unsigned)(ROOT + 12 + move[a[2]][bar]), 96, 15, a[1], 1);
            p->volume = 74; p->data[209] = 12U + 16U * 5U; p->sequence_pages = 0;
        }
    } else if (layer == 2) {
        const uint8_t *r = rumble[index];
        static const uint8_t base[BASS303_NPARAMS] = {0, 0, 0, 0, 30, 0, 64, 110, 0, 0, 10, 7};
        p->type = GROOVE_303; p->controls.mono = 1;
        memcpy(p->data, base, BASS303_NPARAMS);
        memcpy(p->data, r, 4);
        p->data[BASS303_DRIVE] = r[4]; p->data[BASS303_DRVTYPE] = r[5];
        for (unsigned i = 0; i < 16; ++i)
            if ((r[7] >> (i & 7U)) & 1U)
                for (unsigned bar = 0; bar < 4; ++bar)
                    note(&s[bar * 16U + i], r[6], (i & 3U) == 2U ? 112 : 88, 7, 0, 0);
        p->data[209] = 2U;
    } else if (layer == 4) {
        const struct line *l = &perc[index];
        p->type = GROOVE_PLUCK; p->volume = 92;
        p->data[0] = l->a; p->data[1] = l->b; p->data[2] = (uint8_t)(12U * (1U + (l->cd & 3U))); p->data[3] = (uint8_t)((l->cd >> 2) * 2U);
        cycle(s, l->period, l->mask, l->notes, ROOT + 12U, 3);
        p->data[209] = (uint8_t)(4U + 16U * (index % 3U) * 3U);
        if (l->period != 16) p->sequence_pages = 0;
    } else {
        p->type = GROOVE_808;
        kit(p);
        kv(p, KIT, 0, 110);
        if (layer == 1) {
            const uint8_t *k = kicks[index];
            static const uint16_t extra[4] = {0, 0x8000, 0x4000, 0x0400};
            static const uint8_t set[] = {BD, 0, 120, BD, 1, V0, BD, 2, V1, BD, 3, V2, BD, 4, V3, BD, 5, V4, BD, 6, V5, 0xff};
            kset(p, set, k);
            lane(s, BD, 0x1111, 0);
            for (unsigned i = 0; i < 64; i += 4) s[i].velocity = 120;
            for (unsigned i = 0; i < 64; ++i)
                if ((extra[k[6]] >> (i & 15U)) & 1U && (i & 32U)) { s[i].drums = 1; s[i].velocity = 70; }
        } else if (layer == 3) {
            const uint8_t *h = hat_sound[index];
            static const uint8_t set[] = {CH, 0, 96, OH, 0, 88, CH, 2, V0, OH, 1, V1, CH, 1, V2, OH, 2, V2,
                CH, 3, V3, CH, 4, V4, OH, 3, V3, OH, 4, V4, OH, 5, 20, 0xff};
            kset(p, set, h);
            lane(s, CH, hat_masks[index][0], 0);
            lane(s, OH, hat_masks[index][1], 8);
            if (index == 14) poly(s, CH, 3, 1);
        } else if (layer == 5) {
            const uint8_t *t = top_sound[index];
            static const uint8_t set[] = {CY, 0, 84, CY, 1, V0, CY, 2, V1, CY, 5, 30, CY, 6, 10, CH, 0, 60, CH, 2, 34, CB, 0, 58, 0xff};
            kset(p, set, t);
            /* Aux: maracas (clap variant), cowbell or conga. */
            static const uint8_t aux[4] = {CP, CP, CB, LT};
            unsigned x = aux[t[2]];
            if (x == CP) p->data[CP] = 1; /* MARAC */
            if (x == LT) p->data[LT] = 1; /* CONGA */
            if (x != CB) kv(p, x, 0, 72);
            lane(s, CY, top_masks[index][0], 0);
            lane(s, CH, top_masks[index][1], 0);
            lane(s, x, top_masks[index][2], 0);
            if (index == 12) { poly(s, CB, 3, 2); poly(s, LT, 5, 0); }
            if (index == 6 || index == 10) poly(s, CB, 7, 3);
        } else {
            const uint8_t *c = clap_sound[index];
            static const uint8_t set[] = {CP, 0, 92, SD, 0, 80, RS, 0, 70, CP, 2, V0, CP, 1, V1, SD, 1, V1,
                CP, 7, V2, CP, 8, V3, SD, 7, V2, SD, 8, V3, RS, 6, 30, RS, 7, 20, 0xff};
            kset(p, set, c);
            lane(s, CP, clap_masks[index][0], 0);
            lane(s, SD, clap_masks[index][1], 0);
            lane(s, RS, clap_masks[index][2], 0);
            if (index == 7) poly(s, RS, 5, 2);
        }
    }
    /* Loops that repeat every bar play as 16 steps; polymeters need all 64. */
    if (memcmp(s, s + 16, 48 * sizeof *s)) p->sequence_pages = 0;
}

/* ---- Factory projects: sixteen unsettling hypnotic sets ----------------- */
/* Each set picks one preset per layer (eight nibbles), a tempo, a name from
 * two words, and shifts that set ATMOS and SEQ against the A minor floor:
 * +1 is the Phrygian half step, +6 the tritone, -1 a sour leading tone. */
enum { BLACK, PULSE, STATIC, NERVE, HOLLOW, CELL, DREAD, RUST, ALARM, GHOST, VOID, HUSH };
static const char words[12][7] = {"BLACK", "PULSE", "STATIC", "NERVE", "HOLLOW", "CELL",
                                   "DREAD", "RUST", "ALARM", "GHOST", "VOID", "HUSH"};
struct set { uint8_t pick[4], tempo, name, shift; }; /* tempo: bpm - 126 | swing / 2 << 4 */
#define PICK(a, k, r, h, p, t, s, c) {(a) | (k) << 4, (r) | (h) << 4, (p) | (t) << 4, (s) | (c) << 4}
#define SHIFT(a, s) (uint8_t)(((a) & 15) | ((s) & 15) << 4)
static const struct set sets[GROOVE_FACTORY_PROJECTS] = {
    {PICK(2, 4, 3, 13, 7, 12, 4, 11), 6, BLACK << 4 | PULSE, SHIFT(1, 0)},
    {PICK(8, 9, 6, 15, 10, 6, 12, 7), 42, STATIC << 4 | NERVE, SHIFT(0, 6)},
    {PICK(5, 2, 13, 14, 3, 10, 0, 3), 2, HOLLOW << 4 | CELL, SHIFT(6, 1)},
    {PICK(10, 15, 11, 5, 11, 3, 7, 9), 56, DREAD << 4 | PULSE, SHIFT(-1, 0)},
    {PICK(6, 5, 9, 10, 15, 9, 14, 1), 12, RUST << 4 | ALARM, SHIFT(1, 1)},
    {PICK(13, 7, 3, 3, 2, 13, 3, 6), 32, GHOST << 4 | STATIC, SHIFT(0, -1)},
    {PICK(11, 12, 6, 13, 13, 12, 4, 11), 4, VOID << 4 | NERVE, SHIFT(6, 6)},
    {PICK(2, 2, 13, 14, 7, 6, 12, 7), 49, HUSH << 4 | ALARM, SHIFT(1, 6)},
    {PICK(8, 4, 11, 15, 10, 10, 0, 3), 9, BLACK << 4 | CELL, SHIFT(-1, 1)},
    {PICK(5, 9, 9, 5, 3, 3, 7, 9), 37, HOLLOW << 4 | DREAD, SHIFT(6, 0)},
    {PICK(10, 15, 3, 10, 11, 9, 14, 1), 11, RUST << 4 | GHOST, SHIFT(0, 1)},
    {PICK(6, 7, 6, 3, 15, 13, 3, 6), 55, CELL << 4 | ALARM, SHIFT(1, -2)},
    {PICK(13, 12, 13, 13, 2, 12, 4, 11), 3, STATIC << 4 | HUSH, SHIFT(-1, 6)},
    {PICK(11, 5, 11, 14, 13, 6, 12, 7), 40, GHOST << 4 | PULSE, SHIFT(6, 1)},
    {PICK(2, 4, 9, 15, 7, 10, 0, 3), 14, DREAD << 4 | VOID, SHIFT(1, 1)},
    {PICK(8, 9, 3, 5, 10, 3, 7, 9), 50, HOLLOW << 4 | RUST, SHIFT(-1, -1)}};
const char *groove_factory_name(unsigned index)
{
    static char name[13];
    if (index >= GROOVE_FACTORY_PROJECTS) return NULL;
    unsigned at = 0, w = sets[index].name;
    memset(name, ' ', 12);
    for (unsigned k = 0; k < 2; ++k, ++at)
        for (const char *c = words[k ? w & 15U : w >> 4]; *c; ++c) name[at++] = *c;
    name[12] = 0;
    return name;
}
int groove_factory_build(unsigned index, struct groove_project *p)
{
    if (!p || index >= GROOVE_FACTORY_PROJECTS) return GROOVE_PROJECT_INVALID;
    const struct set *set = &sets[index];
    memset(p, 0, sizeof *p);
    memcpy(p->name, groove_factory_name(index), 12);
    p->tempo = (uint16_t)(126U + (set->tempo & 15U));
    p->effects[0] = 92; p->effects[1] = GROOVE_DELAY_SYNC + 7U; /* 1/8 dotted */
    p->effects[2] = 52; p->effects[3] = 84;
    groove_sequence_defaults(&p->sequence);
    p->sequence.swing = (uint8_t)((set->tempo >> 4) * 2U);
    groove_project_arp_defaults(p);
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        hyp_build(t, (set->pick[t >> 1] >> (4U * (t & 1U))) & 15U, &p->preset[t], p->step[t]);
        if (t != 0 && t != 6) continue;
        /* Signed nibble: ATMOS low, SEQ high. */
        int shift = (int)((set->shift >> (t ? 4U : 0U)) & 15U);
        if (shift > 7) shift -= 16;
        for (unsigned i = 0; i < 64; ++i)
            if (p->step[t][i].velocity) p->step[t][i].note = (uint8_t)(p->step[t][i].note + shift);
    }
    return groove_project_valid(p) ? 0 : GROOVE_PROJECT_INVALID;
}
