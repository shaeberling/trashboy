

#include "trs_screen.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRS_SCREEN_WIDTH 80
#define MAX_TRS_SCREEN_HEIGHT 24

#define TRS_M3_CHAR_WIDTH 8
#define TRS_M3_CHAR_HEIGHT 12

#define TRS_M4_CHAR_WIDTH 8
#define TRS_M4_CHAR_HEIGHT 10

#include "font/font_m3"
#include "font/font_m4"


//----------------------------------------------------------------


uint8_t ScreenBuffer::currentMonitorMode = 0;

ScreenBuffer::ScreenBuffer(uint8_t mode)
{
  this->screenBuffer = (uint8_t*) malloc(MAX_TRS_SCREEN_WIDTH * MAX_TRS_SCREEN_HEIGHT);
  assert(screenBuffer != NULL);
  this->width = 0;
  this->height = 0;
  this->char_width = 0;
  this->char_height = 0;
  this->font = nullptr;
  this->next = nullptr;
  this->isPaintingInverse = false;
  this->canvas = nullptr;
  setMode(mode);
  clear();
}

ScreenBuffer::~ScreenBuffer()
{
  free(screenBuffer);
}

void ScreenBuffer::setCanvas(lv_obj_t* canvas)
{
  this->canvas = canvas;
}

void ScreenBuffer::setMode(uint8_t mode)
{
  bool hires = false;
  bool changeResolution = false;
  uint8_t changes = mode ^ currentMonitorMode;

  if (mode & MODE_TEXT_64x16) {
    width = 64;
    height = 16;
    screen_chars = 64 * 16;
    char_width = TRS_M3_CHAR_WIDTH;
    char_height = TRS_M3_CHAR_HEIGHT;
    font = (uint8_t*) font_m3;
    if (changes & MODE_TEXT_64x16) {
      changeResolution = true;
    }
  }

  if (mode & MODE_TEXT_80x24) {
    width = 80;
    height = 24;
    screen_chars = 80 * 24;
    char_width = TRS_M4_CHAR_WIDTH;
    char_height = TRS_M4_CHAR_HEIGHT;
    font = (uint8_t*) font_m4;
    if (changes & MODE_TEXT_80x24) {
      changeResolution = true;
      hires = true;
    }
  }

  if (changes & MODE_GRAFYX) {
    if ((mode & MODE_GRAFYX) && (currentMonitorMode & MODE_TEXT_64x16)) {
      // Enable Grafyx but the current resolution is 64x16
      hires = true;
      changeResolution = true;
    }
    if (!(mode & MODE_GRAFYX) && (currentMonitorMode & MODE_TEXT_64x16)) {
      // Disable Grafyx and switch back to low res
      hires = false;
      changeResolution = true;
    }
  }

  if (changeResolution) {
#if 0
    const char* modline = hires ? VGA_640x240_60Hz : VGA_512x192_60Hz;

    DisplayController.setResolution(modline);
    Canvas.reset();
    Canvas.setBrushColor(Color::Black);
    Canvas.setGlyphOptions(GlyphOptions().FillBackground(true));
    Canvas.setPenColor(Color::White);
    Canvas.clear();
    clear();
    settingsScreen.init();
#endif
  }
  currentMonitorMode &= ~(MODE_TEXT_64x16 | MODE_TEXT_80x24);
  currentMonitorMode |= mode;
}

uint8_t ScreenBuffer::getMode()
{
  return currentMonitorMode;
}

uint8_t ScreenBuffer::getWidth()
{
  return width;
}

uint8_t ScreenBuffer::getHeight()
{
  return height;
}

uint8_t ScreenBuffer::getCharWidth()
{
  return char_width;
}

uint8_t ScreenBuffer::getCharHeight()
{
  return char_height;
}

uint8_t* ScreenBuffer::getBuffer()
{
  return screenBuffer;
}

void ScreenBuffer::setNext(ScreenBuffer* next)
{
  this->next = next;
}

