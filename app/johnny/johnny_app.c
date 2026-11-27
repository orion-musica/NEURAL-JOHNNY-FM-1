/* NEURAL JOHNNY - app logic (see johnny_app.h).
 *
 *  1. Start screen: name, a few words, "play me 1-16 notes".
 *  2. The first key starts a silent 132 BPM clock; the notes are snapped to
 *     16ths. When the player stops (4 silent 16ths before a bar line), or
 *     after 16 notes or 2 bars, the model writes a continuation of the same
 *     length and starts playing it right on the next bar line: seed + answer,
 *     looping, a note or two developed on every repeat.
 *  3. After 8 bars the first extra layer comes in, then one more every 8 bars
 *     (kick, bass, hats, drone, clap, echo, perc). Layers take breaths of
 *     1, 2 or 4 bars that end on the 8-bar line, now and then a breakdown.
 *  4. A new melody at any moment: everything keeps playing, the lead waits,
 *     the model continues the new motif; the layers not yet in come in at
 *     random block lines instead of in order.
 *  HOME goes back to the start screen. */
#include "johnny_app.h"
#include "johnny_model.h"
#include "johnny_synth.h"
#include "johnny_font.h"
#include <string.h>

/* ------------------------------------------------------------- helpers */
static uint32_t rng_state = 0x6d2b79f5u;
static uint32_t rnd(void) { uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }
static float rndf(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static unsigned rndn(unsigned n) { return n ? rnd() % n : 0; }

/* ------------------------------------------------------------- state */
static const uint8_t ORDER[L_COUNT - 1] = { L_KICK, L_BASS, L_HAT, L_DRONE, L_CLAP, L_ECHO, L_PERC };
static const uint16_t BASS_BITS[] = {
    0x4444,   /* ..x. offbeat           */
    0xeeee,   /* .xxx rolling           */
    0x6666,   /* .xx. gallop            */
    0xcccc,   /* ..xx                   */
    0xaeae,   /* rolling with a gap     */
};
/* Scales as pitch-class masks over the tonic, in order of preference. */
static const uint16_t SCALES[] = {
    0x5ad, /* aeolian  0 2 3 5 7 8 10 */  0x5ab, /* phrygian 0 1 3 5 7 8 10 */
    0x6ad, /* dorian   0 2 3 5 7 9 10 */  0x4a9, /* min pent 0 3 5 7 10 */
    0x9ad, /* harm min 0 2 3 5 7 8 11 */  0x6b5, /* mixolyd  0 2 4 5 7 9 10 */
    0xab5, /* ionian   0 2 4 5 7 9 11 */
};

static struct {
    uint8_t view;
    uint32_t prev_keys, prev_buttons, now;
    /* melody being played in */
    uint8_t rec, seed_count, seed_last;
    uint32_t seed_bar, decided_bar;
    uint8_t seed_note[32];
    /* the phrase, in model tokens */
    uint8_t toks[64], orig[64], len, half, tonic_midi;
    uint16_t scale;
    int8_t lo, hi;
    uint16_t bass_bits, perc_bits;
    uint32_t echo_keep[2];
    uint8_t have_phrase;
    /* arrangement */
    uint8_t active, order_i, free_mode, newest, breath[8];
    uint32_t arr_bar0, planned_for, seen_wraps, reps, seen_swaps, block_bar;
    /* screen */
    uint32_t last_frame, serial;
    float amp[L_COUNT];
    int led_on;
} S;
static uint8_t lcd[MM_LCD_H][MM_LCD_W / 8];

/* ------------------------------------------------------------- model use */
static int tok_allowed(unsigned t)
{
    if (t == MM_REST) return 1;
    int off = (int)t - MM_OFFSET;
    if (off < S.lo || off > S.hi) return 0;
    return (S.scale >> ((unsigned)(off + 120) % 12u)) & 1u;
}
/* Draw one token for step i from the model, inside the scale and range.
 * avoid: a token not to draw (the one there now, to force a change), or -1. */
static uint8_t sample_step(unsigned i, int avoid)
{
    float p[MM_TOKENS], sum = 0.0f;
    mm_predict(S.toks, S.len, i, p);
    for (unsigned t = 0; t < MM_TOKENS; ++t) {
        p[t] = tok_allowed(t) && (int)t != avoid ? p[t] : 0.0f;
        sum += p[t];
    }
    if (sum <= 0.0f) return MM_REST;
    float r = rndf() * sum;
    for (unsigned t = 0; t < MM_TOKENS; ++t) { r -= p[t]; if (r <= 0.0f) return (uint8_t)t; }
    return MM_REST;
}

static void analyse_seed(void)
{
    unsigned count[12] = {0}, first = 0, last = 0, low = 127, high = 0, n = 0;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned m = S.seed_note[i];
        if (!m) continue;
        if (!n) first = m;
        last = m; ++count[m % 12]; ++n;
        if (m < low) low = m;
        if (m > high) high = m;
    }
    /* tonic: most played, first and lowest note weigh extra, and a minor
     * scale that holds every note played is a strong hint */
    float best = -1.0f; unsigned tonic = first % 12;
    for (unsigned c = 0; c < 12; ++c) {
        if (!count[c]) continue;
        uint16_t set = 0;
        for (unsigned k = 0; k < 12; ++k) if (count[k]) set |= (uint16_t)(1u << ((k + 12 - c) % 12));
        float s = (float)count[c] + (first % 12 == c ? 1.5f : 0.0f) + (low % 12 == c ? 1.0f : 0.0f) + (last % 12 == c ? 0.5f : 0.0f);
        for (unsigned k = 0; k < 5; ++k) if ((set & SCALES[k]) == set) { s += 2.0f; break; }
        if (s > best) { best = s; tonic = c; }
    }
    uint16_t set = 0;
    for (unsigned k = 0; k < 12; ++k) if (count[k]) set |= (uint16_t)(1u << ((k + 12 - tonic) % 12));
    S.scale = SCALES[0] | set;
    for (unsigned k = 0; k < sizeof SCALES / sizeof SCALES[0]; ++k)
        if ((set & SCALES[k]) == set) { S.scale = SCALES[k]; break; }
    unsigned t = low;
    while (t % 12 != tonic) --t;
    if ((int)high - (int)t > 24) t += 12;
    S.tonic_midi = (uint8_t)t;
    int lo = (int)low - (int)t - 5, hi = (int)high - (int)t + 7;
    S.lo = (int8_t)(lo < -12 ? -12 : lo); S.hi = (int8_t)(hi > 24 ? 24 : hi);
}

