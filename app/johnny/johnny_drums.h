/* NEURAL JOHNNY - TinyML groove model (int8 MLP, about 4.4k weights).
 * For one 16th of a looping groove: the chance that each pad drum hits.
 * Control task only. */
#pragma once
#include "johnny_port.h"
/* g: hits per step (bit d = drum d), len 32 or 64; looks only at steps
 * before i (cyclically) and at which drums the first half uses. */
void jd_predict(const uint16_t *g, unsigned len, unsigned i, float probs[11]);
