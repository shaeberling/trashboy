// CONFIG_TRASHBOY_TOUCH_TEST_MODE entry point. See touch_test.h.

#include "touch_test.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "Touch_Driver/Touch.h"

static const char *TAG = "touch_test";

#define DOT_RADIUS_PX  15
#define MAX_DOTS       128   // recycled FIFO so we never grow without bound

static lv_obj_t *s_dots[MAX_DOTS] = {0};
static int s_dot_head = 0;
static int s_dot_count = 0;
static bool s_touch_ok = false;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void) indev;
    int x = 0, y = 0;
    if (s_touch_ok && Touch_Read(&x, &y)) {
        data->point.x = (int32_t) x;
        data->point.y = (int32_t) y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void on_screen_pressed(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_obj_t *parent = (lv_obj_t *) lv_event_get_current_target(e);

    // Recycle the oldest slot once we hit the cap.
    if (s_dot_count == MAX_DOTS) {
        lv_obj_t *old = s_dots[s_dot_head];
        if (old != NULL) lv_obj_delete(old);
        s_dots[s_dot_head] = NULL;
        s_dot_head = (s_dot_head + 1) % MAX_DOTS;
        s_dot_count--;
    }

    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, DOT_RADIUS_PX * 2, DOT_RADIUS_PX * 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF4040), 0);
    lv_obj_set_pos(dot, (int32_t)(p.x - DOT_RADIUS_PX),
                        (int32_t)(p.y - DOT_RADIUS_PX));
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);  // dots don't intercept

    int slot = (s_dot_head + s_dot_count) % MAX_DOTS;
    s_dots[slot] = dot;
    s_dot_count++;

    ESP_LOGI(TAG, "touch @ (%d, %d)", (int) p.x, (int) p.y);
}

// Dedicated LVGL pump: the existing main pump in main.cpp is wired into
// the splash teardown logic we are deliberately bypassing in test mode.
static void touch_test_pump_task(void *arg)
{
    (void) arg;
    while (true) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void touch_test_run(void)
{
    // 1) Bring up the GT911. On failure the screen still renders with
    //    the label so we can confirm the LCD half of the build works.
    if (Touch_Init() == ESP_OK) {
        s_touch_ok = true;
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
    } else {
        ESP_LOGE(TAG, "Touch_Init failed -- staying on test screen with "
                      "no touch input");
    }

    // 2) Build the screen.
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    // Stop the screen from interpreting held presses as scroll drags
    // (otherwise the side scrollbar fades in on every long touch).
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Touch Test");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_add_event_cb(scr, on_screen_pressed, LV_EVENT_PRESSED, NULL);

    // 3) Drive LVGL.
    xTaskCreatePinnedToCore(touch_test_pump_task, "lvgl_pump_test",
                            8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Touch test running (DOT_RADIUS_PX=%d, MAX_DOTS=%d, "
                  "touch=%s)",
             DOT_RADIUS_PX, MAX_DOTS, s_touch_ok ? "ok" : "DISABLED");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
