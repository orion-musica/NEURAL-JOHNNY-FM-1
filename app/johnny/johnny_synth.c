/* NEURAL JOHNNY - hypnotic techno synth and step sequencer (see johnny_synth.h).
 * Everything here after ms_init runs in the audio interrupt, except the small
 * ms_* setters, which only write single words the interrupt reads. */
#include "johnny_synth.h"

#define FS 44100.0f
static volatile float SPT = FS * 60.0f / (float)MS_BPM / 4.0f;   /* samples per 16th (SELECT: tempo) */

/* ------------------------------------------------ control -> interrupt */
static ms_phrase phr[2];
static volatile uint8_t live_buf, pending, pend_hold_release, hold;
static volatile uint8_t pend_pos;
static volatile uint32_t plan_bar = 0xffffffffu;
static volatile uint8_t plan_mask;
static volatile uint8_t running, start_req;

enum { Q_ON, Q_OFF };
#define QN 16
static struct { uint8_t type, midi; } queue[QN];
static volatile uint32_t q_head, q_tail;
static void post(uint8_t type, unsigned midi)
{
    uint32_t h = q_head;
    if (h - q_tail >= QN) return;
    queue[h & (QN - 1)].type = type; queue[h & (QN - 1)].midi = (uint8_t)midi;
    q_head = h + 1;
}

/* ------------------------------------------------ interrupt -> control */
static volatile uint32_t cur_bar, wraps, swaps;
static volatile uint8_t cur_step, cur_pos, mask;
static volatile float step_acc;
static volatile float vis_level[L_COUNT];
static volatile uint8_t vis_pitch[L_COUNT];
static volatile uint8_t hits[L_COUNT], hit_note[L_COUNT];

/* ------------------------------------------------------------ tables */
static float note_hz[128];
static float k_lead, k_lead_rel, k_idx, k_echo, k_kick, k_kick_pitch, k_bass, k_hat,
             k_clap, k_perc, k_perc_idx, k_duck, k_drone_up, k_drone_down, k_live;

static float coef(float tau_s) { return mm_exp_neg(1.0f / (tau_s * FS)); }

/* Knob-driven sound: lead gate, master DJ filter and drive. */
static volatile float lead_gate = 0.55f;
static volatile float fx_lp_g = 1.0f, fx_hp_g = 0.0f, fx_drive = 0.0f;
static float fx_lp[2][2], fx_hp[2][2];

/* ------------------------------------------------------------ voices */
typedef struct { float pc, pm, hz, env, idx, ratio, depth, tone, low, k_idx; int gate; uint8_t midi; } fm_t;
static fm_t lead, live, echo;
static float kick_ph, kick_hz, kick_env, kick_click;
static fm_t bass;
static float hat_env, hat_lp, clap_env, clap_t, clap_lp1, clap_lp2;
static fm_t perc;
static float drone_ph[3], drone_amp, drone_lp1, drone_lp2, lfo_ph, duck;
static uint32_t rng = 0x2545f491u;
static float ksamp;   /* fractional sample counter inside a step */
static uint8_t step_in_bar, pos;
static uint32_t bar_count;

static inline __attribute__((always_inline)) float noise(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}
static inline __attribute__((always_inline)) float softclip(float x) { return x / (1.0f + (x < 0 ? -x : x)); }

static FM1_AUDIO_CODE void fm_trigger(fm_t *v, unsigned midi, float gate_steps)
{
    v->midi = (uint8_t)midi; v->hz = note_hz[midi & 127];
    v->env = 1.0f; v->idx = 1.0f;
    v->gate = (int)(gate_steps * SPT);
}
static FM1_AUDIO_CODE float fm_run(fm_t *v, float k_decay, float k_release)
{
    if (v->env < 0.0005f) { v->env = 0.0f; return 0.0f; }
    v->pm += v->hz * v->ratio * (1.0f / FS); if (v->pm >= 1.0f) v->pm -= 1.0f;
    v->pc += v->hz * (1.0f / FS); if (v->pc >= 1.0f) v->pc -= 1.0f;
    float s = mm_sin01(v->pc + v->depth * (0.25f + v->idx) * mm_sin01(v->pm));
    v->idx *= v->k_idx;
    if (v->gate > 0) { --v->gate; v->env *= k_decay; } else v->env *= k_release;
    v->low += v->tone * (s - v->low);
    return v->low * v->env;
}

/* ------------------------------------------------------------ sequencer */
static FM1_AUDIO_CODE int bit(uint16_t bits, unsigned n) { return (bits >> (n & 15u)) & 1u; }

