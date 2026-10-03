#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Seeed SenseCAP Watcher: ESP32-S3 (8 MB octal PSRAM, 32 MB flash), round
// 412x412 SPD2010 LCD on QSPI, and a wheel in the top-right corner — a rotary
// encoder with a push button. Only what TrashBoy uses is brought up: the
// power rails, the wheel and the display. Touch, audio, camera, SD and Wi-Fi
// stay off.

#define WATCHER_LCD_RES 412

// Power rails, I2C expander and the wheel. Call first: on battery the board
// only stays on once the firmware holds the system rail.
esp_err_t watcher_board_init(void);

// Wheel movement since the last call, in quadrature counts (two per step of
// Seeed's own firmware). Positive is what that firmware calls "right".
int watcher_knob_read(void);

// The wheel's push button, not debounced.
bool watcher_button_pressed(void);

// Display. Call watcher_lcd_init() from the task that will do all the drawing,
// pinned to a core: the panel's SPI interrupt is installed on the calling
// core, and the SPI driver can strand a sender that waits on another one.
// The panel comes up with the backlight off and undefined contents.
esp_err_t watcher_lcd_init(void);
void watcher_lcd_backlight(int percent);

// Fill the whole panel with black.
esp_err_t watcher_lcd_clear(void);

// Send a w x h block of RGB565 pixels (high byte first) and wait until it is
// on the wire, so the caller may change the buffer again. The SPD2010 needs
// x and w to be multiples of 4. `pixels` must be DMA-capable internal RAM.
esp_err_t watcher_lcd_draw(int x, int y, int w, int h, const void *pixels);

#ifdef __cplusplus
}
#endif
