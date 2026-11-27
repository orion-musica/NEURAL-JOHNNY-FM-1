/* NEURAL JOHNNY - the player's drum groove on the 11 panel pads, continued
 * by the groove model. Control task only. */
#pragma once
#include "johnny_port.h"
#include "johnny_synth.h"

void jg_init(uint32_t seed);
void jg_style(int mutate, int notes);        /* K3 MUTATE 0..10, K4 NOTES -5..5 */
/* A pad was hit (drum 0..10) at the transport time in c. */
int jg_pad(unsigned drum, const ms_clock *c);
/* Every control tick while the transport runs. pads_held: any pad down.
 * Returns 1 on the tick the player's groove was finished and starts looping. */
int jg_tick(const ms_clock *c, int pads_held);
int jg_have(void);                           /* a groove is looping */
int jg_recording(void);                      /* the player is tapping a new one */
const uint16_t *jg_debug(unsigned *len);