static FM1_AUDIO_CODE void do_step(void)
{
    if (step_in_bar == 0) {
        if (pending == MS_AT_BAR) {
            live_buf ^= 1u; pos = pend_pos; pending = 0; ++swaps;
            if (pend_hold_release) hold = 0;
        }
        if (plan_bar == bar_count) mask = plan_mask;
    }
    if (pending == MS_ALIGN && phr[live_buf ^ 1u].len) {
        live_buf ^= 1u; pos = (uint8_t)((pend_pos + step_in_bar) & (phr[live_buf].len - 1u)); pending = 0; ++swaps;
        if (pend_hold_release) hold = 0;
    }
    if (pos == 0 && pending == MS_AT_WRAP) {
        live_buf ^= 1u; pending = 0; ++swaps;
        if (pend_hold_release) hold = 0;
    }
    const ms_phrase *p = &phr[live_buf];
    cur_bar = bar_count; cur_step = step_in_bar; cur_pos = pos;
    uint8_t m = mask;
    unsigned s = step_in_bar;
    if (p->len) {
        uint8_t n = p->note[pos & (p->len - 1u)];
        if ((m & (1u << L_LEAD)) && !hold && n) { fm_trigger(&lead, n, lead_gate); hit_note[L_LEAD] = n; ++hits[L_LEAD]; }
        unsigned e = (pos + p->len - 3u) & (p->len - 1u);          /* dotted-8th echo */
        uint8_t en = p->note[e];
        if ((m & (1u << L_ECHO)) && en && ((p->echo_keep[e >> 5] >> (e & 31u)) & 1u)) { fm_trigger(&echo, en + 12u > 108u ? en : en + 12u, 0.4f); hit_note[L_ECHO] = en; ++hits[L_ECHO]; }
    }
    if ((m & (1u << L_KICK)) && (s & 3u) == 0) { kick_env = 1.0f; kick_hz = 165.0f; kick_click = 1.0f; duck = 1.0f; ++hits[L_KICK]; }
    if ((m & (1u << L_BASS)) && bit(p->bass_bits, s)) {
        unsigned r = 36u + p->root;
        fm_trigger(&bass, r, 0.5f); ++hits[L_BASS];
    }
    if ((m & (1u << L_HAT)) && (s & 3u) == 2) { hat_env = 1.0f; ++hits[L_HAT]; }
    if ((m & (1u << L_CLAP)) && (s & 7u) == 4) { clap_env = 1.0f; clap_t = 0.0f; ++hits[L_CLAP]; }
    if ((m & (1u << L_PERC)) && bit(p->perc_bits, s)) {
        unsigned r = 60u + p->root + ((s & 4u) ? 7u : 0u);
        fm_trigger(&perc, r, 0.2f); hit_note[L_PERC] = (uint8_t)r; ++hits[L_PERC];
    }
    if (p->len) { pos = (uint8_t)((pos + 1u) & (p->len - 1u)); if (pos == 0) ++wraps; }
    if (++step_in_bar >= 16) { step_in_bar = 0; ++bar_count; }
}