static uint8_t to_tok(unsigned midi)
{
    int off = (int)midi - (int)S.tonic_midi;
    while (off > 24) off -= 12;
    while (off < -12) off += 12;
    return (uint8_t)(off + MM_OFFSET);
}

static uint16_t euclid(unsigned k, unsigned rot)
{
    uint16_t b = 0;
    for (unsigned i = 0; i < 16; ++i) if ((i * k) % 16 < k) b |= (uint16_t)(1u << ((i + rot) % 16));
    return b;
}

/* Copy the phrase into the synth's free buffer. */
static void post_phrase(unsigned when, unsigned start_pos, int release_hold)
{
    ms_cancel();
    ms_phrase *p = ms_back();
    for (unsigned i = 0; i < S.len; ++i)
        p->note[i] = S.toks[i] ? (uint8_t)((int)S.tonic_midi + (int)S.toks[i] - MM_OFFSET) : 0;
    p->len = S.len; p->root = (uint8_t)(S.tonic_midi % 12u);
    p->bass_bits = S.bass_bits; p->perc_bits = S.perc_bits; p->echo_keep[0] = S.echo_keep[0]; p->echo_keep[1] = S.echo_keep[1];
    ms_commit(when, start_pos, release_hold);
}

static void plan_bar(uint32_t bar);

/* Change the note at one step of the answer to the model's next choice.
 * Steps with a note, or right next to one, are picked more often. */
static void change_one(void)
{
    unsigned span = S.len - S.half, i = S.half + rndn(span);
    for (unsigned tries = 0; tries < 4; ++tries) {
        unsigned j = S.half + rndn(span);
        if (S.toks[j] || S.toks[S.half + (j - S.half + 1) % span] || S.toks[S.half + (j - S.half + span - 1) % span]) { i = j; break; }
    }
    S.toks[i] = sample_step(i, S.toks[i]);
}

/* The player has finished a melody: build seed + continuation and go.
 * The answer joins at the next 16th, lined up with the bar. */
