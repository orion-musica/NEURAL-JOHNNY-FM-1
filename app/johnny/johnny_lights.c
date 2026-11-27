/* NEURAL JOHNNY - neural light waves (see johnny_lights.h).
 *
 * Geometry: two rows of "neurons". The keys F3..G5 are LEDs 14..40 at
 * x = 0..26; the 14 panel buttons (LEDs 0..13) are spread over the same
 * width, x = 0, 2, .. 26 (their exact layout is not measured yet).
 *
 *   melody note   -> two impulses from its key, running left and right
 *   echo note     -> one slower impulse on the button row
 *   kick          -> a synaptic burst: a short, fast spark both ways
 *   bass          -> the key of the root blinks
 *   hats / perc   -> single neurons fire for a moment
 *   clap          -> an impulse along the whole button row
 *   bar line      -> (with the kick in) a wave across both rows
 *   your own keys -> impulses from the key you play
 * Impulses branch to the other row with a small chance at every step, so the
 * light looks like a net firing, not a scanner. */
#include "johnny_lights.h"
#include "johnny_app.h"
#include "johnny_synth.h"

enum { LEDS = 41, KEYS0 = 14, MAX_IMP = 20, MAX_FLASH = 10 };

typedef struct { float x, speed; int8_t dir; uint8_t row, tail, on, branches; float stop; } imp_t;
static imp_t imp[MAX_IMP];
static struct { uint8_t led; uint32_t until; } flash[MAX_FLASH];
static unsigned next_imp, next_flash;
static uint8_t seen_hits[L_COUNT];
static uint64_t shown;
static uint32_t last_now, last_bar = 0xffffffffu, last_spark;
static uint32_t rng = 0x1234567u;

static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float rndf(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }

static int led_at(unsigned row, int x)
{
    if (x < 0 || x > 26) return -1;
    if (row == 0) return KEYS0 + x;
    return (x & 1) ? -1 : x / 2;            /* buttons sit on the even positions */
}
static int key_x(unsigned midi)
{
    int k = (int)midi - MM_KEY_LOWEST_MIDI;
    while (k < 0) k += 12;
    while (k > 26) k -= 12;
    return k;
}

static void fire(unsigned row, float x, int dir, float keys_per_s, uint8_t tail, float reach, uint8_t branches)
{
    imp_t *p = &imp[next_imp]; next_imp = (next_imp + 1u) % MAX_IMP;
    p->x = x; p->dir = (int8_t)dir; p->speed = keys_per_s; p->row = (uint8_t)row;
    p->tail = tail; p->on = 1; p->branches = branches;
    p->stop = dir > 0 ? x + reach : x - reach;
}
static void spark(int led, uint32_t now, uint32_t ms)
{
    if (led < 0) return;
    flash[next_flash].led = (uint8_t)led; flash[next_flash].until = now + ms;
    next_flash = (next_flash + 1u) % MAX_FLASH;
}

void jl_init(void)
{
    for (unsigned i = 0; i < MAX_IMP; ++i) imp[i].on = 0;
    for (unsigned i = 0; i < MAX_FLASH; ++i) flash[i].until = 0;
    for (unsigned l = 0; l < L_COUNT; ++l) seen_hits[l] = (uint8_t)ms_hits(l, 0);
    for (unsigned i = 0; i < LEDS; ++i) johnny_hal_led(i, 0);
    shown = 0; last_bar = 0xffffffffu;
}

static int new_hits(unsigned layer, unsigned *note)
{
    uint8_t h = (uint8_t)ms_hits(layer, note);
    int n = (uint8_t)(h - seen_hits[layer]);
    seen_hits[layer] = h;
    return n;
}

