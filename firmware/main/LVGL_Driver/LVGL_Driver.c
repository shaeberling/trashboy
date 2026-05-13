// LVGL display driver: PARTIAL render mode with SOFTWARE rotation.
//
// The panel is a portrait 480 x 640 RGB display, but the device is held in
// landscape so the user's view is 640 x 480 with the panel's native top
// edge on their right. We tell LVGL the display is landscape via
//   lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270)
// — which only swaps the reported width/height, it does *not* rotate
// pixels — and then perform the actual pixel rotation in the flush
// callback before writing to the panel.
//
// Mapping (matches what components/ptrs/trs_screen.h does manually):
//   panel_x = user_y
//   panel_y = NATIVE_H - 1 - user_x
//
// This pattern is the one Espressif's esp_lvgl_port uses when its
// .sw_rotate flag is set, and the one documented in LVGL v9's rotation
// chapter (https://docs.lvgl.io/9.4/details/main-modules/display/rotation).

#include "LVGL_Driver.h"
#include <esp_heap_caps.h>

static const char *LVGL_TAG = "LVGL";
lv_display_t *disp = NULL;

esp_timer_handle_t lvgl_tick_timer = NULL;

static void *buf1 = NULL;
static void *buf2 = NULL;
static void *rot_buf = NULL;

void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) lv_display_get_user_data(disp);
    lv_color_format_t cf = lv_display_get_color_format(disp);

    const int32_t src_w = lv_area_get_width(area);
    const int32_t src_h = lv_area_get_height(area);

    // Translate the user-view (landscape) area into panel-native coords.
    // Width and height swap because of the 90° rotation.
    lv_area_t rot_area;
    rot_area.x1 = area->y1;
    rot_area.y1 = (EXAMPLE_LCD_V_RES - 1) - area->x2;
    rot_area.x2 = area->y2;
    rot_area.y2 = (EXAMPLE_LCD_V_RES - 1) - area->x1;

    // Rotate pixels from user-view into rot_buf in panel orientation.
    const uint32_t src_stride  = lv_draw_buf_width_to_stride((uint32_t) src_w, cf);
    const uint32_t dest_stride = lv_draw_buf_width_to_stride((uint32_t) src_h, cf);
    lv_draw_sw_rotate(px_map, rot_buf,
                      src_w, src_h,
                      (int32_t) src_stride, (int32_t) dest_stride,
                      LV_DISPLAY_ROTATION_270, cf);

#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
    xSemaphoreGive(sem_gui_ready);
    xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
#endif
    esp_lcd_panel_draw_bitmap(panel_handle,
                              rot_area.x1, rot_area.y1,
                              rot_area.x2 + 1, rot_area.y2 + 1,
                              rot_buf);
    lv_display_flush_ready(disp);
}

void example_increase_lvgl_tick(void *arg)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void LVGL_Init(void)
{
    ESP_LOGI(LVGL_TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(LVGL_TAG, "Create display");
    // Display is created with the panel-native (portrait) size; rotation
    // is applied after, which makes LVGL report landscape resolution to
    // widgets.
    disp = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    if (disp == NULL) {
        ESP_LOGE(LVGL_TAG, "Failed to create display");
        return;
    }

    ESP_LOGI(LVGL_TAG, "Allocate buffers from PSRAM");
    // RGB565: 2 bytes per pixel. Worst-case dirty area is the full screen.
    const uint32_t buf_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2;
    buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (buf1 == NULL) {
        ESP_LOGE(LVGL_TAG, "Failed to allocate buffer 1");
        return;
    }
    buf2 = NULL;

    rot_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (rot_buf == NULL) {
        ESP_LOGE(LVGL_TAG, "Failed to allocate rotation buffer");
        return;
    }

    ESP_LOGI(LVGL_TAG, "Set display buffers (PARTIAL, sw_rotate)");
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    ESP_LOGI(LVGL_TAG, "Register display driver");
    lv_display_set_flush_cb(disp, example_lvgl_flush_cb);
    lv_display_set_user_data(disp, (void *)panel_handle);
    lv_display_set_default(disp);

    // Tell LVGL the display is landscape. Pixel rotation happens in the
    // flush callback (lv_draw_sw_rotate), since the RGB panel has no
    // hardware rotation.
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);

    ESP_LOGI(LVGL_TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));
}
