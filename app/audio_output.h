/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_AUDIO_OUTPUT_H
#define FM1_AUDIO_OUTPUT_H
#include "fm1.h"
/* Reset before start or while the audio ISR is excluded. */
void fm1_output_reset(void);
/* Finish one synthesized block: master gain, USB, Bluetooth, USB recording.
 * Audio ISR only; output holds FM1_BLOCK_WORDS Q23 samples. */
FM1_AUDIO_CODE void fm1_output_mix(int32_t *output);
/* 3310 live master FX: RUMBLE 0..100, CRUSH 0..15 (0 is off), and the
 * LO CUT / HI CUT one-pole coefficients in Q12 (0 is off). */
extern volatile uint8_t fm1_fx[2];
extern volatile uint16_t fm1_cut[2];
#endif
