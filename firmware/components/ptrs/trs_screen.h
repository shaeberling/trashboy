
#ifndef __TRS_SCREEN_H__
#define __TRS_SCREEN_H__

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include <freertos/semphr.h>
#include "esp_lcd_types.h"

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
  // CONFIG_TRASHBOY_DISPLAY_ROTATE_180: draw the screen the other way up.
#if CONFIG_TRASHBOY_DISPLAY_ROTATE_180
  static constexpr bool kRotate180 = true;
#else
  static constexpr bool kRotate180 = false;
#endif

  // Canvas pixel buffer (RGB565)
  lv_color16_t *canvas_buf;
  lv_coord_t canvas_width = 0;  // Actual canvas/display width (stride)
  lv_coord_t canvas_height = 0; // Actual canvas/display height
  uint8_t font_width;
  uint8_t font_height;


  lv_color16_t fg_color;
  lv_color16_t bg_color;

  // Precomputed: pattern (0..4095) -> 12 RGB565 pixels (rotated 90° CCW)
  lv_color16_t col_lut[4096][12];

  // Precomputed: glyph (0..255), column (0..7) -> 12-bit pattern of that column (top to bottom)
  uint16_t glyph_col_bits[256][8];

  inline uint16_t glyph_bits(uint8_t ch, int col)
  {
    return glyph_col_bits[ch][col];
  }

  void trs80_glyph_init(const unsigned char* font_data)
  {
    // 1) Extract columns from font and store as bit patterns
    //    For 90° CCW rotation: extract each column (0..7) from top to bottom
    //    Column 0 (leftmost) in original becomes row 7 (bottom) after rotation
    //    Column 7 (rightmost) in original becomes row 0 (top) after rotation
    for (int ch = 0; ch < 256; ch++) {
        for (int col = 0; col < font_width; col++) {
            uint16_t col_pattern = 0;
            // Read bits top-to-bottom in this column
            for (int row = 0; row < font_height; row++) {
                uint8_t row_byte = font_data[ch * font_height + row];
                // Bit 7 is leftmost, bit 0 is rightmost
                int bit = font_width - 1 - col;
                if (row_byte & (1 << bit)) {
                    col_pattern |= (1 << (font_height - 1 - row));
                }
            }
            glyph_col_bits[ch][col] = col_pattern;
        }
    }

    // 2) Build LUT: for every possible 12-bit column pattern, expand to 12 pixels of fg/bg
    //    Each pattern represents one column of the rotated glyph (12 pixels tall after rotation)
    for (int pat = 0; pat < 4096; pat++) {
        for (int y = 0; y < font_height; y++) {
            int bit = font_height - 1 - y;
            col_lut[pat][y] = (pat & (1 << bit)) ? fg_color : bg_color;
        }
    }
  }

