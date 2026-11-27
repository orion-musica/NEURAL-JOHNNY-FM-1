/* NEURAL JOHNNY - TinyML melody model inference (see johnny_model.h).
 * Feature layout must match johnny/tools/train_model.py:features(). */
#include "johnny_model.h"
#include "johnny_weights.h"

static float x[MM_NIN], h1[MM_H1], h2[MM_H2];

void mm_predict(const uint8_t *toks, unsigned len, unsigned i, float probs[MM_TOKENS])
{
    unsigned L = len;
#define PREV(k) toks[(i + L * 4u - (unsigned)(k)) % L]
    uint8_t last = 0;
    for (unsigned k = 1; k < L; ++k) { uint8_t t = PREV(k); if (t) { last = t; break; } }
    uint8_t ids[MM_CTX] = { PREV(1), PREV(2), PREV(3), PREV(4), PREV(8), PREV(16), PREV(L / 2), last };
#undef PREV
    unsigned n = 0;
    for (unsigned c = 0; c < MM_CTX; ++c) {
        const int8_t *e = &mm_E[(unsigned)ids[c] * MM_EMB];
        for (unsigned j = 0; j < MM_EMB; ++j) x[n++] = (float)e[j] * mm_E_scale;
    }
    for (unsigned p = 0; p < MM_NPOS; ++p) x[n++] = (p == i % 16u) ? 1.0f : 0.0f;
    x[n++] = i >= L / 2 ? 1.0f : 0.0f;
    x[n++] = L - i <= 4 ? 1.0f : 0.0f;

    for (unsigned o = 0; o < MM_H1; ++o) {
        const int8_t *w = &mm_W1[o * MM_NIN]; float s = 0.0f;
        for (unsigned k = 0; k < MM_NIN; ++k) s += (float)w[k] * x[k];
        s = s * mm_W1_scale + mm_b1[o]; h1[o] = s > 0.0f ? s : 0.0f;
    }
    for (unsigned o = 0; o < MM_H2; ++o) {
        const int8_t *w = &mm_W2[o * MM_H1]; float s = 0.0f;
        for (unsigned k = 0; k < MM_H1; ++k) s += (float)w[k] * h1[k];
        s = s * mm_W2_scale + mm_b2[o]; h2[o] = s > 0.0f ? s : 0.0f;
    }
    float mx = -1e30f;
    for (unsigned o = 0; o < MM_TOKENS; ++o) {
        const int8_t *w = &mm_W3[o * MM_H2]; float s = 0.0f;
        for (unsigned k = 0; k < MM_H2; ++k) s += (float)w[k] * h2[k];
        probs[o] = s * mm_W3_scale + mm_b3[o];
        if (probs[o] > mx) mx = probs[o];
    }
    float sum = 0.0f;
    for (unsigned o = 0; o < MM_TOKENS; ++o) {
        float d = mx - probs[o];
        probs[o] = d > 16.0f ? 0.0f : mm_exp_neg(d);
        sum += probs[o];
    }
    for (unsigned o = 0; o < MM_TOKENS; ++o) probs[o] /= sum;
}
