/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * 3310 - ARRANGE page for FM1 Open (monochrome LCD edition of HASTA LA VISTA).
 *
 * Included once by screen.c, after its drawing helpers and before
 * apply_controls(). Everything here runs on the control task and only uses
 * the public instrument API (instrument.h); the closed engine keeps playing
 * the notes, so timing stays sample accurate.
 *
 * Knobs on the ARRANGE page:
 *   SELECT    track (= layer) 1..8
 *   PRESETS   layer preset: sound and pattern of the selected layer
 *   ALGORITHM track length 16/32/48/64
 *   HOME+K1   mode RHYTHM / GROOVE / SPACE / USER (a short HOME tap: projects)
 *   K1..K4    the mode's four parameters; K1 is a live master effect
 * USER mode: hold EDIT, turn K1..K4 to pick parameters, release EDIT to keep
 * the new map, EDIT+PLAY to cancel. A short EDIT tap opens the sound editor.
 */
#include "hypno.h"
#include "audio_output.h"

enum { HLV_LIVE, HLV_RHYTHM, HLV_GROOVE, HLV_SPACE, HLV_USER, HLV_MODES };
enum {
    HLV_P_HITS, HLV_P_ROTATE, HLV_P_MUTATE,
    HLV_P_GATE, HLV_P_SWING, HLV_P_CHANCE,
    HLV_P_CUTOFF, HLV_P_PUNCH,
    HLV_P_THROW, HLV_P_REVERB, HLV_P_VOLUME,
    HLV_P_RUMBLE, HLV_P_CRUSH, HLV_P_FILTER, HLV_P_BPM, /* live: master FX and tempo */
    HLV_PARAMS
};
enum { HLV_NOTE_MODE, HLV_NOTE_SAVED, HLV_NOTE_CANCEL, HLV_NOTE_USER_ONLY, HLV_NOTE_PRESET };
#define HLV_ORIG 0xffU
#define HLV_RED 0x4287U /* 3310: one ink */
#define HLV_DARK_RED 0x8531U
#define HLV_GREY 0x4287U
#define HLV_OFF 0x8531U

static const char *const hlv_param_names[HLV_PARAMS] = {
    "HITS", "ROTATE", "MUTATE", "GATE", "SWING", "CHANCE",
    "CUTOFF", "PUNCH", "THROW", "REVERB", "VOLUME", "RUMBLE", "CRUSH", "LO/HI CUT", "BPM"};
static const char *const hlv_mode_names[HLV_MODES] = {"LIVE", "RHYTHM", "GROOVE", "SPACE", "USER"};
/* Red, amber, cyan and the USER mode's young-grass green (RGB565). */
/* 3310: every mode uses the LCD ink; the mode chip names the mode. */
static const uint16_t hlv_mode_colors[HLV_MODES] = {HLV_RED, HLV_RED, HLV_RED, HLV_RED, HLV_RED};
/* LIVE: master effects and tempo. The other modes keep the LO/HI CUT
 * filter on K1; the last row is the starting USER map. */
static const uint8_t hlv_factory_map[HLV_MODES][4] = {
    {HLV_P_RUMBLE, HLV_P_CRUSH, HLV_P_FILTER, HLV_P_BPM},
    {HLV_P_FILTER, HLV_P_HITS, HLV_P_ROTATE, HLV_P_MUTATE},
    {HLV_P_FILTER, HLV_P_GATE, HLV_P_SWING, HLV_P_CHANCE},
    {HLV_P_FILTER, HLV_P_CUTOFF, HLV_P_PUNCH, HLV_P_THROW},
    {HLV_P_RUMBLE, HLV_P_HITS, HLV_P_REVERB, HLV_P_VOLUME}};
/* LO/HI CUT one-pole coefficients (Q12) at 15 points: left of zero a low
 * cut from 25 Hz to 1.6 kHz, right of zero a high cut from 16 kHz to 200 Hz. */
static const uint16_t hlv_cut_table[2][15] = {
    {15, 20, 26, 35, 48, 64, 86, 115, 154, 206, 275, 366, 484, 638, 835},
    {3677, 3323, 2886, 2416, 1962, 1553, 1205, 921, 696, 522, 389, 288, 213, 157, 115}};
static uint8_t hlv_fx[2]; /* RUMBLE, CRUSH 0..100 */
static int8_t hlv_filter; /* -100 low cut .. +100 high cut */
/* Three-letter layer tags for the rows. */
static const char hlv_tags[8][4] = {"ATM", "KIK", "RMB", "HAT", "PRC", "TOP", "SEQ", "CLP"};

static unsigned hlv_mode;
static uint8_t hlv_user[4], hlv_pending[4], hlv_preset[GROOVE_TRACKS];
static unsigned hlv_edit_down, hlv_edit_used, hlv_edit_knob, hlv_home_down, hlv_home_used;
static unsigned hlv_note_kind;
static uint32_t hlv_note_at;
static uint32_t hlv_seed;

/* Rhythm generator state. The base is the user's own pattern, captured the
 * first time a neutral track is touched; HITS/ROTATE/MUTATE always derive
 * from it, so returning every knob to neutral restores the original notes. */
static struct groove_step hlv_base[GROOVE_TRACKS][GROOVE_STEPS];
static struct groove_step hlv_out[GROOVE_STEPS], hlv_scratch[GROOVE_STEPS];
static uint8_t hlv_based[GROOVE_TRACKS], hlv_lane[GROOVE_TRACKS];
static uint8_t hlv_hits[GROOVE_TRACKS], hlv_rot[GROOVE_TRACKS];
static int8_t hlv_mut[GROOVE_TRACKS], hlv_cut[GROOVE_TRACKS];
static uint8_t hlv_chance[GROOVE_TRACKS], hlv_gate[GROOVE_TRACKS];
static uint8_t hlv_last_step[GROOVE_TRACKS];
/* THROW raises the delay send while the knob moves and returns it after. */
static uint8_t hlv_throw_amount[GROOVE_TRACKS], hlv_throw_base[GROOVE_TRACKS], hlv_throw_on[GROOVE_TRACKS];
static uint32_t hlv_throw_until[GROOVE_TRACKS];
static uint8_t hlv_patch[156];