public:
  // The layout this canvas draws: a 64x16 text screen, centered. The pixel
  // math below is only valid inside it.
  static constexpr int TEXT_COLS = 64;
  static constexpr int TEXT_ROWS = 16;

  // Native-pixel rectangle covered by text cell (cell_x, cell_y).
  //
  // After 90° CCW rotation a glyph (8 wide × 12 tall) is rendered 12 wide ×
  // 8 tall, and doubled along native x: 24 × 8 native pixels. Text columns
  // run along native y (bottom to top), text rows along native x.
  //
  // Returns false for a cell outside the 64x16 layout: nothing may be drawn
  // there, the math would run off the buffer.
  inline bool cell_area(int cell_x, int cell_y, lv_area_t *area) const
  {
    if (cell_x < 0 || cell_x >= TEXT_COLS || cell_y < 0 || cell_y >= TEXT_ROWS) {
      return false;
    }
    const int offset_x = (canvas_width - (2 * 12 * 16)) / 2;
    const int offset_y = (canvas_height - (8 * 64)) / 2;

    area->x1 = cell_y * font_height * 2 + offset_x;
    area->x2 = area->x1 + 2 * font_height - 1;
    area->y2 = canvas_height - 1 - cell_x * font_width - offset_y;
    area->y1 = area->y2 - (font_width - 1);
    if (kRotate180) {
      // CONFIG_TRASHBOY_DISPLAY_ROTATE_180: the same cell, mirrored on both
      // native axes.
      const lv_area_t a = *area;
      area->x1 = canvas_width - 1 - a.x2;
      area->x2 = canvas_width - 1 - a.x1;
      area->y1 = canvas_height - 1 - a.y2;
      area->y2 = canvas_height - 1 - a.y1;
    }
    return true;
  }

  // Draw glyph `ch` at text cell (cell_x, cell_y) into `buf`, which must be
  // a canvas_width × canvas_height RGB565 buffer (the canvas buffer, or the
  // panel frame buffer — same geometry). Returns false if the cell is
  // outside the layout and nothing was drawn.
  inline bool blit_glyph(lv_color16_t *buf, uint8_t ch, int cell_x, int cell_y)
  {
    lv_area_t a;
    if (!cell_area(cell_x, cell_y, &a)) {
      return false;
    }

    // For each column of the original glyph (becomes row in rotated space)
    // Original column 0 (leftmost) → rotated row 7 (bottom)
    // Original column 7 (rightmost) → rotated row 0 (top)
    for (int col = 0; col < font_width; col++) {
        uint16_t pat = glyph_bits(ch, col);
        lv_color16_t *pattern = col_lut[pat];

        if (kRotate180) {
          // Turned 180 degrees: the glyph's columns run top to bottom and
          // each row is written right to left.
          lv_color16_t *dst = buf + ((a.y1 + col) * canvas_width + a.x1);
          for (int i = font_height - 1; i >= 0; i--) {
              *dst++ = pattern[i];
              *dst++ = pattern[i];
          }
          continue;
        }

        // Destination: start of the rotated row (which came from original column)
        // These 2 x 12 pixels are contiguous in the buffer
        lv_color16_t *dst = buf + ((a.y2 - col) * canvas_width + a.x1);
        for(int i = 0; i < font_height; i++) {
            *dst++ = *pattern;
            *dst++ = *pattern++;
        }
    }
    return true;
  }

  inline void blit_glyph_to_canvas(uint8_t ch, int cell_x, int cell_y)
  {
    blit_glyph(canvas_buf, ch, cell_x, cell_y);
  }

public:
  TRSCanvas(lv_color16_t* canvas_buf, lv_coord_t canvas_width, lv_coord_t canvas_height, const unsigned char* font_data, uint8_t font_width, uint8_t font_height, lv_color_t fg, lv_color_t bg) {
    this->canvas_buf = canvas_buf;
    this->canvas_width = canvas_width;
    this->canvas_height = canvas_height;
    this->font_width = font_width;
    this->font_height = font_height;
    this->fg_color.red = fg.red >> 3;
    this->fg_color.green = fg.green >> 2;
    this->fg_color.blue = fg.blue >> 3;
    this->bg_color.red = bg.red >> 3;
    this->bg_color.green = bg.green >> 2;
    this->bg_color.blue = bg.blue >> 3;
    trs80_glyph_init(font_data);
  }
};

class TRSScreen {
private:
  ScreenBuffer* top;
  lv_color16_t *canvas_buf;
  lv_obj_t *canvas;
  lv_coord_t canvasWidth;
  lv_coord_t canvasHeight;
  uint8_t* prevScreenBuffer;
  TRSCanvas* trsCanvas;
  SemaphoreHandle_t mutex;
  // Direct-to-panel path (see render()): the RGB panel's own frame buffer,
  // null if it could not be obtained or doesn't match the canvas geometry.
  esp_lcd_panel_handle_t panel;
  lv_color16_t *panel_fb;
  bool overlayActive;
  // Set when the canvas was just (re)shown: LVGL is about to repaint all of
  // it, so the next render() pass leaves the panel to LVGL.
  bool lvglRepaintPending;

public:
  TRSScreen();
  void init();
  // Tell the screen that another LVGL object (the on-screen keyboard) is
  // drawn on top of the canvas. While one is, render() must go through LVGL
  // so the overlay gets composited; otherwise it writes the panel directly.
  // Call from the LVGL-owning task only.
  void setOverlayActive(bool active);
  // Show/hide the emulator's full-screen LVGL canvas. Used by the main-menu
  // flow: the menu (rotated LVGL UI) and the emulator never coexist, so
  // returning to the menu just hides the canvas instead of tearing it down.
  void setVisible(bool visible);
  void createCanvas();
  void push(ScreenBuffer* screenBuffer);
  void pop();
  ScreenBuffer* getTop() { return top; }
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
  // Push every character that changed since the last call to the display.
  // Returns the number of characters redrawn (0 = nothing changed). Call
  // from the LVGL-owning task only.
  int render();
};

extern TRSScreen trs_screen;

#endif