ScreenBuffer* ScreenBuffer::getNext()
{
  return next;
}

void ScreenBuffer::copyBufferFrom(ScreenBuffer* buf)
{
  assert(buf != nullptr && width == buf->width && height == buf->height);
  memcpy(screenBuffer, buf->screenBuffer, screen_chars);
}

void ScreenBuffer::clear()
{
  memset(screenBuffer, ' ', MAX_TRS_SCREEN_WIDTH * MAX_TRS_SCREEN_HEIGHT);
}

void ScreenBuffer::refresh()
{
  for (int pos = 0; pos < screen_chars; pos++) {
    drawChar(pos, screenBuffer[pos]);
  }
}

void ScreenBuffer::update(uint8_t* from, uint8_t* to)
{
  int pos = from - screenBuffer;
  while (from != to) {
    drawChar(pos, *from);
    pos++;
    from++;
  }
}

void ScreenBuffer::setExpanded(int flag)
{
  int bit = flag ? MODE_EXPANDED : 0;
  if ((currentMonitorMode ^ bit) & MODE_EXPANDED) {
    currentMonitorMode ^= MODE_EXPANDED;
#if 0
//XXX
    Canvas.setGlyphOptions(GlyphOptions().DoubleWidth(flag ? 1 : 0)
                                          .FillBackground(true));
#endif
    refresh();
  }
}

void ScreenBuffer::setInverse(int flag)
{
  if (flag) {
    currentMonitorMode |= MODE_INVERSE;
  } else {
    currentMonitorMode &= ~MODE_INVERSE;
  }
}

int ScreenBuffer::isExpandedMode()
{
  return (currentMonitorMode & MODE_EXPANDED) != 0;
}

static void draw_glyph_to_canvas(lv_obj_t *target, lv_coord_t x, lv_coord_t y, uint8_t glyph,
                                 lv_color_t fg, lv_color_t bg)
{
    const unsigned char *glyph_data = &font_m3[glyph * TRS_M3_CHAR_HEIGHT];
    for (int row = 0; row < TRS_M3_CHAR_HEIGHT; row++) {
        uint8_t bits = glyph_data[row];
        for (int col = 0; col < TRS_M3_CHAR_WIDTH; col++) {
            bool on = (bits & (0x80 >> col)) != 0;
            lv_coord_t px = x + col;
            lv_coord_t py = y + row;
            // Rotate 90 degrees clockwise: (px, py) -> (py, canvas_height - 1 - px)
            lv_coord_t rotated_px = py;
            lv_coord_t rotated_py = 640 /*canvas_height*/ - 1 - px;
            lv_canvas_set_px(target, rotated_px, rotated_py, on ? fg : bg, LV_OPA_COVER);
        }
    }
}

void ScreenBuffer::drawChar(uint16_t pos, uint8_t character)
{
  if (pos >= screen_chars) {
    return;
  }
  
  if (isExpandedMode() && (pos & 1) != 0) {
    return;
  }
  screenBuffer[pos] = character;
  int pos_x = (pos % width) * char_width;
  int pos_y = (pos / width) * char_height;
#if 0
//XXX
  if ((currentMonitorMode & MODE_INVERSE) && (character & 0x80)) {
    if (!isPaintingInverse) {
      
      lv_canvas_set_brush_color(canvas, lv_color_white());
      lv_canvas_set_pen_color(canvas, lv_color_black());
      isPaintingInverse = true;
    }
    character &= 0x7f;
  } else if (isPaintingInverse) {
    lv_canvas_set_brush_color(canvas, lv_color_black());
    lv_canvas_set_pen_color(canvas, lv_color_white());
    isPaintingInverse = false;
  }
#endif
#if 0
  xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
  draw_glyph_to_canvas(canvas, pos_x, pos_y, character, lv_color_white(), lv_color_black());
  xSemaphoreGive(lvgl_mutex);
#endif
}

