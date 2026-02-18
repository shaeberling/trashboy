
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
  inline void blit_glyph_to_canvas(uint8_t ch, int cell_x, int cell_y)
  {
    // After 90° CCW rotation:
    // Original glyph: 8 wide × 12 tall
    // Rendered as: 12 wide × 8 tall
    // cell_x/cell_y refer to glyph cell positions in the rotated space

    // Top-left pixel in screen space (in rotated coordinates)
    int px0 = cell_x * font_width;  // rotated width = font_width (8)
    int py0 = cell_y * font_height;   // rotated height = font_height (12)
    
    const int offset_x = (canvas_width - (2 * 12 * 16)) / 2;
    const int offset_y = (canvas_height - (8 * 64)) / 2;
  
    lv_coord_t rotated_px = py0;
    lv_coord_t rotated_py = canvas_height - 1 - px0;
    px0 = rotated_px * 2 + offset_x;
    py0 = rotated_py - offset_y;

    // For each column of the original glyph (becomes row in rotated space)
    // Original column 0 (leftmost) → rotated row 7 (bottom)
    // Original column 7 (rightmost) → rotated row 0 (top)
    for (int col = 0; col < font_width; col++) {
        uint16_t pat = glyph_bits(ch, col);
        
        // Write this column vertically in the rotated output
        // Rotated row = (font_width - 1 - col)
        const int rotated_row = font_width - 1 - col;
        
        // Destination: start of the rotated row (which came from original column)
        // These 12 pixels are contiguous in the canvas buffer
        lv_color16_t *dst = canvas_buf + ((py0 - col /*+ rotated_row*/) * canvas_width + px0);
        lv_color16_t *pattern = col_lut[pat];
        for(int i = 0; i < font_height; i++) {
            *dst++ = *pattern;
            *dst++ = *pattern++;
        }

        // Copy 12 contiguous pixels (the rotated row)
        // col_lut[pat] is 12 lv_color16_t values
        //ZZZZZ memcpy(dst, col_lut[pat], font_height * sizeof(lv_color16_t));
    }
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