void jl_tick(uint32_t now, int playing, uint32_t bar, unsigned step, uint32_t key_down)
{
    float dt = (float)(now - last_now) * 0.001f;
    if (dt > 0.1f) dt = 0.1f;
    last_now = now;
    rng ^= now;
    if (!rng) rng = 1;

    for (unsigned k = 0; k < 27; ++k)                       /* the player's own notes */
        if ((key_down >> k) & 1u) {
            fire(0, (float)k, 1, 70.0f, 2, 27.0f, 2);
            fire(0, (float)k, -1, 70.0f, 2, 27.0f, 2);
        }

    if (playing) {
        unsigned note;
        if (new_hits(L_LEAD, &note)) {
            int x = key_x(note);
            fire(0, (float)x, 1, 55.0f, 3, 27.0f, 3);
            fire(0, (float)x, -1, 55.0f, 3, 27.0f, 3);
        }
        if (new_hits(L_ECHO, &note)) fire(1, (float)(key_x(note) & ~1), rndf() < 0.5f ? 1 : -1, 30.0f, 2, 10.0f, 1);
        if (new_hits(L_KICK, 0)) {
            float x = (float)(rnd() % 27u);
            fire(0, x, 1, 120.0f, 1, 5.0f, 1);
            fire(0, x, -1, 120.0f, 1, 5.0f, 1);
            spark(led_at(0, (int)x), now, 70);
            if (step == 0 && bar != last_bar) {                /* bar line: a wave through the whole net */
                last_bar = bar;
                fire(0, 0.0f, 1, 90.0f, 3, 27.0f, 0);
                fire(1, 26.0f, -1, 90.0f, 3, 27.0f, 0);
            }
        }
        if (new_hits(L_BASS, &note)) spark(led_at(0, key_x(note + 24u)), now, 60);
        if (new_hits(L_HAT, 0)) spark((int)(rnd() % LEDS), now, 35);
        if (new_hits(L_PERC, 0)) { spark((int)(rnd() % LEDS), now, 45); spark((int)(rnd() % LEDS), now, 45); }
        if (new_hits(L_CLAP, 0)) fire(1, rndf() < 0.5f ? 0.0f : 26.0f, 0, 140.0f, 4, 27.0f, 0);
    } else if (now - last_spark > 260u) {                    /* start screen: a resting brain */
        last_spark = now;
        if (rndf() < 0.6f) spark((int)(rnd() % LEDS), now, 90);
        if (rndf() < 0.12f) { float x = (float)(rnd() % 27u); fire(0, x, rndf() < 0.5f ? 1 : -1, 25.0f, 2, 6.0f, 1); }
    }

    uint64_t want = 0;
    for (unsigned i = 0; i < MAX_IMP; ++i) {
        imp_t *p = &imp[i];
        if (!p->on) continue;
        if (p->dir == 0) p->dir = p->x < 13.0f ? 1 : -1;
        float before = p->x;
        p->x += (float)p->dir * p->speed * dt;
        if ((int)before != (int)p->x && p->branches && rndf() < 0.07f) {   /* branch to the other row */
            --p->branches;
            fire(p->row ^ 1u, p->x, rndf() < 0.7f ? p->dir : -p->dir, p->speed * 0.8f, p->tail, 8.0f, 0);
        }
        int head = (int)(p->x + 0.5f);
        for (unsigned t = 0; t <= p->tail; ++t) {
            int led = led_at(p->row, head - p->dir * (int)t);
            if (led >= 0) want |= 1ull << led;
        }
        float tail_end = p->x - (float)p->dir * (float)(p->tail + 1);
        if ((p->dir > 0 && (tail_end > p->stop || tail_end > 26.5f)) || (p->dir < 0 && (tail_end < p->stop || tail_end < -0.5f))) p->on = 0;
    }
    for (unsigned i = 0; i < MAX_FLASH; ++i)
        if ((int32_t)(flash[i].until - now) > 0) want |= 1ull << flash[i].led;

    uint64_t diff = want ^ shown;
    for (unsigned i = 0; diff && i < LEDS; ++i)
        if ((diff >> i) & 1u) { johnny_hal_led(i, (int)((want >> i) & 1u)); diff &= ~(1ull << i); }
    shown = want;
}