static void finish_seed(unsigned seed_bars, uint32_t bar_now, int join_mid_bar)
{
    S.rec = 0;
    analyse_seed();
    S.len = (uint8_t)(seed_bars * 32u); S.half = (uint8_t)(seed_bars * 16u);
    memset(S.toks, 0, sizeof S.toks);
    for (unsigned i = 0; i < S.half; ++i) S.toks[i] = S.seed_note[i] ? to_tok(S.seed_note[i]) : MM_REST;
    unsigned notes = 0;
    for (int attempt = 0; attempt < 3 && !notes; ++attempt)
        for (unsigned i = S.half; i < S.len; ++i) { S.toks[i] = sample_step(i, -1); notes += S.toks[i] != MM_REST; }
    if (!notes) memcpy(S.toks + S.half, S.toks, S.half);
    /* an answer, not an echo: at least two steps differ from the motif */
    for (int guard = 0; guard < 12; ++guard) {
        unsigned diff = 0;
        for (unsigned i = 0; i < S.half; ++i) diff += S.toks[S.half + i] != S.toks[i];
        if (diff >= 2) break;
        change_one();
    }
    memcpy(S.orig, S.toks, sizeof S.toks);
    S.bass_bits = BASS_BITS[rndn(sizeof BASS_BITS / sizeof BASS_BITS[0])];
    S.perc_bits = euclid(rndn(2) ? 5u : 7u, rndn(16));
    S.echo_keep[0] = S.echo_keep[1] = 0;
    for (unsigned i = 0; i < S.len; ++i) if (rndf() < 0.55f) S.echo_keep[i >> 5] |= 1u << (i & 31u);
    post_phrase(MS_ALIGN, S.half, 1);
    S.reps = 0;
    if (!S.have_phrase) {                        /* first melody: the arrangement starts */
        S.have_phrase = 1; S.active = 1u << L_LEAD; S.order_i = 0; S.free_mode = 0;
        S.arr_bar0 = bar_now + (join_mid_bar ? 0u : 1u); memset(S.breath, 0, sizeof S.breath);
        if (join_mid_bar) ms_mask_now(1u << L_LEAD);
    } else {
        S.free_mode = 1;
    }
    S.seen_wraps = 0xffffffffu;
    plan_bar(bar_now + 1);
}

/* One more repeat has started: develop the continuation a little. */
static void develop(void)
{
    ++S.reps;
    if (S.rec || ms_pending()) return;
    unsigned changes = 1u + (rndf() < 0.3f ? 1u : 0u);
    for (unsigned c = 0; c < changes; ++c) change_one();
    /* hypnotic means sparse: an answer much busier than the motif loses a note */
    unsigned seed_n = 0, ans_n = 0;
    for (unsigned i = 0; i < S.half; ++i) { seed_n += S.toks[i] != MM_REST; ans_n += S.toks[S.half + i] != MM_REST; }
    if (ans_n > seed_n + 2u) {
        unsigned k = rndn(ans_n);
        for (unsigned i = S.half; i < S.len; ++i) if (S.toks[i] && !k--) { S.toks[i] = MM_REST; break; }
    }
    if (rndf() < 0.45f) {                        /* drift back towards the first answer */
        for (unsigned tries = 0; tries < 8; ++tries) {
            unsigned i = S.half + rndn(S.len - S.half);
            if (S.toks[i] != S.orig[i]) { S.toks[i] = S.orig[i]; break; }
        }
    }
    if (rndf() < 0.15f) {                        /* the echo picks other notes */
        unsigned i = rndn(S.len);
        S.echo_keep[i >> 5] ^= 1u << (i & 31u);
    }
    post_phrase(MS_AT_WRAP, 0, 0);
}

/* ------------------------------------------------------------- arrangement */
static unsigned bit_count(unsigned m) { unsigned n = 0; while (m) { n += m & 1u; m >>= 1; } return n; }

