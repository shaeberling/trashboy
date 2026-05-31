// CONFIG_TRASHBOY_TOUCH_TEST_MODE entry point. See touch_test.h.

#include "touch_test.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "LCD_Driver/ST7701S.h"
#include "Buzzer/Buzzer.h"
#include "Touch_Driver/Touch.h"

static const char *TAG = "touch_test";

// ---------- Fireworks rendering --------------------------------------------
//
// One screen-sized ARGB8888 lv_canvas covers the whole UI. All bursts
// rasterise their particles into the same canvas via the layer API. We
// avoid lv_canvas_fill_bg / lv_obj_invalidate (which dirty the WHOLE
// canvas widget -- 640x480 through the sw-rotate flush path every frame,
// far too expensive on this hardware). Instead, each frame:
//
//   1) erase each particle's last-frame position with opaque black (the
//      screen bg is black, so an opaque black canvas pixel is visually
//      indistinguishable from "transparent"),
//   2) draw the new colored particle,
//   3) call lv_obj_invalidate_area on ONE bbox enclosing the 8 erase +
//      8 draw rects per burst.
//
// Net cost per burst per frame on this hardware: one small dirty rect
// (typically a few hundred px square) through one lv_draw_sw_rotate pass.
// That made 3 simultaneous bursts render smoothly where the previous
// per-particle-lv_obj approach stuttered at 3.

#define PARTICLES_PER_BURST   8
#define MAX_BURSTS_IN_FLIGHT  3
#define PARTICLE_SIZE_PX      6
#define BURST_DURATION_MS     700
#define PARTICLE_MIN_REACH_PX 40
#define PARTICLE_MAX_REACH_PX 80

// LVGL sees the panel as landscape after the rotation set in LVGL_Init().
#define CANVAS_W EXAMPLE_LCD_V_RES   // 640
#define CANVAS_H EXAMPLE_LCD_H_RES   // 480

typedef struct {
    int16_t  origin_x;
    int16_t  origin_y;
    int16_t  dx[PARTICLES_PER_BURST];
    int16_t  dy[PARTICLES_PER_BURST];
    int16_t  prev_x[PARTICLES_PER_BURST];   // last-frame screen position
    int16_t  prev_y[PARTICLES_PER_BURST];   // (used to erase before redraw)
    uint32_t color[PARTICLES_PER_BURST];
    bool     active;
} firework_burst_t;

static firework_burst_t s_bursts[MAX_BURSTS_IN_FLIGHT] = {0};
static bool s_touch_ok = false;

static lv_obj_t *s_canvas = NULL;
static void     *s_canvas_buf = NULL;

// ---------- Buzzer feedback -------------------------------------------------

#define BUZZ_DURATION_MS 50

static esp_timer_handle_t s_buzz_off_timer = NULL;

static void buzz_off_cb(void *arg)
{
    (void) arg;
    Buzzer_Off();
}

static void buzz_init(void)
{
    const esp_timer_create_args_t args = {
        .callback = buzz_off_cb,
        .name = "buzz_off",
    };
    esp_timer_create(&args, &s_buzz_off_timer);
}

static void buzz_pulse(void)
{
    if (s_buzz_off_timer == NULL) return;
    esp_timer_stop(s_buzz_off_timer);
    Buzzer_On();
    esp_timer_start_once(s_buzz_off_timer, BUZZ_DURATION_MS * 1000ULL);
}

static const uint32_t s_palette[] = {
    0xFF4040, 0xFFA040, 0xFFFF40, 0x40FF40,
    0x40FFFF, 0x4080FF, 0xC040FF, 0xFFFFFF,
};
#define PALETTE_LEN (sizeof(s_palette) / sizeof(s_palette[0]))
static uint8_t s_palette_offset = 0;

// ---------- Touch indev -----------------------------------------------------

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

// ---------- Firework burst --------------------------------------------------

// 1 px AA padding around each drawn particle.
#define ERASE_PAD 1
#define ERASE_RECT_SIZE (PARTICLE_SIZE_PX + 2 * ERASE_PAD)