bool ScreenBuffer::getChar(uint16_t pos, uint8_t& character)
{
  if (pos < screen_chars) {
    character = screenBuffer[pos];
    return true;
  }
  return false;
}

//----------------------------------------------------------------

TRSScreen::TRSScreen()
{
  top = nullptr;
  prevScreenBuffer = (uint8_t*) malloc(MAX_TRS_SCREEN_WIDTH * MAX_TRS_SCREEN_HEIGHT);
  memset(prevScreenBuffer, 0xFF, MAX_TRS_SCREEN_WIDTH * MAX_TRS_SCREEN_HEIGHT);
}

void TRSScreen::init()
{
  canvasWidth = lv_display_get_horizontal_resolution(lv_display_get_default());
  canvasHeight = lv_display_get_vertical_resolution(lv_display_get_default());
  printf("Display resolution: %ldx%ld\n", canvasWidth, canvasHeight);

#if 1
  // RGB565 format: 2 bytes per pixel
  size_t buf_size = canvasWidth * canvasHeight * sizeof(lv_color_t);
  canvas_buf = (lv_color_t*) heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  assert(canvas_buf);

  canvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(canvas, canvas_buf, canvasWidth, canvasHeight, LV_COLOR_FORMAT_RGB565);
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  trsCanvas = new TRSCanvas(canvas_buf, canvasWidth, canvasHeight, font_m3, TRS_M3_CHAR_WIDTH, TRS_M3_CHAR_HEIGHT, lv_color_white(), lv_color_black());
#endif
}

void TRSScreen::push(ScreenBuffer* screenBuffer)
{
  screenBuffer->setCanvas(canvas);
  screenBuffer->setNext(top);
  top = screenBuffer;
}

void TRSScreen::pop()
{
  ScreenBuffer* tmp = top;
  top = top->getNext();
  delete tmp;
}

void TRSScreen::setMode(uint8_t mode)
{
  top->setMode(mode);
}

uint8_t TRSScreen::getMode()
{
  return top->getMode();
}

uint8_t TRSScreen::getWidth()
{
  return top->getWidth();
}

uint8_t TRSScreen::getHeight()
{
  return top->getHeight();
}

void TRSScreen::enableGrafyxMode(bool enable)
{
  uint8_t mode = top->getMode();
  if (enable) {
    mode |= MODE_GRAFYX;
  } else {
    mode &= ~MODE_GRAFYX;
  }
  top->setMode(mode);
}

void TRSScreen::setExpanded(int flag)
{
  assert(top != nullptr);
  top->setExpanded(flag);
}

void TRSScreen::setInverse(int flag)
{
  assert(top != nullptr);
  top->setInverse(flag);
}

bool TRSScreen::isTextMode()
{
  return (top->getMode() & MODE_GRAFYX) == 0;
}

void TRSScreen::drawChar(uint16_t pos, uint8_t character)
{
  assert(top != nullptr);
  //if (isTextMode()) {
    top->drawChar(pos, character);
  //}
}

bool TRSScreen::getChar(uint16_t pos, uint8_t& character)
{
  assert(top != nullptr);
  return top->getChar(pos, character);
}

void TRSScreen::clear()
{
  assert(top != nullptr);
  top->clear();
}

void TRSScreen::refresh()
{
  assert(top != nullptr);
  top->refresh();
}

void TRSScreen::screenshot()
{
#if 0
  if (top == nullptr || !isTextMode() /* XXX || trs_printer_read() == 0xff*/) {
    return;
  }
  uint16_t pos = 0;
  for (int y = 0; y < getHeight(); y++) {
    for (int x = 0; x < getWidth(); x++) {
      uint8_t ch;
      getChar(pos++, ch);
      trs_printer_write(ch);
    }
    trs_printer_write('\r');
  }
  trs_printer_write('\r');
#endif
}