FM1_AUDIO_CODE void johnny_audio_render(int32_t *out, unsigned frames)
{
    while (q_tail != q_head) {
        uint32_t t = q_tail;
        if (queue[t & (QN - 1)].type == Q_ON) fm_trigger(&live, queue[t & (QN - 1)].midi, 1000.0f);
        else if (live.midi == queue[t & (QN - 1)].midi) live.gate = 0;
        q_tail = t + 1;
    }
    if (start_req) {
        start_req = 0; running = 1;
        step_in_bar = 0; bar_count = 0; pos = 0; ksamp = SPT; mask = 0;
        cur_bar = 0; cur_step = 0; cur_pos = 0;
    }
    uint8_t drone_on = (uint8_t)(running && (mask & (1u << L_DRONE)));
    const ms_phrase *p = &phr[live_buf];
    float droot = note_hz[48u + p->root];
    float lvl[L_COUNT] = {0};
    for (unsigned f = 0; f < frames; ++f) {
        if (running) {
            ksamp += 1.0f;
            if (ksamp >= SPT) { ksamp -= SPT; do_step(); p = &phr[live_buf]; droot = note_hz[48u + p->root]; }
        }
        float side = 1.0f - 0.6f * duck; duck *= k_duck;
        float l = 0.0f, r = 0.0f, v;

        v = fm_run(&lead, k_lead, k_lead_rel) * 0.30f * (0.85f + 0.15f * side); l += v; r += v;
        v = fm_run(&live, k_live, k_lead_rel) * 0.30f; l += v; r += v;
        v = fm_run(&echo, k_echo, k_echo) * 0.13f * side; l += v * 0.6f; r += v;

        if (kick_env > 0.0005f) {
            kick_hz = 46.0f + (kick_hz - 46.0f) * k_kick_pitch;
            kick_ph += kick_hz * (1.0f / FS); if (kick_ph >= 1.0f) kick_ph -= 1.0f;
            float k = softclip(1.8f * mm_sin01(kick_ph)) * kick_env + noise() * kick_click * 0.25f;
            kick_env *= k_kick; kick_click *= 0.93f;
            l += k * 0.55f; r += k * 0.55f;
        }
        v = fm_run(&bass, k_bass, k_bass) * 0.34f * side; l += v; r += v;
        if (hat_env > 0.0005f) {
            float n = noise(); hat_lp += 0.45f * (n - hat_lp);
            v = (n - hat_lp) * hat_env * 0.11f; hat_env *= k_hat;
            l += v * 0.7f; r += v;
        }
        if (clap_env > 0.0005f) {
            clap_t += 1.0f / FS;
            float burst = clap_t < 0.022f ? (mm_sin01(clap_t * 136.0f) > 0.0f ? 1.0f : 0.35f) : 1.0f;
            float n = noise(); clap_lp1 += 0.35f * (n - clap_lp1); clap_lp2 += 0.08f * (clap_lp1 - clap_lp2);
            v = (clap_lp1 - clap_lp2) * clap_env * burst * 0.38f; clap_env *= k_clap;
            l += v; r += v * 0.8f;
        }
        v = fm_run(&perc, k_perc, k_perc) * 0.15f; l += v; r += v * 0.55f;

        float target = drone_on ? 1.0f : 0.0f;
        drone_amp = target + (drone_amp - target) * (drone_on ? k_drone_up : k_drone_down);
        if (drone_amp > 0.0005f) {
            static const float ratio[3] FM1_AUDIO_DATA = {1.0f, 1.4983f, 1.0035f};
            float sw = 0.0f;
            for (unsigned i = 0; i < 3; ++i) {
                drone_ph[i] += droot * ratio[i] * (1.0f / FS); if (drone_ph[i] >= 1.0f) drone_ph[i] -= 1.0f;
                sw += drone_ph[i] * 2.0f - 1.0f;
            }
            lfo_ph += 1.0f / (FS * 14.5f); if (lfo_ph >= 1.0f) lfo_ph -= 1.0f;
            float g = 0.012f + 0.03f * (0.5f + 0.5f * mm_sin01(lfo_ph));
            drone_lp1 += g * (sw - drone_lp1); drone_lp2 += g * (drone_lp1 - drone_lp2);
            v = drone_lp2 * drone_amp * 0.11f * (0.55f + 0.45f * side);
            l += v * 0.8f; r += v;
        }
        {   /* K1: DJ filter (low-pass left, high-pass right); K2: drive */
            float g = fx_lp_g, h = fx_hp_g, x[2] = { l, r };
            for (unsigned c = 0; c < 2; ++c) {
                if (g < 0.999f) {
                    fx_lp[c][0] += g * (x[c] - fx_lp[c][0]); fx_lp[c][1] += g * (fx_lp[c][0] - fx_lp[c][1]);
                    x[c] = fx_lp[c][1];
                } else fx_lp[c][0] = fx_lp[c][1] = x[c];
                if (h > 0.0001f) {
                    fx_hp[c][0] += h * (x[c] - fx_hp[c][0]); fx_hp[c][1] += h * (fx_hp[c][0] - fx_hp[c][1]);
                    x[c] -= fx_hp[c][1];
                } else fx_hp[c][0] = fx_hp[c][1] = 0.0f;
            }
            float d = fx_drive, pre = 1.1f + 7.0f * d, post = 0.9f / (1.0f + 1.2f * d);
            l = softclip(x[0] * pre) * post; r = softclip(x[1] * pre) * post;
        }
        out[2 * f] += (int32_t)(l * 8388000.0f);
        out[2 * f + 1] += (int32_t)(r * 8388000.0f);
    }
    lvl[L_LEAD] = lead.env > live.env ? lead.env : live.env;
    lvl[L_KICK] = kick_env; lvl[L_BASS] = bass.env; lvl[L_HAT] = hat_env; lvl[L_DRONE] = drone_amp;
    lvl[L_CLAP] = clap_env; lvl[L_ECHO] = echo.env; lvl[L_PERC] = perc.env;
    for (unsigned i = 0; i < L_COUNT; ++i) vis_level[i] = lvl[i];
    vis_pitch[L_LEAD] = live.env > lead.env ? live.midi : lead.midi;
    vis_pitch[L_ECHO] = echo.midi; vis_pitch[L_PERC] = perc.midi; vis_pitch[L_BASS] = bass.midi;
    step_acc = ksamp;
}

