// Minimal GT911 capacitive touch reader for the Waveshare
// ESP32-S3-Touch-LCD-2.8B.
//
// Hardware:
//   - I2C bus:   I2C_NUM_0, SDA=GPIO 15, SCL=GPIO 7 (shared with the
//                TCA9554 expander and other on-bus devices; already brought
//                up by I2C_Init()).
//   - TP_INT:    GPIO 16 (configured as floating input; we poll rather
//                than IRQ).
//   - TP_RST:    TCA9554 EXIO 2 -- pulsed low during Touch_Init().
//   - I2C addr:  0x5D (default; alternate 0x14 selectable via INT level
//                at reset, not used).
//   - Panel:     ST7701S 480 x 640 native portrait, held landscape.
//
// We intentionally do not use the espressif/esp_lcd_touch_gt911 managed
// component: in ESP-IDF v6 it requires the new i2c_master driver, but the
// project's I2C bus is still set up with the legacy i2c_driver_install()
// API (shared with TCA9554, SD card etc.). Talking to the GT911 directly
// over the legacy i2c_master_write_read_device() helpers avoids migrating
// the whole bus.
//
// Touch_Read() returns coordinates already rotated to match the LVGL
// landscape display (see LVGL_Driver.c, LV_DISPLAY_ROTATION_270).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the GT911. Caller must have already run I2C_Init() and
// EXIO_Init(). Returns ESP_OK or the first failing IDF error.
esp_err_t Touch_Init(void);

// Poll the GT911. On a present touch fills *x and *y with coordinates in
// the **panel-native portrait frame** (0..479 horizontal, 0..639 vertical)
// and returns true. Returns false when no point is reported.
//
// The values are designed to be fed directly into lv_indev_data_t.point:
// LVGL applies its display-rotation transform internally when reading the
// indev, so the caller does NOT need to rotate to the landscape frame.
// See the implementation for the math.
//
// Safe to call from any task; serialises on the I2C bus mutex inside the
// legacy driver.
bool Touch_Read(int *x, int *y);

#ifdef __cplusplus
}
#endif