// Animation tick. v is 0..1000 burst progress. Two-pass per frame:
//   1) erase each particle's last-frame position with opaque black (works
//      because the screen bg is black; the canvas pixel becomes opaque
//      black and renders as black on the panel),
//   2) draw the new colored particle at the current position.
// Then invalidate ONE bbox covering both old and new positions so LVGL
// re-pushes just that area through the sw-rotate flush path.
static void burst_anim_exec_cb(void *var, int32_t v)
{
    firework_burst_t *b = (firework_burst_t *) var;
    if (!b->active || s_canvas == NULL) return;

    const lv_opa_t opa = (lv_opa_t) (LV_OPA_COVER - (LV_OPA_COVER * v) / 1000);

    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);

    lv_draw_rect_dsc_t erase_dsc;
    lv_draw_rect_dsc_init(&erase_dsc);
    erase_dsc.bg_color = lv_color_black();
    erase_dsc.bg_opa = LV_OPA_COVER;
    erase_dsc.radius = LV_RADIUS_CIRCLE;
    erase_dsc.border_width = 0;

    lv_draw_rect_dsc_t draw_dsc;
    lv_draw_rect_dsc_init(&draw_dsc);
    draw_dsc.bg_opa = opa;
    draw_dsc.radius = LV_RADIUS_CIRCLE;
    draw_dsc.border_width = 0;

    int32_t min_x = INT32_MAX, min_y = INT32_MAX;
    int32_t max_x = INT32_MIN, max_y = INT32_MIN;

    for (int i = 0; i < PARTICLES_PER_BURST; i++) {
        // Erase the previous frame's particle (with AA padding).
        lv_area_t erase = {
            .x1 = b->prev_x[i] - PARTICLE_SIZE_PX / 2 - ERASE_PAD,
            .y1 = b->prev_y[i] - PARTICLE_SIZE_PX / 2 - ERASE_PAD,
        };
        erase.x2 = erase.x1 + ERASE_RECT_SIZE - 1;
        erase.y2 = erase.y1 + ERASE_RECT_SIZE - 1;
        lv_draw_rect(&layer, &erase_dsc, &erase);

        // Draw the new particle.
        const int32_t px = b->origin_x + (b->dx[i] * v) / 1000;
        const int32_t py = b->origin_y + (b->dy[i] * v) / 1000;
        draw_dsc.bg_color = lv_color_hex(b->color[i]);

        lv_area_t coords;
        coords.x1 = px - PARTICLE_SIZE_PX / 2;
        coords.y1 = py - PARTICLE_SIZE_PX / 2;
        coords.x2 = coords.x1 + PARTICLE_SIZE_PX - 1;
        coords.y2 = coords.y1 + PARTICLE_SIZE_PX - 1;
        lv_draw_rect(&layer, &draw_dsc, &coords);

        // Union of erase + draw rects feeds the per-burst dirty box.
        if (erase.x1 < min_x) min_x = erase.x1;
        if (erase.y1 < min_y) min_y = erase.y1;
        if (erase.x2 > max_x) max_x = erase.x2;
        if (erase.y2 > max_y) max_y = erase.y2;
        if (coords.x1 < min_x) min_x = coords.x1;
        if (coords.y1 < min_y) min_y = coords.y1;
        if (coords.x2 > max_x) max_x = coords.x2;
        if (coords.y2 > max_y) max_y = coords.y2;

        b->prev_x[i] = (int16_t) px;
        b->prev_y[i] = (int16_t) py;
    }

    lv_canvas_finish_layer(s_canvas, &layer);

    // Canvas is at (0,0) screen-sized: canvas-local coords == screen coords.
    lv_area_t dirty = {
        .x1 = min_x - 1,
        .y1 = min_y - 1,
        .x2 = max_x + 1,
        .y2 = max_y + 1,
    };
    lv_obj_invalidate_area(s_canvas, &dirty);
}

static void burst_anim_completed_cb(lv_anim_t *a)
{
    firework_burst_t *b = (firework_burst_t *) a->var;

    // Erase the final frame's particles so nothing lingers between bursts.
    if (s_canvas != NULL) {
        lv_layer_t layer;
        lv_canvas_init_layer(s_canvas, &layer);

        lv_draw_rect_dsc_t erase_dsc;
        lv_draw_rect_dsc_init(&erase_dsc);
        erase_dsc.bg_color = lv_color_black();
        erase_dsc.bg_opa = LV_OPA_COVER;
        erase_dsc.radius = LV_RADIUS_CIRCLE;
        erase_dsc.border_width = 0;

        int32_t min_x = INT32_MAX, min_y = INT32_MAX;
        int32_t max_x = INT32_MIN, max_y = INT32_MIN;

        for (int i = 0; i < PARTICLES_PER_BURST; i++) {
            lv_area_t erase = {
                .x1 = b->prev_x[i] - PARTICLE_SIZE_PX / 2 - ERASE_PAD,
                .y1 = b->prev_y[i] - PARTICLE_SIZE_PX / 2 - ERASE_PAD,
            };
            erase.x2 = erase.x1 + ERASE_RECT_SIZE - 1;
            erase.y2 = erase.y1 + ERASE_RECT_SIZE - 1;
            lv_draw_rect(&layer, &erase_dsc, &erase);

            if (erase.x1 < min_x) min_x = erase.x1;
            if (erase.y1 < min_y) min_y = erase.y1;
            if (erase.x2 > max_x) max_x = erase.x2;
            if (erase.y2 > max_y) max_y = erase.y2;
        }

        lv_canvas_finish_layer(s_canvas, &layer);

        lv_area_t dirty = {
            .x1 = min_x - 1,
            .y1 = min_y - 1,
            .x2 = max_x + 1,
            .y2 = max_y + 1,
        };
        lv_obj_invalidate_area(s_canvas, &dirty);
    }

    b->active = false;
}

