#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "demos/lv_demos.h"

#include "ST7701S.h"


#define EXAMPLE_LVGL_TICK_PERIOD_MS    2

extern lv_display_t *disp;
void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void example_increase_lvgl_tick(void *arg);

void LVGL_Init(void);