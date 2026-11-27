/* NEURAL JOHNNY - the app: start screen, melody capture, the model's continuation,
 * layer arrangement and the oscillator-line picture. Platform independent:
 * the same file runs on the M-VAVE FM-1 (johnny_fm1.c) and in the browser
 * (WebAssembly). The platform calls johnny_init once and johnny_tick from its
 * control loop (every few ms) and provides johnny_hal_led. */
#pragma once
#include "johnny_port.h"

enum { MM_KEY_LOWEST_MIDI = 53, MM_KEY_COUNT = 27 };      /* F3 .. G5 */
enum { MM_B_OCT_DOWN, MM_B_OCT_UP, MM_B_FX, MM_B_SCL, MM_B_ENV, MM_B_LFO, MM_B_EDIT, MM_B_GLO,
       MM_B_HOME, MM_B_SAVE, MM_B_ARP, MM_B_SEQ, MM_B_PLAY, MM_B_REC, MM_B_COUNT };
/* The 11 drum pads: panel buttons FX, SCL, ENV, LFO, EDIT, GLO, SAVE, ARP,
 * SEQ, PLAY, REC play KICK, SNARE, CLAP, RIM, CLOSED HAT, OPEN HAT, LOW TOM,
 * HIGH TOM, RIDE, SHAKER, COWBELL. OCT- / OCT+ mute and solo, HOME exits. */
static const uint8_t JOHNNY_PAD_BUTTON[11] = { MM_B_FX, MM_B_SCL, MM_B_ENV, MM_B_LFO, MM_B_EDIT, MM_B_GLO,
                                               MM_B_SAVE, MM_B_ARP, MM_B_SEQ, MM_B_PLAY, MM_B_REC };
#define JOHNNY_PAD_MASK 0x3efcu   /* buttons 2..7 and 9..13 */
enum { MM_LCD_W = 80, MM_LCD_H = 80, MM_CELL = 3 };       /* 80 x 80 phone pixels, 3 x 3 each */
enum { MM_VIEW_START, MM_VIEW_WAVES };

void johnny_init(uint32_t now_ms);
/* Knobs, in the order fm1_controls.encoders reports them. */
enum { JK_SELECT, JK_ALGO, JK_K1, JK_K2, JK_K3, JK_K4, JK_PRESET, JK_COUNT };
/* keys: bit n = key n held (F3 + n); buttons: bit n = panel button n held;
 * enc: detents turned since the previous tick (may be NULL). */
void johnny_tick(uint32_t now_ms, uint32_t keys, uint32_t buttons, const int16_t *enc);

/* Phone LCD: 80 x 80 bits, row-major, 10 bytes a row, bit 7 = leftmost. */
const uint8_t *johnny_lcd(void);
unsigned johnny_view(void);
unsigned johnny_lcd_serial(void);           /* changes whenever the picture changes */
/* Rows (in phone pixels) that may change while the lines move. */
enum { MM_WAVE_TOP = 6, MM_WAVE_BOTTOM = 74 };

/* Debug / test helpers. */
unsigned johnny_layers_on(void);            /* mask of layers playing this bar */
unsigned johnny_repeats(void);
const uint8_t *johnny_debug_phrase(unsigned *len, unsigned *tonic);  /* model tokens */

/* ---- provided by the platform ---- */
void johnny_hal_led(unsigned index, int on);  /* 0..13 buttons, 14..40 keys */