static firework_burst_t *acquire_burst_slot(void)
{
    for (int i = 0; i < MAX_BURSTS_IN_FLIGHT; i++) {
        if (!s_bursts[i].active) return &s_bursts[i];
    }
    return NULL;
}

static void canvas_init(lv_obj_t *parent)
{
    const size_t buf_sz = (size_t) CANVAS_W * CANVAS_H * 4;
    s_canvas_buf = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    if (s_canvas_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u-byte canvas buffer",
                 (unsigned) buf_sz);
        return;
    }
    // Fully transparent ARGB8888 = all bytes zero.
    memset(s_canvas_buf, 0, buf_sz);

    s_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf,
                         CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_remove_style_all(s_canvas);
    lv_obj_set_size(s_canvas, CANVAS_W, CANVAS_H);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
}

static void spawn_burst(int x, int y)
{
    firework_burst_t *b = acquire_burst_slot();
    if (b == NULL || s_canvas == NULL) return;

    b->active = true;
    b->origin_x = (int16_t) x;
    b->origin_y = (int16_t) y;
    s_palette_offset = (uint8_t)(s_palette_offset + 1);

    const float two_pi = 6.28318530718f;
    for (int i = 0; i < PARTICLES_PER_BURST; i++) {
        const float base_angle = (two_pi * (float) i) / (float) PARTICLES_PER_BURST;
        const float jitter = (((int)(esp_random() & 0xFF) - 128) / 256.0f) * 0.4f;
        const float angle = base_angle + jitter;
        const int reach = PARTICLE_MIN_REACH_PX +
                          (int)(esp_random() % (PARTICLE_MAX_REACH_PX - PARTICLE_MIN_REACH_PX + 1));
        b->dx[i] = (int16_t)(cosf(angle) * reach);
        b->dy[i] = (int16_t)(sinf(angle) * reach);
        b->color[i] = s_palette[(s_palette_offset + i) % PALETTE_LEN];
        // First-frame erase target == origin; nothing's there yet so it's
        // a benign overwrite of already-black pixels.
        b->prev_x[i] = b->origin_x;
        b->prev_y[i] = b->origin_y;
    }

    burst_anim_exec_cb(b, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, BURST_DURATION_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, burst_anim_exec_cb);
    lv_anim_set_completed_cb(&a, burst_anim_completed_cb);
    lv_anim_start(&a);
}

// ---------- Screen ----------------------------------------------------------

static void on_screen_pressed(lv_event_t *e)
{
    (void) e;
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    spawn_burst((int) p.x, (int) p.y);
    buzz_pulse();

    ESP_LOGI(TAG, "burst @ (%d, %d)", (int) p.x, (int) p.y);
}

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
    buzz_init();

    if (Touch_Init() == ESP_OK) {
        s_touch_ok = true;
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
    } else {
        ESP_LOGE(TAG, "Touch_Init failed -- staying on test screen with "
                      "no touch input");
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    canvas_init(scr);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Touch Test");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_add_event_cb(scr, on_screen_pressed, LV_EVENT_PRESSED, NULL);

    xTaskCreatePinnedToCore(touch_test_pump_task, "lvgl_pump_test",
                            8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Touch test running (canvas=%dx%d ARGB8888, "
                  "particles/burst=%d, max bursts=%d, touch=%s)",
             CANVAS_W, CANVAS_H,
             PARTICLES_PER_BURST, MAX_BURSTS_IN_FLIGHT,
             s_touch_ok ? "ok" : "DISABLED");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
