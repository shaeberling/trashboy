// See watcher_screen.h.

#include "watcher_screen.h"

#include <stdlib.h>
#include <string.h>

#define FONT_W 8
#define FONT_H 12
#define CELL_PIXELS (WSCR_CELL_W * WSCR_CELL_H)

// Phosphor white, as on the Waveshare build (trs_screen.cpp), and black.
#define RGB565_BE(r, g, b) \
  ((uint16_t) __builtin_bswap16((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
static const uint16_t FG = RGB565_BE(225, 225, 255);
static const uint16_t BG = 0;

static uint16_t *s_fb = nullptr;
static uint16_t (*s_glyphs)[CELL_PIXELS] = nullptr;  // [256], ready to copy
static uint8_t s_shown[WSCR_COLS * WSCR_ROWS];
static bool s_all_dirty = true;

// Semigraphics: 2x3 blocks of 4x4 font pixels, which become 3x3 pixels each.
// Every pixel of the cell takes a font pixel from inside its own block.
static void fit_graphics(const uint8_t *rows, uint8_t out[WSCR_CELL_H])
{
  for (int y = 0; y < WSCR_CELL_H; y++) {
    const uint8_t src = rows[y * FONT_H / WSCR_CELL_H];
    uint8_t bits = 0;
    for (int x = 0; x < WSCR_CELL_W; x++) {
      const int sx = x * FONT_W / WSCR_CELL_W;
      bits = (uint8_t) ((bits << 1) | ((src >> (FONT_W - 1 - sx)) & 1));
    }
    out[y] = bits;
  }
}

// Text: the Model III glyphs are 6 columns wide (font columns 1..6) and at
// most 8 rows tall, so the top 9 rows fit a cell as they are. Six columns in
// a 6-pixel cell would leave no gap between letters, so each glyph gives up
// one: the two neighbouring columns that differ least are merged. Most
// glyphs have two that are identical ("A", "M", "0" all double their middle
// column), and those come out as clean 5x7 letters. Glyphs wider than 6
// columns ("m", "w", "_") lose more columns the same way.
static void fit_text(const uint8_t *rows, uint8_t out[WSCR_CELL_H])
{
  // Columns as bit patterns over the rows, left to right.
  uint16_t cols[FONT_W];
  int n = 0;
  for (int x = 0; x < FONT_W; x++) {
    uint16_t col = 0;
    for (int y = 0; y < WSCR_CELL_H; y++) {
      col = (uint16_t) ((col << 1) | ((rows[y] >> (FONT_W - 1 - x)) & 1));
    }
    // The outer columns are margin for nearly every glyph: drop them if empty.
    if ((x == 0 || x == FONT_W - 1) && col == 0) continue;
    cols[n++] = col;
  }

  const int target = WSCR_CELL_W - 1;
  while (n > target) {
    int best = 0, best_cost = 1000;
    for (int i = 0; i + 1 < n; i++) {
      // Rows in which exactly one of the two columns is lit; on a tie the
      // pair nearer the middle goes, which keeps the outline.
      const int diff = __builtin_popcount(cols[i] ^ cols[i + 1]);
      const int off_centre = abs(2 * i + 1 - (n - 1));
      const int cost = diff * 16 + off_centre;
      if (cost < best_cost) {
        best_cost = cost;
        best = i;
      }
    }
    cols[best] |= cols[best + 1];
    memmove(&cols[best + 1], &cols[best + 2], (n - best - 2) * sizeof(cols[0]));
    n--;
  }

  for (int y = 0; y < WSCR_CELL_H; y++) {
    uint8_t bits = 0;
    for (int x = 0; x < WSCR_CELL_W; x++) {
      const int lit = (x < n) ? (cols[x] >> (WSCR_CELL_H - 1 - y)) & 1 : 0;
      bits = (uint8_t) ((bits << 1) | lit);
    }
    out[y] = bits;
  }
}

static void build_glyphs(const uint8_t *font)
{
  for (int ch = 0; ch < 256; ch++) {
    uint8_t bits[WSCR_CELL_H];
    if (ch >= 0x80 && ch <= 0xBF) {
      fit_graphics(font + ch * FONT_H, bits);
    } else {
      fit_text(font + ch * FONT_H, bits);
    }
    uint16_t *px = s_glyphs[ch];
    for (int y = 0; y < WSCR_CELL_H; y++) {
      for (int x = 0; x < WSCR_CELL_W; x++) {
        *px++ = (bits[y] >> (WSCR_CELL_W - 1 - x)) & 1 ? FG : BG;
      }
    }
  }
}

bool wscr_init(uint16_t *fb, const uint8_t *font)
{
  s_fb = fb;
  if (s_glyphs == nullptr) {
    s_glyphs = (uint16_t (*)[CELL_PIXELS]) malloc(256 * sizeof(*s_glyphs));
    if (s_glyphs == nullptr) return false;
  }
  build_glyphs(font);
  memset(s_fb, 0, WSCR_FB_W * WSCR_FB_H * sizeof(uint16_t));
  wscr_invalidate();
  return true;
}

void wscr_invalidate(void)
{
  s_all_dirty = true;
}

uint32_t wscr_update(const uint8_t *chars)
{
  uint32_t dirty_rows = 0;
  for (int row = 0; row < WSCR_ROWS; row++) {
    for (int col = 0; col < WSCR_COLS; col++) {
      const int i = row * WSCR_COLS + col;
      const uint8_t ch = chars[i];
      if (ch == s_shown[i] && !s_all_dirty) continue;
      s_shown[i] = ch;

      const uint16_t *src = s_glyphs[ch];
      uint16_t *dst = s_fb + row * WSCR_CELL_H * WSCR_FB_W +
                      WSCR_MARGIN + col * WSCR_CELL_W;
      for (int y = 0; y < WSCR_CELL_H; y++) {
        memcpy(dst, src, WSCR_CELL_W * sizeof(uint16_t));
        dst += WSCR_FB_W;
        src += WSCR_CELL_W;
      }
      dirty_rows |= 1u << row;
    }
  }
  s_all_dirty = false;
  return dirty_rows;
}
