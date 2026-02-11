#include "LVGL_Driver.h"
#include <esp_heap_caps.h>

static const char *LVGL_TAG = "LVGL";
lv_display_t *disp = NULL;

esp_timer_handle_t lvgl_tick_timer = NULL;

static void *buf1 = NULL;
static void *buf2 = NULL;

void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
    xSemaphoreGive(sem_gui_ready);
    xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
#endif
    // pass the draw buffer to the driver
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    lv_display_flush_ready(disp);
}

void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void LVGL_Init(void)
{
    ESP_LOGI(LVGL_TAG, "Initialize LVGL library");
    lv_init();

    // Create a display
    ESP_LOGI(LVGL_TAG, "Create display");
    disp = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    if (disp == NULL) {
        ESP_LOGE(LVGL_TAG, "Failed to create display");
        return;
    }

    // Allocate buffers
    ESP_LOGI(LVGL_TAG, "Allocate buffers from PSRAM");
    // RGB565 uses 2 bytes per pixel
    uint32_t buf_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2;
    buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (buf1 == NULL) {
        ESP_LOGE(LVGL_TAG, "Failed to allocate buffer 1");
        return;
    }
#if CONFIG_EXAMPLE_DOUBLE_FB
    buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (buf2 == NULL) {
        ESP_LOGE(LVGL_TAG, "Failed to allocate buffer 2");
        return;
    }
#else
    buf2 = NULL;
#endif

    // Set buffers for the display
    ESP_LOGI(LVGL_TAG, "Set display buffers");
#if CONFIG_EXAMPLE_DOUBLE_FB
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);
#else
    lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    // Set color format (RGB565)
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    // Set the flush callback
    ESP_LOGI(LVGL_TAG, "Register display driver");
    lv_display_set_flush_cb(disp, example_lvgl_flush_cb);

    // Store panel handle in display user data
    lv_display_set_user_data(disp, (void *)panel_handle);

    // Set display as default
    lv_display_set_default(disp);

    ESP_LOGI(LVGL_TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };

    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));
}