static void hlv_init(void)
{
    /* Called once at start; everything not set here starts at zero. */
    memcpy(hlv_user, hlv_factory_map[HLV_USER], sizeof hlv_user);
    memcpy(hlv_pending, hlv_user, sizeof hlv_pending);
    memset(hlv_preset, 0xff, sizeof hlv_preset);
    hlv_edit_knob = 4;
    hlv_seed = 0x48c27a13U;
    memset(hlv_hits, HLV_ORIG, sizeof hlv_hits);
    memset(hlv_chance, 8, sizeof hlv_chance);
    memset(hlv_gate, 8, sizeof hlv_gate);
}
static unsigned hlv_rand(void)
{
    hlv_seed ^= hlv_seed << 13;
    hlv_seed ^= hlv_seed >> 17;
    hlv_seed ^= hlv_seed << 5;
    return hlv_seed;
}
static void hlv_note(unsigned kind, uint32_t now)
{
    hlv_note_kind = kind;
    hlv_note_at = now ? now : 1U;
}
static const uint8_t *hlv_map(void)
{
    if (hlv_mode == HLV_USER) return hlv_edit_down ? hlv_pending : hlv_user;
    return hlv_factory_map[hlv_mode];
}

/* ---- Rhythm generator: HITS, ROTATE, MUTATE ---------------------------- */
static unsigned hlv_is_hit(unsigned t, const struct groove_step *cell)
{
    if (groove_pad_count(t)) return (cell->drums >> hlv_lane[t]) & 1U;
    return cell->velocity != 0;
}
static unsigned hlv_neutral(unsigned t)
{
    return hlv_hits[t] == HLV_ORIG && !hlv_rot[t] && !hlv_mut[t];
}
static void hlv_snapshot(unsigned t)
{
    groove_sequence_read(t, 0, hlv_base[t], GROOVE_STEPS);
    hlv_lane[t] = (uint8_t)(t == track && groove_pad_count(t) ? drum : 0);
    hlv_based[t] = 1;
}
static unsigned hlv_length(unsigned t)
{
    unsigned length = groove_sequence_length(t);
    return length && length <= GROOVE_STEPS ? length : 16U;
}
static unsigned hlv_base_hits(unsigned t)
{
    unsigned n = 0, length = hlv_length(t);
    if (!hlv_based[t]) {
        struct groove_view v = {0};
        groove_get_view(t, 0, &v);
        for (unsigned s = 0; s < length; ++s) n += (unsigned)((v.active_steps >> s) & 1U);
        return n;
    }
    for (unsigned s = 0; s < length; ++s) n += hlv_is_hit(t, &hlv_base[t][s]);
    return n;
}
/* Euclidean spread of hits over the track length, then rotation. */
static void hlv_generate(unsigned t)
{
    unsigned length = hlv_length(t), drums = groove_pad_count(t) != 0;
    uint16_t bit = (uint16_t)(1U << hlv_lane[t]);
    memcpy(hlv_out, hlv_base[t], sizeof hlv_out);
    if (hlv_hits[t] != HLV_ORIG) {
        unsigned found = 0, used = 0, hits = hlv_hits[t] > length ? length : hlv_hits[t];
        for (unsigned s = 0; s < length; ++s)
            if (hlv_is_hit(t, &hlv_base[t][s])) hlv_scratch[found++] = hlv_base[t][s];
        for (unsigned s = 0; s < length; ++s) {
            unsigned on = (s * hits) % length < hits;
            if (drums) {
                if (on) hlv_out[s].drums = (uint16_t)(hlv_out[s].drums | bit);
                else hlv_out[s].drums = (uint16_t)(hlv_out[s].drums & ~bit);
            } else if (on) {
                if (found) hlv_out[s] = hlv_scratch[used++ % found];
                else {
                    hlv_out[s].note = (uint8_t)(t == track && selected_note < 128 ? selected_note : 60);
                    hlv_out[s].velocity = 100;
                    hlv_out[s].drums = 0;
                }
            } else hlv_out[s].velocity = 0;
        }
    }
    if (hlv_rot[t] % length) {
        memcpy(hlv_scratch, hlv_out, sizeof hlv_scratch);
        for (unsigned s = 0; s < length; ++s) hlv_out[(s + hlv_rot[t]) % length] = hlv_scratch[s];
    }
}
static void hlv_set_hit(unsigned t, struct groove_step *cell, unsigned on)
{
    if (groove_pad_count(t)) {
        uint16_t bit = (uint16_t)(1U << hlv_lane[t]);
        cell->drums = (uint16_t)(on ? cell->drums | bit : cell->drums & ~bit);
    } else if (!on) cell->velocity = 0;
    else if (!cell->velocity) cell->velocity = 100;
}
/* ERODE: drop hits from the generated pattern, fresh on every loop.
 * EVOLVE: move hits of the live pattern, so the changes accumulate. */
static void hlv_mutate(unsigned t)
{
    unsigned length = hlv_length(t);
    int amount = hlv_mut[t];
    if (amount < 0) {
        hlv_generate(t);
        for (unsigned s = 0; s < length; ++s)
            if (hlv_is_hit(t, &hlv_out[s]) && hlv_rand() % 10U < (unsigned)-amount)
                hlv_set_hit(t, &hlv_out[s], 0);
    } else if (amount > 0) {
        groove_sequence_read(t, 0, hlv_out, GROOVE_STEPS);
        if (hlv_rand() % 8U >= (unsigned)amount) return;
        unsigned moves = (unsigned)(amount + 1) / 2;
        for (unsigned m = 0; m < moves; ++m) {
            unsigned from = hlv_rand() % length, to = hlv_rand() % length;
            if (from == to || !hlv_is_hit(t, &hlv_out[from]) || hlv_is_hit(t, &hlv_out[to])) continue;
            if (groove_pad_count(t)) {
                hlv_set_hit(t, &hlv_out[from], 0);
                hlv_set_hit(t, &hlv_out[to], 1);
            } else {
                struct groove_step cell = hlv_out[to];
                hlv_out[to] = hlv_out[from];
                hlv_out[from] = cell;
            }
        }
    } else return;
    groove_sequence_replace(t, 0, hlv_out, length);
}
static void hlv_apply(unsigned t)
{
    if (!hlv_based[t]) hlv_snapshot(t);
    hlv_generate(t);
    groove_sequence_replace(t, 0, hlv_out, hlv_length(t));
}
/* The first touch of a neutral track captures the user's latest edits. */
static void hlv_rhythm_touch(unsigned t)
{
    if (hlv_neutral(t)) hlv_snapshot(t);
}

