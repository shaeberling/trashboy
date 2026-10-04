#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "demos/lv_demos.h"

#include "ST7701S.h"


#define EXAMPLE_LVGL_TICK_PERIOD_MS    2

// The menu UI's rotation: LVGL renders it landscape and the flush callback
// rotates it into the portrait panel. CONFIG_TRASHBOY_DISPLAY_ROTATE_180
// turns it the other way up (the emulator screen follows in TRSCanvas).
#if CONFIG_TRASHBOY_DISPLAY_ROTATE_180
#define TRASHBOY_MENU_ROTATION         LV_DISPLAY_ROTATION_90
#else
#define TRASHBOY_MENU_ROTATION         LV_DISPLAY_ROTATION_270
#endif

extern lv_display_t *disp;
void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void example_increase_lvgl_tick(void *arg);

void LVGL_Init(void);