
#ifndef __TRS_SCREEN_H__
#define __TRS_SCREEN_H__

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include <freertos/semphr.h>

extern "C" {
#include "LVGL_Driver.h"
}

#define MODE_NORMAL     (1 << 0)
#define MODE_EXPANDED   (1 << 1)
#define MODE_INVERSE    (1 << 2)
#define MODE_ALTERNATE  (1 << 3)
#define MODE_TEXT_64x16 (1 << 4)
#define MODE_TEXT_80x24 (1 << 5)
#define MODE_GRAFYX     (1 << 6)
#define MODE_TEXT       (MODE_TEXT_64x16 | MODE_TEXT_80x24)

class ScreenBuffer {
private:
  static uint8_t currentMonitorMode;
  
  uint8_t*      screenBuffer;
  uint8_t       width;
  uint8_t       height;
  uint16_t      screen_chars;
  uint8_t       char_width;
  uint8_t       char_height;
  uint8_t*      font;
  bool          isPaintingInverse;
  ScreenBuffer* next;
  lv_obj_t*     canvas;


public:
  ScreenBuffer(uint8_t mode);
  virtual ~ScreenBuffer();
  void setCanvas(lv_obj_t* canvas);
  void setMode(uint8_t mode);
  uint8_t getMode();
  uint8_t* getBuffer();
  uint8_t getWidth();
  uint8_t getHeight();
  uint8_t getCharWidth();
  uint8_t getCharHeight();
  void setNext(ScreenBuffer* next);
  ScreenBuffer* getNext();
  void copyBufferFrom(ScreenBuffer* buf);
  void clear();
  void refresh();
  void update(uint8_t* from, uint8_t* to);
  void setExpanded(int flag);
  void setInverse(int flag);
  int isExpandedMode();
  void drawChar(uint16_t pos, uint8_t character);
  bool getChar(uint16_t pos, uint8_t& character);
};

class TRSCanvas {
private:
  // Canvas pixel buffer (RGB565)
  lv_color_t *canvas_buf;
  lv_coord_t canvas_width = 0;  // Actual canvas/display width (stride)
  lv_coord_t canvas_height = 0; // Actual canvas/display height
  uint8_t font_width;
  uint8_t font_height;
  lv_img_dsc_t img_dsc;  // Image descriptor for rendering

// If you want easy colors:
  lv_color_t fg_color; // e.g., green phosphor
  lv_color_t bg_color; // black

// Precomputed: pattern (0..255) -> 8 RGB565 pixels (as lv_color_t)
  lv_color_t row_lut[256][8];

// Precomputed: glyph (0..255), row (0..11) -> pattern byte
  uint8_t glyph_row_bits[256][12];

  inline uint8_t glyph_bits(uint8_t ch, int row)
  {
    return glyph_row_bits[ch][row];
  }

  void trs80_glyph_init(const unsigned char* font_data)
  {
    // 1) Copy font bytes into glyph_row_bits for fast indexing
    //    font_m3 is laid out: glyph0 row0..row11, glyph1 row0..row11, ...
    for (int ch = 0; ch < 256; ch++) {
        for (int r = 0; r < font_height; r++) {
            glyph_row_bits[ch][r] = font_data[ch * font_height + r];
        }
    }

    // 2) Build LUT: for every possible 8-bit row pattern, expand to 8 pixels of fg/bg
    //    Assume bit7 = leftmost pixel, bit0 = rightmost pixel.
    for (int pat = 0; pat < 256; pat++) {
        for (int x = 0; x < 8; x++) {
            int bit = 7 - x;
            row_lut[pat][x] = (pat & (1 << bit)) ? fg_color : bg_color;
        }
    }
  }

public:
  inline void blit_glyph_to_canvas(uint8_t ch, int cell_x, int cell_y)
  {
    // Top-left pixel in screen space
    const int px0 = cell_x * font_width;
    const int py0 = cell_y * font_height;

    // Destination pointer in the canvas buffer
    // Use canvas_width as stride (full display width), not SCR_W
    lv_color_t *dst = canvas_buf + (py0 * canvas_width + px0);

    // For each glyph row, copy 8 pixels from LUT
    for (int r = 0; r < font_height; r++) {
        uint8_t pat = glyph_bits(ch, r);

        // Copy 8 pixels (unrolled-ish)
        // row_lut[pat] is 8 lv_color_t values
        memcpy(dst, row_lut[pat], font_width * sizeof(lv_color_t));

        dst += canvas_width; // next screen row (use actual canvas width as stride)
    }
  }

public:
  TRSCanvas(lv_color_t* canvas_buf, lv_coord_t canvas_width, lv_coord_t canvas_height, const unsigned char* font_data, uint8_t font_width, uint8_t font_height, lv_color_t fg, lv_color_t bg) {
    this->canvas_buf = canvas_buf;
    this->canvas_width = canvas_width;
    this->canvas_height = canvas_height;
    this->font_width = font_width;
    this->font_height = font_height;
    this->fg_color = fg;
    this->bg_color = bg;
    trs80_glyph_init(font_data);
  }
};

class TRSScreen {
private:
  ScreenBuffer* top;
  lv_color_t *canvas_buf;
  lv_obj_t *canvas;
  lv_coord_t canvasWidth;
  lv_coord_t canvasHeight;
  uint8_t* prevScreenBuffer;
  TRSCanvas* trsCanvas;

public:
  TRSScreen();
  void init();
  void push(ScreenBuffer* screenBuffer);
  void pop();
  void setMode(uint8_t mode);
  uint8_t getMode();
  uint8_t getWidth();
  uint8_t getHeight();
  void enableGrafyxMode(bool enable);
  void setExpanded(int flag);
  void setInverse(int flag);
  bool isTextMode();
  void drawChar(uint16_t pos, uint8_t character);
  bool getChar(uint16_t pos, uint8_t& character);
  void clear();
  void refresh();
  void screenshot();
  void blit_glyph_to_canvas(uint8_t ch, int cell_x, int cell_y);
  void render();
};

extern TRSScreen trs_screen;

#endif
