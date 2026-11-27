/* NEURAL JOHNNY - FM-1 adapter entry points. Set NEURAL_JOHNNY to 0 to build the
 * plain base firmware from the same tree. */
#pragma once
#ifndef NEURAL_JOHNNY
#define NEURAL_JOHNNY 1
#endif
#include <stdint.h>
/* 1 = the Neural Johnny synth is mixed into the output (audio_output.c). */
extern volatile uint8_t johnny_active;
void johnny_fm1_init(void);
void johnny_fm1_control(void);
