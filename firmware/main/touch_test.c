// CONFIG_TRASHBOY_TOUCH_TEST_MODE entry point. See touch_test.h.

#include "touch_test.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "Touch_Driver/Touch.h"

static const char *TAG = "touch_test";

// ---------- Fireworks tuning ------------------------------------------------
//
// Cost model on this hardware (ESP32-S3 driving a 480x640 RGB panel with
// LVGL PARTIAL render mode + SW rotation in the flush callback): each
// particle property change invalidates a small rect that the partial-render
// pipeline then has to re-rotate and DMA out. So we keep particle count
// low, use ONE animation timer per burst (not one per particle), and cap
// the number of bursts in flight.

// Empirically the original 8 / 3 split is the ceiling on this hardware --
// bumping either past it (tried 12 particles and 5 bursts) causes visible
// frame stutter on overlapping bursts. Do not raise without re-testing.
#define PARTICLES_PER_BURST   8
#define MAX_BURSTS_IN_FLIGHT  3
#define PARTICLE_SIZE_PX      6
#define BURST_DURATION_MS     700
#define PARTICLE_MIN_REACH_PX 40
#define PARTICLE_MAX_REACH_PX 80

typedef struct {
    lv_obj_t *dots[PARTICLES_PER_BURST];
    int16_t   origin_x;
    int16_t   origin_y;
    int16_t   dx[PARTICLES_PER_BURST];  // pre-baked total displacement
    int16_t   dy[PARTICLES_PER_BURST];
    bool      active;
} firework_burst_t;

static firework_burst_t s_bursts[MAX_BURSTS_IN_FLIGHT] = {0};
static bool s_touch_ok = false;

// Bright primary-ish palette; cycled through bursts so consecutive presses
// don't all look the same.
static const uint32_t s_palette[] = {
    0xFF4040,  // red
    0xFFA040,  // orange
    0xFFFF40,  // yellow
    0x40FF40,  // green
    0x40FFFF,  // cyan
    0x4080FF,  // blue
    0xC040FF,  // purple
    0xFFFFFF,  // white
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

// LVGL animation tick. v is the 0..1000 progress passed via set_values().
// Updates every particle in the burst from its origin toward origin + d*v.
static void burst_anim_exec_cb(void *var, int32_t v)
{
    firework_burst_t *b = (firework_burst_t *) var;
    if (!b->active) return;

    // Map 0..1000 -> 0..LV_OPA_COVER for the fade, then complement so the
    // particles fade out as they fly outward. Computed once per frame and
    // reused for all particles.
    const lv_opa_t opa = (lv_opa_t) (LV_OPA_COVER - (LV_OPA_COVER * v) / 1000);

    for (int i = 0; i < PARTICLES_PER_BURST; i++) {
        lv_obj_t *dot = b->dots[i];
        if (dot == NULL) continue;
        const int32_t x = b->origin_x + (b->dx[i] * v) / 1000;
        const int32_t y = b->origin_y + (b->dy[i] * v) / 1000;
        lv_obj_set_pos(dot, x - PARTICLE_SIZE_PX / 2, y - PARTICLE_SIZE_PX / 2);
        lv_obj_set_style_bg_opa(dot, opa, 0);
    }
}

// Cleans the burst up when its animation finishes.
static void burst_anim_completed_cb(lv_anim_t *a)
{
    firework_burst_t *b = (firework_burst_t *) a->var;
    for (int i = 0; i < PARTICLES_PER_BURST; i++) {
        if (b->dots[i] != NULL) {
            lv_obj_delete(b->dots[i]);
            b->dots[i] = NULL;
        }
    }
    b->active = false;
}

// Find a free burst slot, or NULL if all MAX_BURSTS_IN_FLIGHT are still
// running. The caller drops the press in that case (better than degrading
// frame rate by piling on more particles).
static firework_burst_t *acquire_burst_slot(void)
{
    for (int i = 0; i < MAX_BURSTS_IN_FLIGHT; i++) {
        if (!s_bursts[i].active) return &s_bursts[i];
    }
    return NULL;
}

static void spawn_burst(lv_obj_t *parent, int x, int y)
{
    firework_burst_t *b = acquire_burst_slot();
    if (b == NULL) {
        // Too many concurrent bursts -- silently drop this one. With
        // BURST_DURATION_MS=700 a finger would have to drum at >4 Hz to
        // notice.
        return;
    }

    b->active = true;
    b->origin_x = (int16_t) x;
    b->origin_y = (int16_t) y;
    s_palette_offset = (uint8_t)(s_palette_offset + 1);

    // Hand out the particle directions evenly around the unit circle with
    // a small angular jitter and per-particle distance jitter so the burst
    // looks organic rather than mechanical. cosf/sinf are cheap on the
    // S3 FPU and run only PARTICLES_PER_BURST times per press.
    const float two_pi = 6.28318530718f;
    for (int i = 0; i < PARTICLES_PER_BURST; i++) {
        const float base_angle = (two_pi * (float) i) / (float) PARTICLES_PER_BURST;
        const float jitter = (((int)(esp_random() & 0xFF) - 128) / 256.0f) * 0.4f;
        const float angle = base_angle + jitter;
        const int reach = PARTICLE_MIN_REACH_PX +
                          (int)(esp_random() % (PARTICLE_MAX_REACH_PX - PARTICLE_MIN_REACH_PX + 1));
        b->dx[i] = (int16_t)(cosf(angle) * reach);
        b->dy[i] = (int16_t)(sinf(angle) * reach);

        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, PARTICLE_SIZE_PX, PARTICLE_SIZE_PX);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        const uint32_t hex = s_palette[(s_palette_offset + i) % PALETTE_LEN];
        lv_obj_set_style_bg_color(dot, lv_color_hex(hex), 0);
        lv_obj_set_pos(dot, x - PARTICLE_SIZE_PX / 2, y - PARTICLE_SIZE_PX / 2);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        b->dots[i] = dot;
    }

    // One animation per burst, NOT per particle. The exec_cb walks all
    // particles each tick -- with PARTICLES_PER_BURST=8 and 3 concurrent
    // bursts, that's ~24 lv_obj_set_pos calls per LVGL tick worst-case.
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
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_obj_t *parent = (lv_obj_t *) lv_event_get_current_target(e);
    spawn_burst(parent, (int) p.x, (int) p.y);

    ESP_LOGI(TAG, "burst @ (%d, %d)", (int) p.x, (int) p.y);
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
    // Stop the screen from interpreting held presses as scroll drags
    // (otherwise the side scrollbar fades in on every long touch).
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Touch Test");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_add_event_cb(scr, on_screen_pressed, LV_EVENT_PRESSED, NULL);

    xTaskCreatePinnedToCore(touch_test_pump_task, "lvgl_pump_test",
                            8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Touch test running (particles/burst=%d, max bursts=%d, "
                  "size=%d px, duration=%d ms, touch=%s)",
             PARTICLES_PER_BURST, MAX_BURSTS_IN_FLIGHT,
             PARTICLE_SIZE_PX, BURST_DURATION_MS,
             s_touch_ok ? "ok" : "DISABLED");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