/* ---- Step settings: GATE (setting 1) and CHANCE (setting 2) ------------- */
static void hlv_step_settings(unsigned t, unsigned setting, unsigned target)
{
    struct groove_view v = {0};
    groove_get_view(t, 0, &v);
    unsigned length = hlv_length(t);
    for (unsigned s = 0; s < length; ++s) {
        if (!((v.active_steps >> s) & 1U)) continue;
        int current = (int)groove_step_setting(t, s, setting);
        if (current != (int)target) groove_step_setting_edit(t, s, setting, (int)target - current);
    }
}

/* ---- Parameter knobs ---------------------------------------------------- */
static void hlv_param_turn(unsigned p, int delta, uint32_t now)
{
    unsigned t = track, length = hlv_length(t);
    int step = delta > 0 ? 1 : -1;
    switch (p) {
    case HLV_P_HITS: {
        hlv_rhythm_touch(t);
        /* Left past zero returns to ORIG, the user's own pattern; ORIG is the left end. */
        if (hlv_hits[t] == HLV_ORIG && delta < 0) break;
        int hits = (hlv_hits[t] == HLV_ORIG ? (int)hlv_base_hits(t) : (int)hlv_hits[t]) + delta;
        hlv_hits[t] = hits < 0 ? (uint8_t)HLV_ORIG : (uint8_t)clamp(hits, 0, (int)length);
        hlv_apply(t);
        break;
    }
    case HLV_P_ROTATE:
        hlv_rhythm_touch(t);
        hlv_rot[t] = (uint8_t)(((int)hlv_rot[t] + delta % (int)length + (int)length) % (int)length);
        hlv_apply(t);
        break;
    case HLV_P_MUTATE:
        hlv_rhythm_touch(t);
        hlv_mut[t] = (int8_t)clamp(hlv_mut[t] + step, -8, 8);
        if (!hlv_mut[t]) hlv_apply(t); /* HOLD restores the generated pattern. */
        break;
    case HLV_P_GATE:
        if (groove_pad_count(t)) { notice = "MELODIC ONLY"; notice_until = now + 1500U; break; }
        hlv_gate[t] = (uint8_t)clamp(hlv_gate[t] + step, 1, 15);
        hlv_step_settings(t, 1, hlv_gate[t]);
        break;
    case HLV_P_SWING:
        groove_sequence_setting_edit(0, delta);
        break;
    case HLV_P_CHANCE:
        hlv_chance[t] = (uint8_t)clamp(hlv_chance[t] + step, 1, 8);
        hlv_step_settings(t, 2, hlv_chance[t] == 8 ? 0U : hlv_chance[t]);
        break;
    case HLV_P_CUTOFF:
        if (groove_source_type(t) == GROOVE_DX7) { notice = "NOT ON DX7"; notice_until = now + 1500U; break; }
        groove_control_edit(t, 0, 0, delta);
        hlv_cut[t] = (int8_t)clamp(hlv_cut[t] + delta, -99, 99);
        break;
    case HLV_P_PUNCH: {
        if (groove_source_type(t) != GROOVE_DX7) { notice = "DX7 ONLY"; notice_until = now + 1500U; break; }
        struct groove_fm_controls controls;
        memset(&controls, 0, sizeof controls);
        if (!groove_fm_copy(t, hlv_patch, &controls) && !controls.enabled) groove_adsr_enable(t, 1);
        groove_adsr_edit(t, 1, delta);
        break;
    }
    case HLV_P_THROW: {
        uint8_t sends[2];
        groove_sends_get(t, sends);
        if (!hlv_throw_on[t]) { hlv_throw_base[t] = sends[1]; hlv_throw_on[t] = 1; }
        hlv_throw_amount[t] = (uint8_t)clamp((int)hlv_throw_amount[t] + step, 0, 15);
        int target = hlv_throw_amount[t] > hlv_throw_base[t] ? hlv_throw_amount[t] : hlv_throw_base[t];
        if (target != sends[1]) groove_sends_edit(t, 1, target - (int)sends[1]);
        hlv_throw_until[t] = now + 1500U;
        break;
    }
    case HLV_P_REVERB:
        groove_sends_edit(t, 0, step);
        break;
    case HLV_P_VOLUME:
        groove_control_edit(t, 0, delta, 0);
        break;
    case HLV_P_BPM:
        groove_tempo_edit(delta);
        break;
    case HLV_P_FILTER: {
        /* Live and global, not saved. Coefficients interpolate the table. */
        int f = hlv_filter = (int8_t)clamp(hlv_filter + delta, -100, 100), v = f < 0 ? -f : f;
        unsigned q = v ? (unsigned)(v - 1) * 896U / 99U : 0, i = q >> 6;
        const uint16_t *table = hlv_cut_table[f > 0];
        int a = table[i] + (i < 14 ? ((int)table[i + 1] - (int)table[i]) * (int)(q & 63U) / 64 : 0);
        fm1_cut[0] = (uint16_t)(f < 0 ? a : 0);
        fm1_cut[1] = (uint16_t)(f > 0 ? a : 0);
        break;
    }
    default: { /* RUMBLE, CRUSH */
        unsigned k = p - HLV_P_RUMBLE;
        hlv_fx[k] = (uint8_t)clamp(hlv_fx[k] + delta, 0, 100);
        fm1_fx[k] = (uint8_t)(k ? (hlv_fx[k] * 15U + 50U) / 100U : hlv_fx[k]);
        break;
    }
    }
}
static void hlv_param_value(unsigned p, const struct groove_view *v, char out[12])
{
    unsigned t = track, type = groove_source_type(t);
    int value = 0, pct = -1; /* pct: value is shown with % */
    const char *word = 0;
    switch (p) {
    case HLV_P_HITS:
        if (hlv_hits[t] == HLV_ORIG) word = "ORIG";
        else value = hlv_hits[t];
        break;
    case HLV_P_ROTATE: value = hlv_rot[t]; break;
    case HLV_P_MUTATE:
        value = hlv_mut[t];
        if (!value) word = "HOLD";
        break;
    case HLV_P_GATE:
        if (groove_pad_count(t)) word = "--";
        else pct = hlv_gate[t] == 15 ? 100 : (hlv_gate[t] * 100 + 8) / 16;
        break;
    case HLV_P_SWING: pct = (int)groove_sequence_setting(0); break;
    case HLV_P_CHANCE: pct = (hlv_chance[t] * 100 + 4) / 8; break;
    case HLV_P_CUTOFF:
        if (type == GROOVE_DX7) word = "--";
        else value = hlv_cut[t];
        break;
    case HLV_P_PUNCH: {
        struct groove_fm_controls controls;
        memset(&controls, 0, sizeof controls);
        if (type != GROOVE_DX7) word = "--";
        else if (groove_fm_copy(t, hlv_patch, &controls) || !controls.enabled) word = "PATCH";
        else value = controls.envelope[1];
        break;
    }
    case HLV_P_THROW:
        if (!hlv_throw_amount[t]) word = "OFF";
        else pct = (hlv_throw_amount[t] * 100 + 7) / 15;
        break;
    case HLV_P_REVERB: {
        uint8_t sends[2];
        groove_sends_get(t, sends);
        pct = (sends[0] * 100 + 7) / 15;
        break;
    }
    case HLV_P_VOLUME: value = v->volume; break;
    case HLV_P_BPM:
        if (v->clock_mode && !v->tempo) word = "EXT";
        else value = v->tempo;
        break;
    case HLV_P_FILTER:
        value = hlv_filter;
        if (!value) word = "OFF";
        break;
    default:
        pct = hlv_fx[p - HLV_P_RUMBLE];
        if (!pct) word = "OFF";
        break;
    }
    if (word) { strcpy(out, word); return; }
    if (pct >= 0) { decimal(out, pct, 1); strcat(out, "%"); return; }
    /* Signed values: MUTATE is negative for ERODE, positive for EVOLVE. */
    out[0] = 0;
    if (value > 0 && p != HLV_P_HITS && p != HLV_P_VOLUME && p != HLV_P_PUNCH && p != HLV_P_BPM) strcpy(out, "+");
    decimal(out + strlen(out), value, 1);
}

