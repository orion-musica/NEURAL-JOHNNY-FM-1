/* NEURAL JOHNNY - M-VAVE FM-1 adapter (fm1_open SDK).
 * Connects the platform-independent app to the FM-1 panel, LCD and LEDs.
 * Not part of the PC test or the browser version.
 *
 * Hook-up (NEURAL_JOHNNY in johnny_fm1.h), the same places Echo Ludwig uses:
 *   app/screen.c        groove_screen_init()    -> johnny_fm1_init()
 *   app/screen.c        groove_screen_control() -> johnny_fm1_control()
 *   app/audio_output.c  fm1_output_mix() starts with johnny_audio_render(output, FM1_BLOCK_FRAMES) */
#include "fm1.h"
#include "fm1_engine_control.h"
#include "johnny_app.h"
#include "johnny_screen.h"
#include "johnny_fm1.h"
#include <string.h>

#include "../instrument.h"

int groove_master_update(unsigned adc); /* engine: MASTER knob -> output level */

/* The app replaces the base firmware's UI, so the linker would drop the
 * runtime pieces only that UI used. sdk/Build.ps1 requires them in every
 * image (Bluetooth, tuning, project edits): keep them referenced. */
static const void *const johnny_keep_linked[] __attribute__((used)) = {
    (const void *)fm1_ble_set_role, (const void *)fm1_ble_scan, (const void *)fm1_ble_connect,
    (const void *)fm1_ble_disconnect, (const void *)fm1_ble_get_status, (const void *)fm1_ble_get_device,
    (const void *)fm1_bluetooth_configure, (const void *)fm1_bluetooth_get_status,
    (const void *)groove_tuning_capture, (const void *)groove_tuning_apply, (const void *)groove_project_apply_edit,
};
const void *const *johnny_keep_linked_table(void) { return johnny_keep_linked; }

volatile uint8_t johnny_active = 1;

/* The picture is redrawn in bands of 4 phone rows (12 screen rows); only the
 * bands whose phone pixels changed since the last frame are sent. */
enum { BAND_LCD_ROWS = 4, BAND_ROWS = BAND_LCD_ROWS * MM_CELL, FRAME_MS = 40 };
static uint16_t band[FM1_SCREEN_WIDTH * BAND_ROWS];
static uint8_t shown[MM_LCD_H][MM_LCD_W / 8];
static unsigned shown_serial = ~0u, shown_view = ~0u;
static uint32_t last_frame;

void johnny_hal_led(unsigned index, int on) { fm1_led_set(index, on); }

static void redraw(uint32_t now)
{
    unsigned serial = johnny_lcd_serial(), view = johnny_view();
    if (serial == shown_serial) return;
    int full = view != shown_view;
    if (!full && now - last_frame < FRAME_MS) return;
    last_frame = now;
    const uint8_t *lcd = johnny_lcd();
    for (unsigned b = 0; b < MM_LCD_H / BAND_LCD_ROWS; ++b) {
        const uint8_t *rows = lcd + b * BAND_LCD_ROWS * (MM_LCD_W / 8);
        if (!full && !memcmp(rows, shown[b * BAND_LCD_ROWS], BAND_LCD_ROWS * (MM_LCD_W / 8))) continue;
        memcpy(shown[b * BAND_LCD_ROWS], rows, BAND_LCD_ROWS * (MM_LCD_W / 8));
        johnny_screen_band(b * BAND_ROWS, BAND_ROWS, band);
        fm1_screen_blit(0, b * BAND_ROWS, FM1_SCREEN_WIDTH, BAND_ROWS, band, FM1_SCREEN_WIDTH * BAND_ROWS);
    }
    shown_serial = serial; shown_view = view;
}

void johnny_fm1_init(void)
{
    fm1_controls_init();
    fm1_screen_init();
    fm1_engine_pause(); fm1_engine_stop(); fm1_engine_resume(); /* the FM instrument stays silent */
    for (unsigned i = 0; i < 41; ++i) fm1_led_set(i, 0);
    shown_serial = ~0u; shown_view = ~0u;
    johnny_init(fm1_millis());
    redraw(fm1_millis());
}

void johnny_fm1_control(void)
{
    struct fm1_controls c;
    fm1_controls_read(&c);
    groove_master_update(c.knob);              /* MASTER knob keeps working as volume */
    uint32_t now = fm1_millis();
    johnny_tick(now, c.keys & 0x07ffffffu, c.buttons & 0x3fffu);
    redraw(now);
}
