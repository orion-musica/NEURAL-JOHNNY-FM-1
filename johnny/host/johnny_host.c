/* NEURAL JOHNNY - PC test: a scripted player plays a motif, lets the model run
 * for a while, plays a second motif, and the result is written as a WAV,
 * a bar-by-bar log and a few screen dumps (raw RGB565, 240 x 240).
 *   make -C johnny/host && ./johnny/host/johnny_host out_dir */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "johnny_app.h"
#include "johnny_synth.h"
#include "johnny_screen.h"

static uint64_t led_state; static unsigned led_writes;
void johnny_hal_led(unsigned index, int on) { if (on) led_state |= 1ull << index; else led_state &= ~(1ull << index); ++led_writes; }

typedef struct { double ms; int key; int dur_steps; } ev;
#define STEP_MS (60000.0 / 132.0 / 4.0)

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[512];
    int bars_total = argc > 2 ? atoi(argv[2]) : 84;
    /* motif 1 (A minor): A3 . . C4 . . E4 . A3 . . G3 . . E4 .  -> keys from F3 (53) */
    static const int m1[][2] = { {0, 57}, {3, 60}, {6, 64}, {8, 57}, {11, 55}, {14, 64} };
    /* motif 2 at bar 52 (D phrygian-ish, 2 bars) */
    static const int m2[][2] = { {0, 62}, {2, 62}, {4, 63}, {7, 65}, {10, 62}, {12, 69}, {16, 62}, {19, 63}, {22, 60} };
    ev evs[64]; int ne = 0;
    double t0 = 1000.0;
    for (unsigned i = 0; i < sizeof m1 / sizeof m1[0]; ++i) evs[ne++] = (ev){ t0 + m1[i][0] * STEP_MS + (i % 2 ? 9 : -6), m1[i][1] - 53, 1 };
    double t1 = t0 + 52 * 16 * STEP_MS + 3 * STEP_MS;   /* starts on step 3 of a bar */
    for (unsigned i = 0; i < sizeof m2 / sizeof m2[0]; ++i) evs[ne++] = (ev){ t1 + m2[i][0] * STEP_MS + (i % 3 ? 7 : -8), m2[i][1] - 53, 1 };

    snprintf(path, sizeof path, "%s/johnny.wav", dir);
    FILE *wav = fopen(path, "wb");
    snprintf(path, sizeof path, "%s/johnny_log.txt", dir);
    FILE *log = fopen(path, "w");
    if (!wav || !log) { perror("open"); return 1; }
    unsigned frames_total = (unsigned)((t0 / 1000.0 + bars_total * 16 * STEP_MS / 1000.0) * 44100.0);
    frames_total -= frames_total % 128;
    uint32_t hdr[11] = { 0x46464952, 36 + frames_total * 4, 0x45564157, 0x20746d66, 16, 0x00020001, 44100, 44100 * 4, 0x00100004, 0x61746164, frames_total * 4 };
    fwrite(hdr, 4, 11, wav);

    johnny_init(0);
    int32_t block[256]; int16_t pcm[256];
    uint32_t keys = 0; double now = 0.0; unsigned last_bar = 0xffffffffu;
    int dumps[16] = { 500, 6000, 30000, 60000, 90000, 120000 }; unsigned nd = 0, ndumps = 6;
    const char *shots = getenv("SHOTS");          /* SHOTS="500,9000,..." in ms, ascending */
    if (shots) { ndumps = 0; for (const char *q = shots; *q && ndumps < 16; ) { dumps[ndumps++] = atoi(q); while (*q && *q != ',') ++q; if (*q) ++q; } }
    static uint16_t band[240 * 240];
    float peak = 0.0f; double sumsq = 0.0;
    for (unsigned f = 0; f < frames_total; f += 128) {
        now = f * 1000.0 / 44100.0;
        keys = 0;
        for (int i = 0; i < ne; ++i)
            if (now >= evs[i].ms && now < evs[i].ms + evs[i].dur_steps * STEP_MS * 0.7) keys |= 1u << evs[i].key;
        int16_t enc[7] = {0};
        /* knob script: filter sweep down and back in bars 40-47, sound morph, mutate up */
        unsigned tick_n = f / 128;
        if (now > t0 + 40 * 16 * STEP_MS && now < t0 + 44 * 16 * STEP_MS && tick_n % 160 == 0) enc[2] = -1;
        if (now > t0 + 44 * 16 * STEP_MS && now < t0 + 48 * 16 * STEP_MS && tick_n % 160 == 0) enc[2] = 1;
        if (now > t0 + 20 * 16 * STEP_MS && now < t0 + 30 * 16 * STEP_MS && tick_n % 400 == 0) enc[6] = 1;
        if (now > t0 + 60 * 16 * STEP_MS && tick_n % 600 == 0) enc[4] = 1;
        johnny_tick((uint32_t)now, keys, 0, enc);
        memset(block, 0, sizeof block);
        johnny_audio_render(block, 128);
        for (unsigned i = 0; i < 256; ++i) {
            float v = block[i] / 8388608.0f;
            if (v > peak) peak = v;
            if (-v > peak) peak = -v;
            sumsq += v * v;
            pcm[i] = (int16_t)(block[i] >> 8);
        }
        fwrite(pcm, 2, 256, wav);
        ms_clock c; ms_get_clock(&c);
        if (ms_running() && c.bar != last_bar) {
            last_bar = c.bar;
            fprintf(log, "bar %3u  layers ", c.bar);
            static const char *nm = "LKBHDCEP";
            for (int l = 0; l < 8; ++l) fputc((c.mask >> l) & 1 ? nm[l] : '.', log);
            fprintf(log, "  leds %2d writes %5u repeats %u  ", __builtin_popcountll(led_state), led_writes, johnny_repeats());
            unsigned len, tonic; const uint8_t *t = johnny_debug_phrase(&len, &tonic);
            static const char *pc[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            for (unsigned i = 0; i < len; ++i) {
                if (i && i % 16 == 0) fputc('|', log);
                if (!t[i]) fputs(" .", log);
                else { int m = (int)tonic + t[i] - 13; fprintf(log, " %s%d", pc[m % 12], m / 12 - 1); }
            }
            fputc('\n', log);
        }
        if (nd < ndumps && now >= dumps[nd]) {
            johnny_screen_band(0, 240, band);
            snprintf(path, sizeof path, "%s/screen_%d.raw", dir, dumps[nd]);
            FILE *s = fopen(path, "wb"); fwrite(band, 2, 240 * 240, s); fclose(s);
            ++nd;
        }
    }
    fprintf(log, "peak %.3f  rms %.4f\n", peak, (float)__builtin_sqrt(sumsq / (frames_total * 2.0)));
    fclose(wav); fclose(log);
    return 0;
}