/* ---- Controls ----------------------------------------------------------- */
/* Runs early in apply_controls so EDIT/PLAY are not taken by other gestures.
 * Returns 1 when the event was consumed. */
static int hlv_edit_gesture(const struct fm1_controls *c, uint32_t pressed, uint32_t now,
                            unsigned *play_handled)
{
    if (page != HLV) { hlv_edit_down = 0; return 0; }
    if (!hlv_edit_down) {
        if (!(pressed & BUTTON(EDIT)) || (c->buttons & ~BUTTON(EDIT))) return 0;
        hlv_edit_down = 1;
        hlv_edit_used = 0;
        hlv_edit_knob = 4;
        memcpy(hlv_pending, hlv_user, sizeof hlv_pending);
        return 1;
    }
    if (pressed & BUTTON(PLAY)) {
        *play_handled = 1;
        hlv_edit_down = 0;
        hlv_edit_knob = 4;
        memcpy(hlv_pending, hlv_user, sizeof hlv_pending);
        if (hlv_mode == HLV_USER) hlv_note(HLV_NOTE_CANCEL, now);
        return 1;
    }
    if (!(c->buttons & BUTTON(EDIT))) {
        hlv_edit_down = 0;
        hlv_edit_knob = 4;
        if (hlv_edit_used) {
            if (hlv_mode == HLV_USER) {
                memcpy(hlv_user, hlv_pending, sizeof hlv_user);
                hlv_note(HLV_NOTE_SAVED, now);
            }
            return 1;
        }
        /* A short tap keeps the original EDIT meaning: open the sound editor. */
        unsigned edit = groove_source_type(track) == GROOVE_DX7 ? FM_QUICK : TONE_EDIT;
        page = edit;
        advanced_edit = 0;
        if (page == TONE_EDIT) tone_pages[track][tone_slot()] = 0;
        if (page == TONE_EDIT && groove_pad_count(track)) drum_parts[track] = (uint8_t)drum;
        return 1;
    }
    for (unsigned i = 0; i < 4; ++i) {
        int delta = c->encoders[2 + i];
        if (!delta) continue;
        hlv_edit_used = 1;
        if (hlv_mode != HLV_USER) { hlv_note(HLV_NOTE_USER_ONLY, now); continue; }
        int next = clamp((int)hlv_pending[i] + (delta > 0 ? 1 : -1), 0, HLV_PARAMS - 1);
        for (unsigned j = 0; j < 4; ++j)
            if (j != i && hlv_pending[j] == (uint8_t)next) hlv_pending[j] = hlv_pending[i];
        hlv_pending[i] = (uint8_t)next;
        hlv_edit_knob = i;
    }
    return 1;
}
/* HOME held on ARRANGE shows the mode slider and K1 moves it. Projects and
 * presets are in the SAVE menu. Other HOME combinations keep their meaning.
 * Runs before the panel shortcuts; returns 1 when the event was consumed. */
