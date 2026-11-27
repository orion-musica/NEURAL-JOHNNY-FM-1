/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_HYPNO_H
#define FM1_HYPNO_H
#include "instrument.h"
/* 3310: eight hypnotic-techno layers, one per track, each with its own
 * preset list (presets.c). Build writes one track's sound and 64 steps. */
enum { HYP_LAYERS = 8 };
unsigned hyp_count(unsigned layer);
const char *hyp_name(unsigned layer);
/* Replace the working project with the layer sounds and no notes (undoable). */
int groove_project_new(void);
void hyp_build(unsigned layer, unsigned index, struct groove_preset *p, struct groove_step steps[64]);
#endif
