/* NEURAL JOHNNY - WebAssembly glue: the browser runs the very same app, model,
 * synth and screen code as the FM-1 firmware. */
#include <stddef.h>
#include <stdint.h>
#include "johnny_app.h"
#include "johnny_synth.h"
#include "johnny_screen.h"

#define EXPORT(name) __attribute__((export_name(name)))
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = d; const unsigned char *q = s;
    if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; --n, ++p, ++q) if (*p != *q) return *p - *q;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }

static int32_t block[256];
static float audio_out[2 * 1024];
static uint16_t band[240 * 16];
static uint32_t rgba[240 * 240];

EXPORT("init") void w_init(uint32_t now) { johnny_init(now); }
EXPORT("tick") void w_tick(uint32_t now, uint32_t keys, uint32_t buttons) { johnny_tick(now, keys, buttons); }
/* frames: a multiple of 128, at most 1024. Returns interleaved L/R floats. */
EXPORT("render") float *w_render(unsigned frames)
{
    if (frames > 1024) frames = 1024;
    for (unsigned f = 0; f < frames; f += 128) {
        for (unsigned i = 0; i < 256; ++i) block[i] = 0;
        johnny_audio_render(block, 128);
        for (unsigned i = 0; i < 256; ++i) audio_out[f * 2 + i] = (float)block[i] * (1.0f / 8388608.0f);
    }
    return audio_out;
}
EXPORT("serial") unsigned w_serial(void) { return johnny_lcd_serial(); }
EXPORT("layers") unsigned w_layers(void) { return johnny_layers_on(); }
EXPORT("screen") uint32_t *w_screen(void)
{
    for (unsigned y = 0; y < 240; y += 16) {
        johnny_screen_band(y, 16, band);
        for (unsigned i = 0; i < 240 * 16; ++i) {
            uint16_t p = band[i];
            uint32_t r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
            rgba[y * 240 + i] = 0xff000000u | (b << 16) | (g << 8) | r;
        }
    }
    return rgba;
}