static int hlv_home_gesture(const struct fm1_controls *c, uint32_t pressed, uint32_t now)
{
    if (page != HLV) { hlv_home_down = 0; return 0; }
    if (!hlv_home_down) {
        if (!(pressed & BUTTON(HOME)) || (c->buttons & ~BUTTON(HOME))) return 0;
        hlv_home_down = 1;
        hlv_home_used = 0;
        return 1;
    }
    if (!(c->buttons & BUTTON(HOME))) { hlv_home_down = 0; return 1; }
    if (c->encoders[2]) {
        hlv_home_used = 1;
        unsigned mode = (unsigned)clamp((int)hlv_mode + c->encoders[2], 0, HLV_MODES - 1);
        if (mode != hlv_mode) { hlv_mode = mode; hlv_note(HLV_NOTE_MODE, now); }
        return 1;
    }
    if ((c->buttons & ~BUTTON(HOME)) || encoder_active(c->encoders)) hlv_home_used = 1;
    return 0;
}
/* PRESETS: the next preset of this track's layer, built into the scratch
 * project and applied to this track only (sound, pattern, length; undoable). */
static void hlv_layer_preset(unsigned t, int delta, uint32_t now)
{
    int n = (int)hyp_count(t), i = hlv_preset[t] == 0xffU ? (delta > 0 ? -1 : 0) : hlv_preset[t];
    i = ((i + delta) % n + n) % n;
    struct groove_project *p = groove_project_scratch();
    int r = groove_project_capture(p);
    if (!r) {
        hyp_build(t, (unsigned)i, &p->preset[t], p->step[t]);
        r = groove_timbre_apply(t, p);
    }
    if (r) { notice = r == GROOVE_PROJECT_MEMORY ? "NO MEMORY FOR SOUND" : "PRESET FAILED"; notice_until = now + 1500U; return; }
    unsigned pages = p->preset[t].sequence_pages;
    groove_sequence_length_set(t, pages ? pages * 16U : 64U);
    hlv_preset[t] = (uint8_t)i;
    hlv_based[t] = 0; hlv_hits[t] = HLV_ORIG; hlv_rot[t] = 0; hlv_mut[t] = 0; hlv_cut[t] = 0;
    hlv_chance[t] = hlv_gate[t] = 8;
    hlv_note(HLV_NOTE_PRESET, now);
}
static void hlv_controls(const struct fm1_controls *c, uint32_t now)
{
    select_track(c->encoders[0]);
    if (c->encoders[1]) {
        sequence_length_adjust(c->encoders[1]);
        if (!hlv_neutral(track)) hlv_apply(track);
    }
    if (c->encoders[6]) hlv_layer_preset(track, c->encoders[6] > 0 ? 1 : -1, now);
    const uint8_t *map = hlv_map();
    for (unsigned i = 0; i < 4; ++i)
        if (c->encoders[2 + i])
            hlv_param_turn(map[i], fm_encoder_delta(2 + i, c->encoders[2 + i]), now);
}
/* Loop-synchronous work: MUTATE at each track's loop start, THROW release. */
static void hlv_poll(uint32_t now)
{
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        if (hlv_throw_on[t] && (int32_t)(now - hlv_throw_until[t]) >= 0) {
            uint8_t sends[2];
            groove_sends_get(t, sends);
            if (sends[1] != hlv_throw_base[t]) groove_sends_edit(t, 1, (int)hlv_throw_base[t] - (int)sends[1]);
            hlv_throw_on[t] = 0;
            hlv_throw_amount[t] = 0;
        }
        if (!hlv_mut[t]) continue;
        struct groove_view v = {0};
        groove_get_view(t, 0, &v);
        if (v.playing && v.play_step < hlv_last_step[t]) {
            if (!hlv_based[t]) hlv_snapshot(t);
            hlv_mutate(t);
        }
        hlv_last_step[t] = v.play_step;
    }
}

/* ---- SAVE menu ---------------------------------------------------------- */
/* 1 SAVE PROJECT, 2 LOAD PROJECT (user slots), 3 LOAD PRESET (the sixteen
 * HYPNOTIC sets), 4 NEW PROJECT (YES / NO first). SELECT, K1 or PRESETS move,
 * keys 1-4 pick, SAVE opens, HOME goes back. */
static unsigned hlv_menu_item, hlv_menu_confirm;
static void hlv_menu_controls(uint32_t pressed, uint32_t key_down, uint32_t now, int move)
{
    blocked_keys |= key_down; /* a menu key must not also play a note */
    unsigned ok = (pressed & BUTTON(SAVE)) != 0, no = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (key_down & (1U << white_keys[i])) {
            if (hlv_menu_confirm) { ok = i == 0; no = i != 0; }
            else { hlv_menu_item = i; ok = 1; }
        }
    if (hlv_menu_confirm) {
        if (no || move) { hlv_menu_confirm = 0; return; }
        if (!ok || !release_keys()) return;
        hlv_menu_confirm = 0;
        int r = groove_project_new();
        notice = r ? "NEW PROJECT FAILED" : "NEW PROJECT"; notice_until = now + 2000U;
        if (!r) { preset_loaded = 0; memset(hlv_preset, 0xff, sizeof hlv_preset); memset(hlv_based, 0, sizeof hlv_based); }
        page = music_page = HLV;
        return;
    }
    if (move) hlv_menu_item = (unsigned)clamp((int)hlv_menu_item + move, 0, 3);
    if (!ok) return;
    if (!hlv_menu_item) {
        project_return = HLV; page = PROJECT; project_result = 0;
        project_reload = project_operation = 0;
        project_slot = groove_project_slot(); project_factory = name_cursor = 0;
        project_refresh();
    } else if (hlv_menu_item < 3) {
        preset_bank = hlv_menu_item - 1U;
        preset_slot = preset_bank ? (preset_current_bank ? preset_current_slot : 0) : groove_project_slot();
        project_result = 0; preset_refresh(); page = PRESET_BROWSER;
    } else hlv_menu_confirm = 1;
}
static void hlv_menu_draw(void)
{
    char name[13];
    groove_project_name(name);
    uint32_t sig = signature(hlv_menu_item | (hlv_menu_confirm << 2), name, 12);
    if (cache->hlv[0] == sig) return;
    cache->hlv[0] = sig;
    paint_fill(0, 0, 240, 240, BACK);
    text(6, 8, "PROJECT", 2, TEXT, BACK);
    text(6, 32, name, 1, DIM, BACK);
    paint_fill(0, 44, 240, 2, TEXT);
    if (hlv_menu_confirm) {
        paint_fill(10, 60, 220, 70, TEXT);
        text(22, 72, "SAVED SLOTS STAY", 1, BACK, TEXT);
        text(22, 92, "NEW PROJECT?", 2, BACK, TEXT);
        frame(10, 144, 104, 40, TEXT); frame(126, 144, 104, 40, TEXT);
        paint_fill(13, 147, 98, 34, TEXT);
        text(44, 156, "YES", 2, BACK, TEXT);
        text(164, 156, "NO", 2, TEXT, BACK);
        text(6, 224, "KEY1/SAVE YES  KEY2/HOME NO", 1, DIM, BACK);
        return;
    }
    static const char *const items[4] = {"SAVE PROJECT", "LOAD PROJECT", "LOAD PRESET", "NEW PROJECT"};
    for (unsigned i = 0; i < 4; ++i) {
        unsigned y = 54 + i * 38, on = i == hlv_menu_item;
        paint_fill(6, y, 228, 32, on ? TEXT : PANEL);
        number(14, y + 9, (int)i + 1, 1, 2, on ? BACK : DIM, on ? TEXT : PANEL);
        text(40, y + 9, items[i], 2, on ? BACK : TEXT, on ? TEXT : PANEL);
    }
    text(6, 210, "SELECT / KEYS 1-4", 1, DIM, BACK);
    text(6, 224, "SAVE = OPEN  HOME = BACK", 1, DIM, BACK);
}