static void new_block(unsigned block)
{
    if (block > 0) {
        if (!S.free_mode) {
            if (S.order_i < L_COUNT - 1) { S.newest = ORDER[S.order_i++]; S.active |= 1u << S.newest; }
            else S.newest = 0xff;
        } else {
            unsigned missing = ((1u << L_COUNT) - 1u) & ~S.active;
            S.newest = 0xff;
            if (missing && rndf() < 0.55f) {
                unsigned k = rndn(bit_count(missing));
                for (unsigned l = 0; l < L_COUNT; ++l)
                    if ((missing >> l) & 1u) { if (!k--) { S.active |= 1u << l; S.newest = (uint8_t)l; break; } }
            }
        }
    }
    memset(S.breath, 0, sizeof S.breath);
    unsigned others = S.active & ~(1u << L_LEAD);
    if (S.newest < L_COUNT) others &= ~(1u << S.newest);   /* a layer that just came in stays */
    unsigned n = bit_count(S.active) - 1u;
    if (bit_count(others) >= 1 && n >= 2 && rndf() < 0.6f) {
        static const uint8_t weight[L_COUNT] = { 0, 5, 3, 3, 2, 3, 2, 2 };
        unsigned total = 0;
        for (unsigned l = 0; l < L_COUNT; ++l) if ((others >> l) & 1u) total += weight[l];
        unsigned pick = rndn(total), layer = L_KICK;
        for (unsigned l = 0; l < L_COUNT; ++l)
            if ((others >> l) & 1u) { if (pick < weight[l]) { layer = l; break; } pick -= weight[l]; }
        static const uint8_t lens[5] = { 1, 1, 2, 2, 4 };
        unsigned len = lens[rndn(5)];
        for (unsigned b = 8 - len; b < 8; ++b) S.breath[b] |= (uint8_t)(1u << layer);
    }
    if (n >= 5 && rndf() < 0.22f) {                         /* breakdown: the floor drops for 4 bars */
        unsigned floor = ((1u << L_KICK) | (1u << L_BASS) | (1u << L_HAT)) & others;
        for (unsigned b = 4; b < 8; ++b) S.breath[b] |= (uint8_t)floor;
    }
    if (n >= 3 && rndf() < 0.12f) S.breath[7] |= 1u << L_LEAD;   /* the melody holds its breath */
}

static void plan_bar(uint32_t bar)
{
    uint8_t m = 0;
    if (S.have_phrase && bar >= S.arr_bar0) {
        uint32_t rel = bar - S.arr_bar0;
        if (rel % 8u == 0u && S.block_bar != bar) { S.block_bar = bar; new_block(rel / 8u); }
        m = (uint8_t)(S.active & ~S.breath[rel % 8u]);
        if (!m) m = 1u << L_LEAD;
    } else if (S.have_phrase) m = (uint8_t)S.active;
    ms_plan_mask(bar, m);
    S.planned_for = bar;
}

/* ------------------------------------------------------------- capture */
static void capture_note(unsigned midi, const ms_clock *c, int first_ever)
{
    uint32_t q;
    if (first_ever) q = 0;
    else {
        float at = (float)(c->bar * 16u + c->step) + c->phase;
        q = (uint32_t)(at + 0.5f);
    }
    if (!S.rec) {
        S.rec = 1; S.seed_count = 0; S.seed_last = 0;
        memset(S.seed_note, 0, sizeof S.seed_note);
        S.seed_bar = q / 16u;
        if (q % 16u >= 12u) { S.seed_bar += 1; q = S.seed_bar * 16u; }   /* a late start counts from the next bar */
        S.decided_bar = 0xffffffffu;
        ms_cancel();
        ms_lead_hold(1);
    }
    if (q < S.seed_bar * 16u) q = S.seed_bar * 16u;
    uint32_t rel = q - S.seed_bar * 16u;
    if (rel >= 32u || S.seed_count >= 16u) return;
    if (!S.seed_note[rel]) ++S.seed_count;
    S.seed_note[rel] = (uint8_t)midi;
    if (rel > S.seed_last) S.seed_last = (uint8_t)rel;
}

/* When is the melody finished?
 *  - on the last 16th of a bar: 16 notes, 2 full bars, or 4 silent 16ths
 *    before the bar line -> the answer starts right on the bar line;
 *  - a motif that ran to the end of its bar: if nothing follows in the first
 *    three 16ths of the next bar, it was one bar -> the answer joins there. */
static void check_seed_done(const ms_clock *c, uint32_t keys)
{
    if (!S.rec || c->bar < S.seed_bar) return;
    unsigned bars_in = c->bar - S.seed_bar + 1u;
    if (c->step >= 15u && c->bar != S.decided_bar) {
        S.decided_bar = c->bar;
        int done = S.seed_count >= 16u || bars_in >= 2u || (S.seed_last + 5u <= bars_in * 16u && !keys);
        if (done) { finish_seed(bars_in > 2u ? 2u : bars_in, c->bar, 0); return; }
    }
    if (bars_in == 2u && c->step >= 3u && c->step < 15u && S.seed_last < 16u && !keys)
        finish_seed(1u, c->bar, 1);
}

