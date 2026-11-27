/* NEURAL JOHNNY - hypnotic techno synth and step sequencer.
 * Eight layers sound from the audio interrupt; the control task (johnny_app.c)
 * only posts phrases, layer masks and live notes.
 *   ms_*                 -> control task
 *   johnny_audio_render   -> audio interrupt (adds to the FM-1 output block) */
#pragma once
#include "johnny_port.h"

enum { L_LEAD, L_KICK, L_BASS, L_HAT, L_DRONE, L_CLAP, L_ECHO, L_PERC, L_COUNT };
enum { MS_BPM = 132, MS_MAX_STEPS = 64 };

typedef struct {
    uint8_t note[MS_MAX_STEPS];   /* MIDI note per 16th, 0 = rest */
    uint8_t len;                  /* 32 or 64 (a power of two: the interrupt masks, never divides) */
    uint8_t root;                 /* tonic pitch class 0..11 */
    uint16_t bass_bits;           /* bass hits in one bar (bit n = step n) */
    uint16_t perc_bits;           /* perc hits in one bar */
    uint32_t echo_keep[2];        /* which lead steps the echo repeats (bit per step) */
} ms_phrase;

void ms_init(void);
void ms_start(void);                         /* transport from bar 0 step 0, now */
void ms_stop(void);
int ms_running(void);

/* The phrase buffer the control task may fill (never the one playing). */
ms_phrase *ms_back(void);
/* Play the back buffer: at the next bar line (from step start_pos), when the
 * playing phrase wraps (start_pos ignored), or at the very next 16th
 * (MS_ALIGN: from step start_pos + the step's place in the bar, so a phrase
 * whose answer starts at start_pos lines up with the bar wherever it joins).
 * release_hold: let the lead sound again (it is held while a new melody is
 * being played in). */
enum { MS_AT_BAR = 1, MS_AT_WRAP = 2, MS_ALIGN = 3 };
void ms_commit(unsigned when, unsigned start_pos, int release_hold);
void ms_cancel(void);                        /* forget a commit not yet taken */
int ms_pending(void);

void ms_plan_mask(uint32_t bar, uint8_t mask);   /* layers on in that bar */
void ms_mask_now(uint8_t mask);
/* Knobs: tempo (BPM), the lead's FM sound, master filter (-1 low-pass ..
 * 0 off .. +1 high-pass) and drive (0..1). */
void ms_set_tempo(float bpm);
void ms_set_lead(float ratio, float depth, float tone, float decay_s, float idx_s, float gate_steps);
void ms_set_fx(float filter, float drive);              /* layers on from the next 16th */
void ms_lead_hold(int on);                   /* silence the model's lead now */

void ms_live_on(unsigned midi);              /* the player's own notes */
void ms_live_off(unsigned midi);

/* Transport, read by the control task. */
typedef struct { uint32_t bar; unsigned step; float phase; unsigned pos; uint32_t wraps; uint32_t swaps; uint8_t mask; } ms_clock;
void ms_get_clock(ms_clock *c);
/* Envelope level (0..1) and current note of every layer, for the screen. */
float ms_level(unsigned layer);
unsigned ms_pitch(unsigned layer);

FM1_AUDIO_CODE void johnny_audio_render(int32_t *stereo_q23, unsigned frames);