/* ---- LED show: SNAKE on the keyboard ------------------------------------ */
/* While ARRANGE plays, a snake crawls over the 27 key LEDs one key per
 * sixteenth, bouncing at the ends and hunting a blinking dot. Eating grows
 * it; at ten keys the board flashes and it starts small again. Every kick
 * inverts the whole keyboard, every clap throws the dot somewhere new, each
 * track flashes its own panel button on its hits and the six remaining
 * buttons run a ring that turns round every bar. */
static uint8_t hlv_snake[10] = {2, 1, 0}, hlv_snake_len = 3, hlv_food = 20, hlv_led_step = 0xff, hlv_flash, hlv_clap;
static int8_t hlv_dir = 1;
static int hlv_leds(const struct groove_view *v)
{
    static const uint8_t pulse[8] = {FX, PLAY, REC, OCT_UP, SCL, OCT_DOWN, ARP, HOME};
    static const uint8_t ring[6] = {ENV, LFO, EDIT, GLO, SAVE, SEQ};
    unsigned clock = 0xffU, hits = 0;
    for (unsigned t = 0; t < 8; ++t) {
        if (clock == 0xffU && v[t].playing) clock = v[t].play_step;
        if (v[t].audible && v[t].level >= 64) hits |= 1U << t;
    }
    if (clock == 0xffU) return 0;
    if (clock != hlv_led_step) {
        hlv_led_step = (uint8_t)clock;
        int head = hlv_snake[0] + hlv_dir;
        if (head < 0 || head > 26) { hlv_dir = (int8_t)-hlv_dir; head = hlv_snake[0] + hlv_dir; }
        memmove(hlv_snake + 1, hlv_snake, sizeof hlv_snake - 1);
        hlv_snake[0] = (uint8_t)head;
        if (head == hlv_food) {
            if (++hlv_snake_len > 9) { hlv_snake_len = 3; hlv_flash = 4; }
            hlv_food = (uint8_t)(hlv_rand() % 27U);
        }
        if (hlv_flash) --hlv_flash;
    }
    if ((hits & 0x80U) && !hlv_clap) hlv_food = (uint8_t)(hlv_rand() % 27U);
    hlv_clap = (uint8_t)(hits >> 7);
    uint32_t keys = 0, buttons = 0;
    for (unsigned i = 0; i < hlv_snake_len; ++i) keys |= 1U << hlv_snake[i];
    if ((frame_time / 90U) & 1U) keys |= 1U << hlv_food;
    if ((hits & 2U) || hlv_flash) keys ^= 0x7ffffffU;
    for (unsigned t = 0; t < 8; ++t)
        if ((hits >> t) & 1U) buttons |= 1U << pulse[t];
    unsigned r = clock % 6U;
    buttons |= 1U << ring[(clock / 16U) & 1U ? 5U - r : r];
    uint32_t changed = keys ^ led_cache;
    for (unsigned key = 0; key < 27; ++key)
        if (changed & (1U << key)) fm1_led_set(key + 14, (keys >> key) & 1U);
    led_cache = keys;
    changed = buttons ^ button_led_cache;
    for (unsigned key = 0; key < 14; ++key)
        if (changed & (1U << key)) fm1_led_set(key, (buttons >> key) & 1U);
    button_led_cache = (uint16_t)buttons;
    return 1;
}

/* ---- Drawing (240 x 240) ------------------------------------------------ */
enum { HLV_C_HEAD, HLV_C_INFO, HLV_C_ROW, HLV_C_ZOOM = HLV_C_ROW + 8, HLV_C_BOX, HLV_C_HINT, HLV_C_COUNT };
static unsigned hlv_level_frame(const struct groove_view v[8])
{
    for (unsigned t = 0; t < 8; ++t)
        if (v[t].audible && v[t].level >= 64) return 1;
    return 0;
}
static unsigned hlv_box_active(void)
{
    if (hlv_edit_down || hlv_home_down) return 1;
    return hlv_note_at && frame_time - hlv_note_at < 800U;
}
static void hlv_center(unsigned y, const char *s, unsigned scale, uint16_t fg, uint16_t bg)
{
    unsigned w = (unsigned)strlen(s) * 6U * scale;
    text(w < 180 ? 30 + (180 - w) / 2 : 30, y, s, scale, fg, bg);
}
/* 3310: one 12x12 phone symbol per layer: ATMOS note, KICK bell, RUMBLE
 * battery, HATS keypad, PERC handset, TOPS signal, SEQ snake, CLAP envelope.
 * Bit 11 is the leftmost pixel. */
