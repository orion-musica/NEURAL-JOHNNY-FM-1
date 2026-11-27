/* NEURAL JOHNNY - groove model inference (see johnny_drums.h).
 * Feature layout must match johnny/tools/train_drums.py:features(). */
#include "johnny_drums.h"
#include "johnny_drum_weights.h"

static float x[JD_NIN], h1[JD_H1], h2[JD_H2];

void jd_predict(const uint16_t *g, unsigned len, unsigned i, float probs[JD_DRUMS])
{
    const unsigned back[JD_BACK] = { 1, 2, 4, 8, 16, len / 2 };
    unsigned n = 0;
    for (unsigned b = 0; b < JD_BACK; ++b) {
        uint16_t h = g[(i + len * 4u - back[b]) % len];
        for (unsigned d = 0; d < JD_DRUMS; ++d) x[n++] = (float)((h >> d) & 1u);
    }
    uint16_t used = 0;
    for (unsigned k = 0; k < len / 2; ++k) used |= g[k];
    for (unsigned d = 0; d < JD_DRUMS; ++d) x[n++] = (float)((used >> d) & 1u);
    for (unsigned p = 0; p < 16; ++p) x[n++] = (p == i % 16u) ? 1.0f : 0.0f;
    x[n++] = i >= len / 2 ? 1.0f : 0.0f;
    x[n++] = i % 16u >= 12u ? 1.0f : 0.0f;

    for (unsigned o = 0; o < JD_H1; ++o) {
        const int8_t *w = &jd_W1[o * JD_NIN]; float s = 0.0f;
        for (unsigned k = 0; k < JD_NIN; ++k) s += (float)w[k] * x[k];
        s = s * jd_W1_scale + jd_b1[o]; h1[o] = s > 0.0f ? s : 0.0f;
    }
    for (unsigned o = 0; o < JD_H2; ++o) {
        const int8_t *w = &jd_W2[o * JD_H1]; float s = 0.0f;
        for (unsigned k = 0; k < JD_H1; ++k) s += (float)w[k] * h1[k];
        s = s * jd_W2_scale + jd_b2[o]; h2[o] = s > 0.0f ? s : 0.0f;
    }
    for (unsigned o = 0; o < JD_DRUMS; ++o) {
        const int8_t *w = &jd_W3[o * JD_H2]; float s = 0.0f;
        for (unsigned k = 0; k < JD_H2; ++k) s += (float)w[k] * h2[k];
        s = s * jd_W3_scale + jd_b3[o];
        /* logistic without libm: e^-|s| from the port helper */
        float e = mm_exp_neg(s < 0.0f ? (-s > 8.0f ? 8.0f : -s) : (s > 8.0f ? 8.0f : s));
        probs[o] = s >= 0.0f ? 1.0f / (1.0f + e) : e / (1.0f + e);
    }
}
