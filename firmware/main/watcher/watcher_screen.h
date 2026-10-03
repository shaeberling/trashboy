#pragma once

#include <stdint.h>

// The TRS-80's 64x16 text screen for the SenseCAP Watcher's round 412 px
// panel.
//
// The largest 8:3 rectangle that fits the circle is 384x144: 6x9 pixels per
// character cell, 3/4 of the 8x12 cell the Model III font is drawn in.
// Semigraphics characters (2x3 blocks per cell) scale exactly, to 3x3 pixels
// per block. Text is not scaled but refitted: see build_glyphs().
//
// This file knows nothing about the panel; it turns the character buffer
// into pixels and says which rows changed. watcher_main.cpp sends them.

#define WSCR_COLS    64
#define WSCR_ROWS    16
#define WSCR_CELL_W  6
#define WSCR_CELL_H  9

// The SPD2010 only takes windows that start and end on 4-pixel columns. The
// 384 visible columns would start at x = 14, so the frame buffer carries a
// 2-pixel black margin on each side and goes to x = 12..399.
#define WSCR_MARGIN  2
#define WSCR_FB_W    (WSCR_COLS * WSCR_CELL_W + 2 * WSCR_MARGIN)  // 388
#define WSCR_FB_H    (WSCR_ROWS * WSCR_CELL_H)                    // 144

// `fb` is WSCR_FB_W x WSCR_FB_H RGB565 pixels in the panel's byte order (high
// byte first). `font` is 256 glyphs of 12 rows, one byte per row, bit 7 on
// the left (ptrs/font/font_m3). Fills the frame buffer with black. Returns
// false if out of memory.
bool wscr_init(uint16_t *fb, const uint8_t *font);

// Draw every cell of `chars` (WSCR_COLS x WSCR_ROWS) that differs from what
// the frame buffer shows. Returns a bit mask of the text rows that changed
// (bit 0 = top row); 0 if the picture is up to date.
uint32_t wscr_update(const uint8_t *chars);

// Forget what the frame buffer shows: the next wscr_update() draws all of it.
void wscr_invalidate(void);