static const uint16_t hlv_icons[8][12] = {
    {0x0fe, 0x0fe, 0x082, 0x082, 0x082, 0x082, 0x082, 0x38e, 0x79e, 0x79e, 0x30c, 0x000} /* note */,
    {0x060, 0x0f0, 0x1f8, 0x1f8, 0x1f8, 0x3fc, 0x3fc, 0x7fe, 0xfff, 0x000, 0x060, 0x060} /* bell */,
    {0x0f0, 0x3fc, 0x204, 0x2f4, 0x204, 0x2f4, 0x204, 0x2f4, 0x204, 0x2f4, 0x204, 0x3fc} /* battery */,
    {0xeee, 0xeee, 0x000, 0xeee, 0xeee, 0x000, 0xeee, 0xeee, 0x000, 0xeee, 0xeee, 0x000} /* keypad */,
    {0xf00, 0xf08, 0xc04, 0xc44, 0xc22, 0xc22, 0xc22, 0xc44, 0xc04, 0xf08, 0xf00, 0x000} /* handset */,
    {0xf80, 0x200, 0x203, 0x203, 0x203, 0x21b, 0x21b, 0x21b, 0x2db, 0x2db, 0x2db, 0x000} /* signal */,
    {0x004, 0x00e, 0xfce, 0xfc0, 0xc00, 0xc00, 0xffc, 0xffc, 0x00c, 0x00c, 0x1fc, 0x1fc} /* snake */,
    {0x000, 0xfff, 0xc03, 0xa05, 0x909, 0x891, 0x861, 0x801, 0x801, 0xfff, 0x000, 0x000} /* envelope */
};
static void hlv_icon(unsigned x, unsigned y, unsigned t, uint16_t fg, uint16_t bg)
{
    paint_fill(x, y, 14, 14, bg);
    for (unsigned r = 0; r < 12; ++r) {
        uint16_t bits = hlv_icons[t & 7U][r];
        for (unsigned c = 0; c < 12; ) {
            if (!(bits & (0x800U >> c))) { ++c; continue; }
            unsigned start = c;
            while (c < 12 && (bits & (0x800U >> c))) ++c;
            paint_fill(x + 1 + start, y + 1 + r, c - start, 1, fg);
        }
    }
}
static void hlv_draw_box(uint16_t mc)
{
    /* x 30..209 / 8..231, y 48..175: whole 16-row bands. */
    if (paint_rendering && (paint_top >= 176 || paint_top + PAINT_ROWS <= 48)) return;
    unsigned active = hlv_box_active(), k = hlv_edit_knob;
    uint32_t sig = signature(active | (hlv_note_kind << 1) | (hlv_mode << 4) | (hlv_edit_down << 6) |
        ((uint32_t)k << 7) | ((uint32_t)hlv_preset[track] << 10) | ((uint32_t)track << 18) | (hlv_home_down << 21), hlv_pending, 4);
    if (sig == cache->hlv[HLV_C_BOX]) return;
    cache->hlv[HLV_C_BOX] = sig;
    if (!active) return; /* Rows repaint themselves when the box closes. */
    (void)k; /* The knob labels show the picks (marked *); no list, to save Flash. */
    /* Notes: a small title, a big middle line and a small hint. */
    static const char *const tops[] = {"MODE", "USER", "USER", "USER", "PRESET"};
    static const char *const mids[] = {"", "SAVED", "CANCELLED", "USER ONLY", ""};
    static const char *const subs[] = {"", "", "", "HOME+K1 > USER", ""};
    unsigned kind = hlv_home_down ? HLV_NOTE_MODE : hlv_edit_down ? HLV_NOTE_USER_ONLY : hlv_note_kind, big = 2;
    const char *top = tops[kind], *mid = mids[kind], *sub = subs[kind];
    char line[40]; /* four names and spaces, up to 30 characters */
    if (hlv_edit_down && hlv_mode == HLV_USER) { top = "EDIT"; mid = "TURN K1-K4"; sub = "LET GO SAVE  +PLAY UNDO"; }
    else if (kind == HLV_NOTE_MODE) {
        mid = hlv_mode_names[hlv_mode];
        line[0] = 0;
        for (unsigned i = 0; i < 4; ++i) { strcat(line, hlv_param_names[hlv_map()[i]]); strcat(line, " "); }
        sub = line;
    } else if (kind == HLV_NOTE_PRESET) {
        top = hyp_name(track);
        decimal(line, hlv_preset[track] + 1, 2); mid = line; big = 3;
    }
    uint16_t bg = kind == HLV_NOTE_MODE || kind == HLV_NOTE_SAVED ? mc : PANEL, fg = bg == PANEL ? TEXT : BACK;
    paint_fill(30, 64, 180, 96, bg);
    hlv_center(76, top, 1, fg, bg);
    hlv_center(kind == HLV_NOTE_MODE ? 92 : 96, mid, big, fg, bg);
    hlv_center(kind == HLV_NOTE_MODE ? 144 : 136, sub, 1, fg, bg);
    if (kind == HLV_NOTE_MODE) {
        /* The mode slider: five stops, the current one is a big block. */
        paint_fill(36, 76, 168, 8, bg);
        paint_fill(40, 129, 160, 1, fg);
        for (unsigned i = 0; i < HLV_MODES; ++i) {
            unsigned x = 40 + i * 40, on = i == hlv_mode;
            if (x > 196) x = 196;
            paint_fill(on ? x - 4 : x - 2, on ? 124 : 127, on ? 9 : 5, on ? 11 : 5, fg);
        }
        hlv_center(76, hlv_home_down ? "HOLD HOME  TURN K1" : "MODE", 1, fg, bg);
    }
}
static void hlv_draw(const struct groove_view v[8])
{
    const struct groove_view *sel = &v[track];
    unsigned mode = hlv_mode;
    uint16_t mc = hlv_mode_colors[mode];
    unsigned lit_frame = hlv_level_frame(v), box = hlv_box_active();
    /* Header: title, mode chip, tempo and the 2-frame level LED. */
    if (!(paint_rendering && paint_top >= 30)) {
        uint32_t sig = mode | (lit_frame << 2) | ((uint32_t)sel->tempo << 3) | ((uint32_t)sel->clock_mode << 13);
        if (sig != cache->hlv[HLV_C_HEAD]) {
            cache->hlv[HLV_C_HEAD] = sig;
            paint_fill(0, 0, 240, 27, BACK);
            text(8, 8, "ARRANGE", 2, TEXT, BACK);
            paint_fill(98, 6, 64, 16, mc);
            text(98 + (64 - 6 * (unsigned)strlen(hlv_mode_names[mode])) / 2, 11, hlv_mode_names[mode], 1, BACK, mc);
            if (sel->clock_mode && !sel->tempo) text(168, 11, "EXT", 1, DIM, BACK);
            else number(168, 11, sel->tempo, 3, 1, DIM, BACK);
            text(190, 11, "BPM", 1, DIM, BACK);
            paint_fill(222, 8, 12, 12, lit_frame ? mc : PANEL);
        }
    }
    /* Eight rows: phone icon (inverts on hits), source, 4 sections x 4 bars. */
    for (unsigned t = 0; t < 8; ++t) {
        unsigned y = 46 + t * 16;
        if (paint_rendering && (y >= paint_top + PAINT_ROWS || y + 14 <= paint_top) && (t || paint_top != 32)) continue;
        unsigned length = v[t].sequence_length ? v[t].sequence_length : 16;
        uint32_t bars = 0;
        for (unsigned b = 0; b < 16; ++b)
            if ((v[t].active_steps >> (b * 4)) & 0xfU) bars |= 1U << b;
        unsigned playing_bar = v[t].playing ? v[t].play_step / 4U : 31U;
        unsigned lit = v[t].audible && v[t].level >= 64;
        unsigned tag = mode == HLV_GROOVE ? hlv_chance[t] : length;
        uint32_t meta = (uint32_t)playing_bar | ((length / 16U) << 5) | ((t == track) << 8) | (lit << 9) |
            (mode << 10) | (box << 12) | ((uint32_t)v[t].muted << 13) | ((uint32_t)v[t].type << 14) | (tag << 17);
        uint32_t sig = signature(meta, &bars, sizeof bars);
        if (sig == cache->hlv[HLV_C_ROW + t]) continue;
        cache->hlv[HLV_C_ROW + t] = sig;
        paint_fill(0, t ? y : 30, 240, t ? 14 : 30, BACK); /* row 0 also clears the gap above */
        if (t == track) paint_fill(0, y, 3, 14, mc);
        uint16_t bg = lit ? mc : PANEL;
        hlv_icon(6, y, t, lit ? BACK : TEXT, bg); /* 2-frame: inverts on hits */
        text(24, y + 4, hlv_tags[t], 1, t == track ? mc : v[t].muted ? DIM : TEXT, BACK);
        for (unsigned b = 0; b < 16; ++b) {
            unsigned x = 46 + (b / 4) * 42 + (b % 4) * 10;
            if (b * 4 >= length) { frame(x, y + 2, 8, 10, HLV_OFF); continue; }
            /* One ink: the playing bar is drawn as an outline around its fill. */
            uint16_t color = (bars >> b) & 1U ? mc : PANEL;
            if (b == playing_bar) {
                paint_fill(x, y + 2, 8, 10, TEXT);
                paint_fill(x + 2, y + 4, 4, 6, (bars >> b) & 1U ? DIM : BACK);
            } else paint_fill(x, y + 2, 8, 10, color);
        }
        char value[12];
        if (mode == HLV_GROOVE) {
            decimal(value, hlv_chance[t] == 8 ? 100 : (int)((hlv_chance[t] * 100U + 4U) / 8U), 1);
        } else decimal(value, (int)length, 1);
        text(216, y + 4, value, 1, DIM, BACK);
    }
    /* Zoom: the sixteen steps of the section the selected track plays. */
    if (!(paint_rendering && (paint_top >= 194 || paint_top + PAINT_ROWS <= 178))) {
        unsigned length = sel->sequence_length ? sel->sequence_length : 16;
        unsigned at = sel->playing ? sel->play_step : cursor % length, first = at & ~15U;
        uint64_t lane = groove_pad_count(track) ? groove_drum_steps(track, drum) : sel->active_steps;
        uint32_t bits = (uint32_t)((lane >> first) & 0xffffU);
        uint32_t sig = signature(bits | ((uint32_t)first << 16) | ((uint32_t)(at & 15U) << 22) | (mode << 26) |
            ((uint32_t)sel->playing << 28), &length, sizeof length);
        if (sig != cache->hlv[HLV_C_ZOOM]) {
            cache->hlv[HLV_C_ZOOM] = sig;
            paint_fill(0, 178, 240, 16, BACK);
            for (unsigned i = 0; i < 16; ++i) {
                unsigned x = 8 + i * 14 + (i / 4) * 2, s = first + i;
                if (s >= length) { frame(x, 180, 12, 12, HLV_OFF); continue; }
                unsigned on = (bits >> i) & 1U, head = sel->playing && s == at;
                if (head) {
                    paint_fill(x, 180, 12, 12, TEXT);
                    paint_fill(x + 3, 183, 6, 6, on ? DIM : BACK);
                } else paint_fill(x, 180, 12, 12, on ? mc : PANEL);
            }
        }
    }
    hlv_draw_box(mc);
    /* The four parameters of this mode (the mode itself is in the header). */
    const uint8_t *map = hlv_map();
    for (unsigned i = 0; i < 4; ++i) {
        char label[12], value[12];
        strcpy(label, hlv_param_names[map[i]]);
        if (mode == HLV_USER && hlv_edit_down && hlv_pending[i] != hlv_user[i]) strcat(label, "*");
        hlv_param_value(map[i], sel, value);
        knob(i, label, value, mc);
    }
    if (!(paint_rendering && paint_top + PAINT_ROWS <= 229)) {
        const char *hint = notice ? notice : hlv_edit_down ? "EDIT HELD" : "PRE PRESET  ALG LEN  HOME+K1 MODE";
        uint32_t sig = signature(0, hint, (unsigned)strlen(hint));
        if (sig != cache->hlv[HLV_C_HINT]) {
            cache->hlv[HLV_C_HINT] = sig;
            paint_fill(0, 229, 240, 11, BACK);
            text(8, 231, hint, 1, DIM, BACK);
        }
    }
}
