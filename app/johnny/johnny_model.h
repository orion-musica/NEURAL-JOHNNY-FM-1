/* NEURAL JOHNNY - TinyML melody model (int8 MLP, about 8.4k weights).
 * Tokens: 0 = rest, 1..37 = -12..+24 semitones from the tonic.
 * Runs in the control task only (never in the audio interrupt). */
#pragma once
#include "johnny_port.h"

enum { MM_TOKENS = 38, MM_REST = 0, MM_OFFSET = 13 };

/* Probabilities of every token at step i of a looping phrase toks[0..len-1]
 * (len 32 or 64). Only steps before i (cyclically) are looked at. */
void mm_predict(const uint8_t *toks, unsigned len, unsigned i, float probs[MM_TOKENS]);