/* ------------------------------------------------------------- picture */
static void lcd_clear(void) { memset(lcd, 0, sizeof lcd); }
static void lcd_set(int x, int y) { if (x >= 0 && y >= 0 && x < MM_LCD_W && y < MM_LCD_H) lcd[y][x >> 3] |= (uint8_t)(0x80u >> (x & 7)); }
static void lcd_text(int x, int y, const char *s)
{
    for (; *s; ++s, x += 6) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 95) ch = '?';
        for (int r = 0; r < 7; ++r)
            for (int b = 0; b < 5; ++b)
                if (mm_font[ch - 32][r] & (0x10u >> b)) lcd_set(x + b, y + r);
    }
}
static void lcd_text_c(int y, const char *s) { lcd_text((MM_LCD_W - (int)strlen(s) * 6 + 1) / 2, y, s); }

static void draw_start(void)
{
    lcd_clear();
    for (int x = 8; x < 72; ++x) {                       /* a small oscillator over the name */
        int y = 9 + (int)(4.0f * mm_sin01((float)(x - 8) / 21.0f));
        lcd_set(x, y);
    }
    lcd_text_c(18, "NEURAL JOHNNY");
    for (int x = 12; x < 68; x += 2) lcd_set(x, 27);
    lcd_text_c(32, "PLAY ME 1-16");
    lcd_text_c(41, "NOTES. I WILL");
    lcd_text_c(50, "CARRY YOUR");
    lcd_text_c(59, "MELODY ON.");
    lcd_text_c(71, "BUL@KE");
    if ((S.now / 500u) & 1u)                              /* blinking phone cursor */
        for (int y = 59; y < 66; ++y) for (int x = 70; x < 74; ++x) lcd_set(x, y);
}

static float wave(unsigned l, float x, float t, unsigned pitch, uint32_t frame)
{
    switch (l) {
    case L_LEAD: {
        float cyc = 1.2f + ((float)pitch - 48.0f) / 20.0f;
        if (cyc < 0.8f) cyc = 0.8f;
        float ph = x * cyc + t * 0.55f;
        return 0.75f * mm_sin01(ph) + 0.25f * mm_sin01(2.0f * ph + 0.3f);
    }
    case L_KICK:  return mm_sin01(x * 0.7f - t * 0.18f);
    case L_BASS:  { float ph = x * 3.0f + t * 0.7f; ph -= (float)(int)ph; return ph * 2.0f - 1.0f; }
    case L_HAT:   { uint32_t h = (uint32_t)(x * 80.0f) * 2654435761u ^ frame * 40503u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                    return (float)(h & 255u) / 127.5f - 1.0f; }
    case L_DRONE: return 0.7f * mm_sin01(x - t * 0.07f) + 0.3f * mm_sin01(x * 2.5f + t * 0.11f);
    case L_CLAP:  { uint32_t h = (uint32_t)(x * 40.0f) * 2246822519u ^ frame * 3266489917u; h ^= h >> 15;
                    return ((float)(h & 255u) / 127.5f - 1.0f) * (0.5f + 0.5f * mm_sin01(x * 1.5f)); }
    case L_ECHO:  return mm_sin01(x * 6.0f + t * 1.1f);
    default:      return mm_sin01(x * 4.0f + t * 0.5f) > 0.0f ? 1.0f : -1.0f;   /* perc: square */
    }
}

static void draw_waves(uint32_t now, unsigned mask)
{
    static const float gain[L_COUNT] = { 15.0f, 24.0f, 12.0f, 6.0f, 14.0f, 9.0f, 8.0f, 7.0f };
    static const float idle[L_COUNT] = { 1.5f, 1.0f, 1.2f, 0.8f, 2.0f, 0.8f, 1.0f, 1.0f };
    lcd_clear();
    float t = (float)now * 0.001f;
    uint32_t frame = now / 40u;
    for (unsigned l = 0; l < L_COUNT; ++l) {
        int on = (mask >> l) & 1u;
        if (l == L_LEAD && S.rec) on = 1;
        float lvl = ms_level(l);
        float target = on ? idle[l] + lvl * gain[l] : lvl * gain[l] * 0.4f;
        S.amp[l] += (target - S.amp[l]) * 0.4f;
        if (!on && S.amp[l] < 0.6f) continue;           /* resting: the line is gone */
        unsigned pitch = ms_pitch(l);
        int prev = 0;
        for (int x = 0; x < MM_LCD_W; ++x) {
            float v = S.amp[l] * wave(l, (float)x / (float)MM_LCD_W, t, pitch, frame);
            if (v > 33.0f) v = 33.0f;
            if (v < -33.0f) v = -33.0f;
            int y = 40 + (int)(v < 0 ? v - 0.5f : v + 0.5f);
            if (x == 0) prev = y;
            int a = prev, b = y;                         /* join to the previous column */
            if (b > a + 1) { for (int k = a + 1; k <= b; ++k) lcd_set(x, k); }
            else if (b < a - 1) { for (int k = b; k < a; ++k) lcd_set(x, k); }
            else lcd_set(x, y);
            prev = y;
        }
    }
}

