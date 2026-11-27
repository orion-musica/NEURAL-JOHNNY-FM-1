/* NEURAL JOHNNY - neural light waves over all 41 LEDs (27 keys, 14 buttons).
 * Every hit of the music fires impulses that run along the keys like signals
 * along an axon and now and then branch over to the button row. */
#pragma once
#include "johnny_port.h"
void jl_init(void);
/* Fire impulses from what the synth played since the last call and move them.
 * playing: 0 on the start screen (slow idle sparks), 1 while music runs.
 * bar / step: the transport. Calls johnny_hal_led only for LEDs that change. */
/* base: LEDs to keep on (bit n = LED n), e.g. OCT- while sounds are muted. */
void jl_tick(uint32_t now, int playing, uint32_t bar, unsigned step, uint32_t key_down, uint64_t base);
