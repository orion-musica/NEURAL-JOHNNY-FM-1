/* NEURAL JOHNNY - two-tone phone LCD: 80 x 80 square pixels on the 240 x 240 screen. */
#pragma once
#include "johnny_port.h"
enum { JOHNNY_SCREEN_W = 240, JOHNNY_SCREEN_H = 240 };
/* Render screen rows y0 .. y0+rows-1 as native RGB565 into pixels (240 * rows). */
void johnny_screen_band(unsigned y0, unsigned rows, uint16_t *pixels);
