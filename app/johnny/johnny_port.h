/* NEURAL JOHNNY - portability shims shared by every johnny_*.c file.
 * On the FM-1 the SDK header supplies FM1_AUDIO_CODE / FM1_AUDIO_DATA
 * (code and tables that must stay in RAM for the audio interrupt).
 * The host / browser builds define FM1_HOST_TEST and get empty macros. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef FM1_HOST_TEST
#ifndef FM1_AUDIO_CODE
#define FM1_AUDIO_CODE
#define FM1_AUDIO_DATA
#endif
#else
#include "fm1.h"
#endif

#define MM_PI 3.14159265f

/* libm-free helpers (the firmware does not link libm). */
static inline float mm_sin01(float p) /* sin(2*pi*p), any p */
{
    p -= (float)(int)p; if (p < 0.0f) p += 1.0f;
    float x = p * 2.0f - 1.0f;                      /* -1..1 -> -sin */
    float y = 4.0f * x * (1.0f - (x < 0 ? -x : x));
    y = 0.225f * (y * (y < 0 ? -y : y) - y) + y;
    return -y;
}
static inline float mm_exp_neg(float x) /* e^-x for 0 <= x <= ~8 */
{
    int n = 0;
    while (x > 0.5f) { x *= 0.5f; ++n; }
    float t = 1.0f, s = 1.0f;
    for (int i = 1; i < 8; ++i) { t *= -x / (float)i; s += t; }
    while (n--) s *= s;
    return s;
}