static void leds(void)
{
    float lvl = ms_level(L_LEAD);
    int want = -1;
    if (S.view == MM_VIEW_WAVES && lvl > 0.25f) {
        int k = (int)ms_pitch(L_LEAD) - MM_KEY_LOWEST_MIDI;
        while (k < 0) k += 12;
        while (k >= MM_KEY_COUNT) k -= 12;
        want = 14 + k;
    }
    if (want != S.led_on) {
        if (S.led_on >= 0) johnny_hal_led((unsigned)S.led_on, 0);
        if (want >= 0) johnny_hal_led((unsigned)want, 1);
        S.led_on = want;
    }
}

/* ------------------------------------------------------------- platform API */
void johnny_init(uint32_t now)
{
    memset(&S, 0, sizeof S);
    rng_state ^= now * 2654435761u + 1u;
    ms_init();
    S.view = MM_VIEW_START; S.now = now; S.led_on = -1;
    S.planned_for = 0xffffffffu; S.block_bar = 0xffffffffu;
    draw_start(); ++S.serial;
}

void johnny_tick(uint32_t now, uint32_t keys, uint32_t buttons)
{
    S.now = now;
    rng_state ^= keys * 0x9e3779b9u;
    if (!rng_state) rng_state = 1;
    uint32_t down = keys & ~S.prev_keys, up = S.prev_keys & ~keys;
    uint32_t bdown = buttons & ~S.prev_buttons;
    S.prev_keys = keys; S.prev_buttons = buttons;

    if (bdown & (1u << MM_B_HOME)) {                    /* back to the start screen */
        ms_stop();
        for (unsigned k = 0; k < MM_KEY_COUNT; ++k) ms_live_off(MM_KEY_LOWEST_MIDI + k);
        johnny_init(now);
        return;
    }
    ms_clock c = {0};
    if (ms_running()) ms_get_clock(&c);
    for (unsigned k = 0; k < MM_KEY_COUNT; ++k) {
        if ((up >> k) & 1u) ms_live_off(MM_KEY_LOWEST_MIDI + k);
        if ((down >> k) & 1u) {
            unsigned midi = MM_KEY_LOWEST_MIDI + k;
            ms_live_on(midi);
            if (S.view == MM_VIEW_START) {
                S.view = MM_VIEW_WAVES; ++S.serial;
                ms_start();
                capture_note(midi, &c, 1);
            } else capture_note(midi, &c, 0);
        }
    }
    if (S.view == MM_VIEW_WAVES && ms_running()) {
        ms_get_clock(&c);
        check_seed_done(&c, keys);
        if (S.have_phrase && c.bar + 1u != S.planned_for) plan_bar(c.bar + 1u);
        if (S.have_phrase && c.wraps != S.seen_wraps) {
            if (S.seen_wraps != 0xffffffffu) develop();
            S.seen_wraps = c.wraps;
        }
        if (now - S.last_frame >= 33u) { S.last_frame = now; draw_waves(now, c.mask); ++S.serial; }
    } else if (S.view == MM_VIEW_START) {
        static uint32_t blink;
        if (now / 500u != blink) { blink = now / 500u; draw_start(); ++S.serial; }
    }
    leds();
}

const uint8_t *johnny_lcd(void) { return &lcd[0][0]; }
unsigned johnny_view(void) { return S.view; }
unsigned johnny_lcd_serial(void) { return S.serial; }
unsigned johnny_layers_on(void) { ms_clock c; ms_get_clock(&c); return c.mask; }
unsigned johnny_repeats(void) { return S.reps; }
const uint8_t *johnny_debug_phrase(unsigned *len, unsigned *tonic) { *len = S.len; *tonic = S.tonic_midi; return S.toks; }