void TRSScreen::render()
{
  if (top == nullptr) {
    return;
  }

  // Track one combined dirty rectangle (in pixel coords) to minimize invalidate calls
  bool any_dirty = false;
  lv_coord_t dirty_x0 = canvasWidth, dirty_y0 = canvasHeight, dirty_x1 = 0, dirty_y1 = 0;

  uint8_t width = top->getWidth();
  uint8_t height = top->getHeight();
  uint8_t char_width = top->getCharWidth();
  uint8_t char_height = top->getCharHeight();
  uint8_t* buffer = top->getBuffer();

  for (int i = 0; i < width * height; i++) {
    uint8_t newv = buffer[i];
    if (newv == prevScreenBuffer[i]) continue;

    prevScreenBuffer[i] = newv;

    int cx = i % width;
    int cy = i / width;

#if 0
   trsCanvas->blit_glyph_to_canvas(newv, cx, cy);
#else
    int pos_x = (i % width) * char_width;
  int pos_y = (i / width) * char_height;
  //printf("Drawing char '%c' at (%d, %d)\n", newv, pos_x, pos_y);
draw_glyph_to_canvas(canvas, pos_x, pos_y, newv, lv_color_white(), lv_color_black());
#endif

    // expand dirty bounds in pixels
    lv_coord_t px0 = cx * char_width;
    lv_coord_t py0 = cy * char_height;
    lv_coord_t px1 = px0 + char_width - 1;
    lv_coord_t py1 = py0 + char_height - 1;

    if (!any_dirty) {
        dirty_x0 = px0; dirty_y0 = py0;
        dirty_x1 = px1; dirty_y1 = py1;
        any_dirty = true;
    } else {
        if (px0 < dirty_x0) dirty_x0 = px0;
        if (py0 < dirty_y0) dirty_y0 = py0;
        if (px1 > dirty_x1) dirty_x1 = px1;
        if (py1 > dirty_y1) dirty_y1 = py1;
    }
  }

  if (any_dirty) {
      lv_area_t a = {
          .x1 = dirty_x0,
          .y1 = dirty_y0,
          .x2 = dirty_x1,
          .y2 = dirty_y1
      };

      // Invalidate only the changed region.
      // If your LVGL v8 build doesn't have lv_obj_invalidate_area,
      // fall back to lv_obj_invalidate(canvas).
      //printf("Invalidating area: (%d,%d) - (%d,%d)\n", a.x1, a.y1, a.x2, a.y2);
      //XXX need to rotate coordinates... lv_obj_invalidate_area(canvas, &a);
      
        // For image widget, signal that the source has changed
        //lv_img_set_src(canvas, &img_dsc);
        //  lv_img_cache_invalidate_src(&img_dsc);
        lv_obj_invalidate(canvas);
  }
}

TRSScreen trs_screen;



//----------------------------------------------------------------

#if 0
static const char* KEY_COLOR  = "color";


static const uint8_t wiper_settings[][3] = {
  {225, 225, 255},
  {51, 255, 51},
  {255, 177, 0}};

void SettingsScreen::init() {
  setScreenColor(getScreenColor());
}

screen_color_t SettingsScreen::getScreenColor() {
  return (screen_color_t) nvs_get_u8(KEY_COLOR);
}

void SettingsScreen::setScreenColor(screen_color_t color) {
  nvs_set_u8(KEY_COLOR, color);

#ifdef CONFIG_POCKET_TRS_TTGO_VGA32_SUPPORT
  switch(color) {
    case SCREEN_COLOR_WHITE:
      DisplayController.setPaletteItem(1, RGB888(0xe0, 0xe0, 0xff));
      break;
    case SCREEN_COLOR_GREEN:
      DisplayController.setPaletteItem(1, RGB888(0x33, 0xff, 0x33));
      break;
    case SCREEN_COLOR_AMBER:
      DisplayController.setPaletteItem(1, RGB888( 0xff, 0xb0, 0x00));
      break;
  }
#else
  for (int i = 0; i < 3; i++) {
    writeDigiPot(i, wiper_settings[color][i]);
  }
#endif
}

SettingsScreen settingsScreen;

#endif