/* ------------------------------------------------------------ control side */
void ms_init(void)
{
    float hz = 8.1757989f;                     /* MIDI 0 */
    for (unsigned i = 0; i < 128; ++i) { note_hz[i] = hz; hz *= 1.0594630943f; }
    k_lead = coef(0.20f); k_lead_rel = coef(0.035f); k_idx = coef(0.06f); k_echo = coef(0.16f);
    k_live = coef(0.35f);
    k_kick = coef(0.20f); k_kick_pitch = coef(0.028f); k_bass = coef(0.07f); k_hat = coef(0.055f);
    k_clap = coef(0.09f); k_perc = coef(0.06f); k_perc_idx = coef(0.02f); k_duck = coef(0.11f);
    k_drone_up = coef(1.4f); k_drone_down = coef(0.6f);
    lead.ratio = 1.0f; lead.depth = 0.9f; lead.tone = 0.55f; lead.k_idx = k_idx;
    live = lead;
    echo.ratio = 3.5f; echo.depth = 0.45f; echo.tone = 0.35f; echo.k_idx = k_idx;
    bass.ratio = 1.0f; bass.depth = 0.5f; bass.tone = 0.25f; bass.k_idx = k_idx;
    perc.ratio = 1.47f; perc.depth = 1.1f; perc.tone = 0.6f; perc.k_idx = k_perc_idx;
    SPT = FS * 60.0f / (float)MS_BPM / 4.0f; lead_gate = 0.55f; fx_lp_g = 1.0f; fx_hp_g = 0.0f; fx_drive = 0.0f;
    running = 0; mask = 0; hold = 0; pending = 0; plan_bar = 0xffffffffu;
    for (unsigned b = 0; b < 2; ++b) { phr[b].len = 0; phr[b].root = 0; phr[b].bass_bits = phr[b].perc_bits = 0; phr[b].echo_keep[0] = phr[b].echo_keep[1] = 0; }
}
void ms_start(void) { pending = 0; hold = 0; plan_bar = 0xffffffffu; start_req = 1; }
void ms_stop(void) { running = 0; mask = 0; }
int ms_running(void) { return running || start_req; }
ms_phrase *ms_back(void) { return &phr[live_buf ^ 1u]; }
void ms_commit(unsigned when, unsigned start_pos, int release_hold)
{
    pend_pos = (uint8_t)start_pos; pend_hold_release = (uint8_t)(release_hold != 0);
    pending = (uint8_t)when;
}
void ms_cancel(void) { pending = 0; }
int ms_pending(void) { return pending; }
void ms_plan_mask(uint32_t bar, uint8_t m) { plan_bar = 0xffffffffu; plan_mask = m; plan_bar = bar; }
void ms_mask_now(uint8_t m) { mask = m; }
void ms_set_tempo(float bpm) { SPT = FS * 60.0f / bpm / 4.0f; }
void ms_set_lead(float ratio, float depth, float tone, float decay_s, float idx_s, float gate_steps)
{
    float ki = coef(idx_s);
    lead.ratio = ratio; lead.depth = depth; lead.tone = tone; lead.k_idx = ki;
    live.ratio = ratio; live.depth = depth; live.tone = tone; live.k_idx = ki;
    k_lead = coef(decay_s); k_live = coef(decay_s * 1.6f);
    lead_gate = gate_steps;
}
void ms_set_fx(float filter, float drive)
{
    /* filter -1..0: low-pass 18 kHz -> 160 Hz; 0..1: high-pass 20 Hz -> 2.5 kHz */
    if (filter < -0.01f) { float fc = 18000.0f * mm_exp_neg(-filter * 4.72f); fx_lp_g = 1.0f - mm_exp_neg(2.0f * MM_PI * fc / FS); fx_hp_g = 0.0f; }
    else if (filter > 0.01f) { float fc = 20.0f / mm_exp_neg(filter * 4.83f); fx_hp_g = 1.0f - mm_exp_neg(2.0f * MM_PI * fc / FS); fx_lp_g = 1.0f; }
    else { fx_lp_g = 1.0f; fx_hp_g = 0.0f; }
    fx_drive = drive;
}
void ms_lead_hold(int on) { hold = (uint8_t)(on != 0); }
void ms_live_on(unsigned midi) { post(Q_ON, midi); }
void ms_live_off(unsigned midi) { post(Q_OFF, midi); }
void ms_get_clock(ms_clock *c)
{
    uint32_t sw;
    do {
        sw = swaps;
        c->bar = cur_bar; c->step = cur_step; c->pos = cur_pos; c->wraps = wraps;
        c->phase = step_acc / SPT; c->mask = mask; c->swaps = swaps;
    } while (sw != swaps);
    if (c->phase > 1.0f) c->phase = 1.0f;
}
float ms_level(unsigned layer) { return layer < L_COUNT ? vis_level[layer] : 0.0f; }
unsigned ms_hits(unsigned layer, unsigned *note) { if (layer >= L_COUNT) return 0; if (note) *note = hit_note[layer]; return hits[layer]; }
unsigned ms_pitch(unsigned layer) { return layer < L_COUNT ? vis_pitch[layer] : 0u; }
