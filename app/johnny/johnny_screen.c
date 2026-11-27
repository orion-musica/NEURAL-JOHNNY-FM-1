/* NEURAL JOHNNY - two-tone phone LCD renderer. Every phone pixel is a 3 x 3
 * square: 2 x 2 of colour and a faint grid line, like the old green LCDs.
 * Rendered in horizontal bands so it needs no full frame buffer. */
#include "johnny_screen.h"
#include "johnny_app.h"

#define LCD_BG   0xc79bu   /* RGB 199,240,216 pale green */
#define LCD_GRID 0xbf19u   /* a shade darker: the pixel grid */
#define LCD_INK  0x4287u   /* RGB 67,82,61 dark olive */
#define LCD_INK2 0x6b4cu   /* the gap between two dark pixels */

void johnny_screen_band(unsigned y0, unsigned rows, uint16_t *pixels)
{
    const uint8_t *lcd = johnny_lcd();
    for (unsigned r = 0; r < rows; ++r) {
        unsigned y = y0 + r, ly = y / MM_CELL, sy = y % MM_CELL;
        uint16_t *row = pixels + (size_t)r * JOHNNY_SCREEN_W;
        const uint8_t *line = lcd + (size_t)(ly < MM_LCD_H ? ly : MM_LCD_H - 1) * (MM_LCD_W / 8);
        for (unsigned lx = 0; lx < MM_LCD_W; ++lx) {
            int on = (line[lx >> 3] >> (7 - (lx & 7))) & 1;
            uint16_t c = on ? LCD_INK : LCD_BG, g = on ? LCD_INK2 : LCD_GRID;
            uint16_t *p = row + lx * MM_CELL;
            if (sy == 2) { p[0] = g; p[1] = g; p[2] = g; }
            else { p[0] = c; p[1] = c; p[2] = g; }
        }
    }
}
