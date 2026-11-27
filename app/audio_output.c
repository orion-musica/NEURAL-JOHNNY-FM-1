/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "audio_output.h"
#include "fm1_engine_control.h"
#include "fm1_audio_io.h"
#include "johnny/johnny_fm1.h"
#include "johnny/johnny_synth.h"

static unsigned output_gain;
/* 3310 live master FX, set by the ARRANGE page (see audio_output.h). */
volatile uint8_t fm1_fx[2];
volatile uint16_t fm1_cut[2];
static int32_t usb_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));
static int16_t bluetooth_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));

void fm1_output_reset(void) { output_gain = 0; }
static inline __attribute__((always_inline)) int32_t limit(int64_t value)
{
    /* Bias signed 24-bit PCM into [0, 2^24). Unsigned arithmetic also
     * defines the check at the int64 limits, without two wide compares. */
    if (((uint64_t)value + 8388608U) >> 24)
        return value < 0 ? -8388608 : 8388607;
    return (int32_t)value;
}
static int32_t hi_state[2][2], lo_state[2][2], crush_held[2];
static unsigned crush_count;
/* RUMBLE: the low band (mostly the kick) smeared by two combs at 11 kHz,
 * ducked by the dry low end so it rolls between the kicks. */
enum { COMB_A = 223, COMB_B = 307 };
static int16_t comb_a[COMB_A], comb_b[COMB_B];
static unsigned comb_ia, comb_ib, comb_phase;
static int32_t low1, low2, tail, tail_lp, duck, comb_sum;
/* 32-bit only: (x - y) >> 8 keeps 17 bits, times a Q12 coefficient. */
FM1_AUDIO_CODE static int32_t pole(int32_t *y, int32_t x, int32_t a)
{
    *y += ((x - *y) >> 8) * a >> 4;
    return *y;
}
static inline __attribute__((always_inline)) int16_t sat16(int32_t v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}
static inline __attribute__((always_inline)) int32_t sat24(int32_t v)
{
    return v > 8388607 ? 8388607 : v < -8388608 ? -8388608 : v;
}
FM1_AUDIO_CODE static void live_fx(int32_t *output)
{
    unsigned rumble = fm1_fx[0], crush = fm1_fx[1] & 15U;
    int32_t lo = fm1_cut[0], hi = fm1_cut[1];
    /* No division in the audio ISR: hold ~ 1 + crush^2/40, shift ~ 9 + crush*2/3. */
    unsigned hold = 1U + ((crush * crush * 13U) >> 9), shift = 9U + ((crush * 43U) >> 6);
    int32_t feedback = 23500 + (int32_t)rumble * 82, mask = ~(int32_t)((1U << shift) - 1U); /* Q15 */
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; i += 2) {
        int32_t wet = 0;
        if (rumble) {
            int32_t low = pole(&low2, pole(&low1, (output[i] + output[i + 1]) >> 1, 69), 69);
            int32_t level = low < 0 ? -low : low;
            duck += level > duck ? (level - duck) >> 4 : -(duck >> 11);
            comb_sum += low >> 2;
            if (++comb_phase == 4U) {
                int32_t in = comb_sum >> 8, a = comb_a[comb_ia], b = comb_b[comb_ib]; /* Q15 */
                comb_a[comb_ia] = sat16(in + ((a * feedback) >> 15));
                comb_b[comb_ib] = sat16(in + ((b * feedback) >> 15));
                if (++comb_ia == COMB_A) comb_ia = 0;
                if (++comb_ib == COMB_B) comb_ib = 0;
                tail = (a + b) << 7;
                comb_phase = 0; comb_sum = 0;
            }
            wet = pole(&tail_lp, tail, 52);
            int32_t gain = 4096 - (duck >= 2097152 ? 3600 : ((duck >> 9) * 3600) >> 12); /* Q12 */
            wet = ((((wet >> 8) * gain) >> 5) * (int32_t)rumble * 5) >> 8; /* about rumble/100, no division */
        }
        if (crush && ++crush_count >= hold) crush_count = 0;
        for (unsigned c = 0; c < 2; ++c) {
            int32_t x = sat24(output[i + c] + wet);
            if (crush) {
                if (!crush_count) crush_held[c] = x & mask;
                x = crush_held[c];
            }
            if (lo) x -= pole(&lo_state[c][1], pole(&lo_state[c][0], x, lo), lo);
            if (hi) x = pole(&hi_state[c][1], pole(&hi_state[c][0], x, hi), hi);
            else hi_state[c][0] = hi_state[c][1] = x;
            output[i + c] = x;
        }
    }
}
FM1_AUDIO_CODE void fm1_output_mix(int32_t *output)
{
    if (!output) return;
#if NEURAL_JOHNNY
    if (johnny_active) johnny_audio_render(output, FM1_BLOCK_FRAMES);   /* Neural Johnny: model lead + 7 layers */
#endif
    if (fm1_fx[0] | fm1_fx[1] | fm1_cut[0] | fm1_cut[1]) live_fx(output);
    unsigned target = fm1_engine_output_level();
    if (target > 127U) target = 127U;
    int delta = (int)target - (int)output_gain;
    output_gain = (unsigned)((int)output_gain + (delta > 8 ? 8 : delta < -8 ? -8 : delta));

    uint16_t usb_gain[2] = {0, 0};
    int usb_frames = fm1_usb_audio_read(usb_pcm, FM1_BLOCK_FRAMES, usb_gain);
    unsigned left = ((unsigned)usb_gain[0] * output_gain) >> 7;
    unsigned right = ((unsigned)usb_gain[1] * output_gain) >> 7;
    /* Accumulate USB before clipping, then add Bluetooth.
     * Discard the block if the USB stream resets. */
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
        int64_t value = ((int64_t)output[i] * output_gain) >> 7;
        if (usb_frames == FM1_BLOCK_FRAMES)
            value += ((int64_t)usb_pcm[i] * ((i & 1U) ? right : left)) >> 7;
        output[i] = usb_frames == -2 ? 0 : limit(value);
    }
    if (fm1_bluetooth_audio_read(bluetooth_pcm, FM1_BLOCK_FRAMES) == FM1_BLOCK_FRAMES) {
        /* output is already clipped to 24 bits. Adding signed 16-bit PCM
         * times at most 254 fits in 25 bits, so this stage needs no int64. */
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
            int32_t value = output[i] + (int32_t)bluetooth_pcm[i] * (int32_t)(output_gain * 2U);
            output[i] = value > 8388607 ? 8388607 : value < -8388608 ? -8388608 : value;
        }
    }
    (void)fm1_usb_audio_write(output, FM1_BLOCK_FRAMES);
}
