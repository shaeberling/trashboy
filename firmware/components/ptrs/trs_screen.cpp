

#include "trs_screen.h"
#include "settings.h"
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
  printf("ScreenBuffer created with mode %d: width=%d, height=%d, char_width=%d, char_height=%d\n", mode, width, height, char_width, char_height);
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
  if (buf != nullptr) {
    assert(width == buf->width && height == buf->height);
    memcpy(screenBuffer, buf->screenBuffer, screen_chars);
  }
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

static const lv_color_t screen_lv_colors[] = {
  LV_COLOR_MAKE(225, 225, 255), // White
  LV_COLOR_MAKE(51, 255, 51),   // Green
  LV_COLOR_MAKE(255, 177, 0)};  // Amber

TRSScreen::TRSScreen()
{
  top = nullptr;
  trsCanvas = nullptr;
  prevScreenBuffer = (uint8_t*) malloc(MAX_TRS_SCREEN_WIDTH * MAX_TRS_SCREEN_HEIGHT);
  memset(prevScreenBuffer, 0xFF, MAX_TRS_SCREEN_WIDTH * MAX_TRS_SCREEN_HEIGHT);
  mutex = xSemaphoreCreateRecursiveMutex();
}

void TRSScreen::init()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  canvasWidth = lv_display_get_horizontal_resolution(lv_display_get_default());
  canvasHeight = lv_display_get_vertical_resolution(lv_display_get_default());
  printf("Display resolution: %ldx%ld\n", canvasWidth, canvasHeight);

#if 1
  // RGB565 format: 2 bytes per pixel
  size_t buf_size = canvasWidth * canvasHeight * sizeof(lv_color16_t);
  canvas_buf = (lv_color16_t*) heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  assert(canvas_buf);

  canvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(canvas, canvas_buf, canvasWidth, canvasHeight, LV_COLOR_FORMAT_RGB565);
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  createCanvas();
#endif
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::createCanvas()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  if (trsCanvas != nullptr) {
    delete trsCanvas;
    trsCanvas = nullptr;
  }
  lv_color_t fg;
  // Initialize TRSCanvas with the current font and colors
  switch (settingsScreen.getScreenColor()) {
    case SCREEN_COLOR_GREEN:
      fg = screen_lv_colors[1];
      break;
    case SCREEN_COLOR_AMBER:
      fg = screen_lv_colors[2];
      break;
    default:
      fg = screen_lv_colors[0];
      break;
  }
  lv_color_t bg = lv_color_black();
  trsCanvas = new TRSCanvas(canvas_buf, canvasWidth, canvasHeight, font_m3, TRS_M3_CHAR_WIDTH, TRS_M3_CHAR_HEIGHT, fg, bg);
  refresh();
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::push(ScreenBuffer* screenBuffer)
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  screenBuffer->setCanvas(canvas);
  screenBuffer->setNext(top);
  screenBuffer->copyBufferFrom(top);
  top = screenBuffer;
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::pop()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  ScreenBuffer* tmp = top;
  top = top->getNext();
  delete tmp;
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::setMode(uint8_t mode)
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  top->setMode(mode);
  xSemaphoreGiveRecursive(mutex);
}

uint8_t TRSScreen::getMode()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  uint8_t mode = top->getMode();
  xSemaphoreGiveRecursive(mutex);
  return mode;
}

uint8_t TRSScreen::getWidth()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  uint8_t width = top->getWidth();
  xSemaphoreGiveRecursive(mutex);
  return width;
}

uint8_t TRSScreen::getHeight()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  uint8_t height = top->getHeight();
  xSemaphoreGiveRecursive(mutex);
  return height;
}

void TRSScreen::enableGrafyxMode(bool enable)
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  uint8_t mode = top->getMode();
  if (enable) {
    mode |= MODE_GRAFYX;
  } else {
    mode &= ~MODE_GRAFYX;
  }
  top->setMode(mode);
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::setExpanded(int flag)
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  assert(top != nullptr);
  top->setExpanded(flag);
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::setInverse(int flag)
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  assert(top != nullptr);
  top->setInverse(flag);
  xSemaphoreGiveRecursive(mutex);
}

bool TRSScreen::isTextMode()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  bool isText = (top->getMode() & MODE_GRAFYX) == 0;
  xSemaphoreGiveRecursive(mutex);
  return isText;
}

void TRSScreen::drawChar(uint16_t pos, uint8_t character)
{
  //xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  assert(top != nullptr);
  //if (isTextMode()) {
    top->drawChar(pos, character);
  //}
  //xSemaphoreGiveRecursive(mutex);
}

bool TRSScreen::getChar(uint16_t pos, uint8_t& character)
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  assert(top != nullptr);
  bool result = top->getChar(pos, character);
  xSemaphoreGiveRecursive(mutex);
  return result;
}

void TRSScreen::clear()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  assert(top != nullptr);
  top->clear();
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::refresh()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  memset(prevScreenBuffer, 0xFF, MAX_TRS_SCREEN_WIDTH * MAX_TRS_SCREEN_HEIGHT);
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::screenshot()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
#if 0
  if (top == nullptr || !isTextMode() /* XXX || trs_printer_read() == 0xff*/) {
    xSemaphoreGiveRecursive(mutex);
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
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::render()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  if (top == nullptr) {
    xSemaphoreGiveRecursive(mutex);
    return;
  }

  // Track one combined dirty rectangle (in pixel coords) to minimize invalidate calls
  bool any_dirty = false;

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

    trsCanvas->blit_glyph_to_canvas(newv, cx, cy);
    any_dirty = true;
  }

  if (any_dirty) {
    lv_obj_invalidate(canvas);
  }
  xSemaphoreGiveRecursive(mutex);
}

void TRSScreen::blit_glyph_to_canvas(uint8_t ch, int cell_x, int cell_y)
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  if (trsCanvas != nullptr) {
    trsCanvas->blit_glyph_to_canvas(ch, cell_x, cell_y);
  }
  xSemaphoreGiveRecursive(mutex);
}

TRSScreen trs_screen;



//----------------------------------------------------------------

static const char* KEY_COLOR  = "color";


void SettingsScreen::init() {
  setScreenColor(getScreenColor());
}

screen_color_t SettingsScreen::getScreenColor() {
  return (screen_color_t) nvs_get_u8(KEY_COLOR);
}

void SettingsScreen::setScreenColor(screen_color_t color) {
  nvs_set_u8(KEY_COLOR, color);
  trs_screen.createCanvas();
}

SettingsScreen settingsScreen;

