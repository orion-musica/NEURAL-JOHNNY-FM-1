/* NEURAL JOHNNY - the player's drum groove on the panel buttons (see johnny_groove.h).
 *
 * Works like the melody: tap 1 or 2 bars on the pads, stop, and the groove
 * model writes an answer of the same length; groove and answer loop on their
 * own, and every repeat the model flips a hit or two of its answer. */
#include "johnny_groove.h"
#include "johnny_drums.h"
#include "johnny_synth.h"
#include <string.h>

static struct {
    uint8_t rec, have, count, last;
    uint32_t seed_bar, decided_bar, seen_wraps;
    uint16_t seed[32];
    uint16_t g[64], orig[64], used;
    uint8_t len, half;
    int mutate, notes;
} G;

static uint32_t rng = 0x51ed27u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float rndf(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static unsigned bits(uint32_t m) { unsigned n = 0; while (m) { n += m & 1u; m >>= 1; } return n; }

void jg_init(uint32_t seed)
{
    memset(&G, 0, sizeof G);
    rng ^= seed * 2654435761u + 7u; if (!rng) rng = 1;
    G.mutate = 5;
}
void jg_style(int mutate, int notes) { G.mutate = mutate; G.notes = notes; }
int jg_have(void) { return G.have; }
int jg_recording(void) { return G.rec; }

/* Density from K4 NOTES: how much more or less likely a hit is. */
static float density(void)
{
    static const float d[11] = { 0.35f, 0.45f, 0.58f, 0.7f, 0.85f, 1.0f, 1.18f, 1.38f, 1.6f, 1.85f, 2.1f };
    return d[G.notes + 5];
}
static float shape(float p, int used)
{
    if (!used) p *= 0.3f;                      /* drums you did not play join only when the model is sure */
    p *= density();
    if (G.mutate <= 3) p = p * p * (3.0f - 2.0f * (p > 1.0f ? 1.0f : p));   /* calm: firmer yes / no */
    else if (G.mutate >= 8 && used) p = 0.8f * p + 0.08f;                  /* wild: more surprises */
    return p > 1.0f ? 1.0f : p;
}
static uint16_t sample_step(unsigned i)
{
    float pr[D_COUNT];
    jd_predict(G.g, G.len, i, pr);
    uint16_t h = 0;
    for (unsigned d = 0; d < D_COUNT; ++d)
        if (rndf() < shape(pr[d], (G.used >> d) & 1u)) h |= (uint16_t)(1u << d);
    return h;
}
/* Flip one hit of the answer, where the model most wants a change. */
static void flip_one(void)
{
    unsigned span = G.len - G.half, i = G.half + rnd() % span;
    float pr[D_COUNT], w[D_COUNT], sum = 0.0f;
    jd_predict(G.g, G.len, i, pr);
    for (unsigned d = 0; d < D_COUNT; ++d) {
        int on = (G.g[i] >> d) & 1u, used = (G.used >> d) & 1u;
        if (d == D_KICK && (i % 4u) == 0 && on) { w[d] = 0.0f; continue; }   /* four on the floor stays */
        w[d] = (on ? 1.0f - pr[d] : shape(pr[d], used)) * (used ? 1.0f : 0.25f) + 0.002f;
        sum += w[d];
    }
    float r = rndf() * sum;
    for (unsigned d = 0; d < D_COUNT; ++d) { r -= w[d]; if (r <= 0.0f) { G.g[i] ^= (uint16_t)(1u << d); return; } }
}
static void post(unsigned when, unsigned pos)
{
    ms_groove_cancel();
    ms_groove *b = ms_groove_back();
    memcpy(b->hits, G.g, sizeof b->hits);
    b->len = G.len;
    ms_groove_commit(when, pos);
}

static int finish(unsigned seed_bars)
{
    G.rec = 0;
    G.len = (uint8_t)(seed_bars * 32u); G.half = (uint8_t)(seed_bars * 16u);
    memset(G.g, 0, sizeof G.g);
    memcpy(G.g, G.seed, G.half * sizeof G.g[0]);
    G.used = 0;
    for (unsigned i = 0; i < G.half; ++i) G.used |= G.g[i];
    for (unsigned i = G.half; i < G.len; ++i) G.g[i] = sample_step(i);
    for (int guard = 0; guard < 12; ++guard) {          /* an answer, not a copy */
        unsigned diff = 0;
        for (unsigned i = 0; i < G.half; ++i) diff += bits(G.g[G.half + i] ^ G.g[i]);
        if (diff >= 2) break;
        flip_one();
    }
    memcpy(G.orig, G.g, sizeof G.g);
    post(MS_ALIGN, G.half);
    G.have = 1;
    G.seen_wraps = ms_groove_wraps();
    return 1;
}

int jg_pad(unsigned drum, const ms_clock *c)
{
    float at = (float)(c->bar * 16u + c->step) + c->phase;
    uint32_t q = (uint32_t)(at + 0.5f);
    if (!G.rec) {
        G.rec = 1; G.count = 0; G.last = 0;
        memset(G.seed, 0, sizeof G.seed);
        G.seed_bar = q / 16u;
        if (q % 16u >= 12u) { G.seed_bar += 1; q = G.seed_bar * 16u; }
        G.decided_bar = 0xffffffffu;
        ms_groove_cancel();
    }
    if (q < G.seed_bar * 16u) q = G.seed_bar * 16u;
    uint32_t rel = q - G.seed_bar * 16u;
    if (rel >= 32u) return 0;
    G.seed[rel] |= (uint16_t)(1u << drum);
    if (G.count < 255) ++G.count;
    if (rel > G.last) G.last = (uint8_t)rel;
    return 0;
}

int jg_tick(const ms_clock *c, int pads_held)
{
    int done = 0;
    if (G.rec && c->bar >= G.seed_bar) {                 /* same end rules as the melody */
        unsigned bars_in = c->bar - G.seed_bar + 1u;
        if (c->step >= 15u && c->bar != G.decided_bar) {
            G.decided_bar = c->bar;
            if (bars_in >= 2u || (G.last + 5u <= bars_in * 16u && !pads_held)) done = finish(bars_in > 2u ? 2u : bars_in);
        }
        if (!done && G.rec && bars_in == 2u && c->step >= 3u && c->step < 15u && G.last < 16u && !pads_held) done = finish(1u);
    }
    uint32_t w = ms_groove_wraps();
    if (G.have && !G.rec && w != G.seen_wraps) {         /* a new repeat: develop the answer */
        G.seen_wraps = w;
        if (G.mutate > 0 && !ms_groove_pending()) {
            unsigned changes = G.mutate <= 3 ? (rndf() < 0.5f ? 1u : 0u)
                             : G.mutate <= 7 ? 1u + (rndf() < 0.3f ? 1u : 0u)
                             : 2u + (unsigned)(G.mutate - 8) / 2u + (rndf() < 0.5f ? 1u : 0u);
            for (unsigned k = 0; k < changes; ++k) flip_one();
            unsigned seed_n = 0, ans_n = 0;
            for (unsigned i = 0; i < G.half; ++i) { seed_n += bits(G.g[i]); ans_n += bits(G.g[G.half + i]); }
            int target = (int)seed_n + 3 + G.notes * 2;
            if (target < 1) target = 1;
            if ((int)ans_n > target) {                    /* keep the answer about as busy as you */
                unsigned k = rnd() % ans_n;   /* (four-on-the-floor kicks are skipped) */
                for (unsigned i = G.half; i < G.len; ++i)
                    for (unsigned d = 0; d < D_COUNT; ++d)
                        if ((G.g[i] >> d) & 1u && !(d == D_KICK && i % 4u == 0)) { if (!k--) { G.g[i] &= (uint16_t)~(1u << d); goto removed; } }
            }
        removed:
            if (rndf() < (G.mutate >= 8 ? 0.15f : 0.45f)) {   /* drift back towards the first answer */
                for (unsigned t = 0; t < 8; ++t) {
                    unsigned i = G.half + rnd() % (G.len - G.half);
                    if (G.g[i] != G.orig[i]) { G.g[i] = G.orig[i]; break; }
                }
            }
            post(MS_AT_WRAP, 0);
        }
    }
    return done;
}

const uint16_t *jg_debug(unsigned *len) { *len = G.len; return G.g; }
