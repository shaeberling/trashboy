#include "splash.h"

#include "lvgl.h"
#include "esp_log.h"

extern const lv_image_dsc_t logo90c;  // 182w x 596h, pre-rotated for landscape view

static const char *TAG = "splash";

static lv_obj_t *splash_root = NULL;
static lv_obj_t *splash_status = NULL;
static const char * volatile pending_status = NULL;

#define NATIVE_W 480
#define NATIVE_H 640
#define USER_W   640
#define USER_H   480

#define STATUS_SCALE        512   // LVGL transform scale: 256 = 1x, so 512 = 2x
#define STATUS_USER_TOP     (30 + 182 + 60)  // logo top + logo user-height + 60 px gap

static void center_status_horizontally(void)
{
    if (splash_status == NULL) return;
    lv_obj_update_layout(splash_status);
    // Widget natural width before transform; the 2x scale doubles the
    // on-screen footprint, so account for that when centering.
    int32_t natural_w = lv_obj_get_width(splash_status);
    int32_t rendered_w = natural_w * STATUS_SCALE / 256;
    // Nudge ~one wide-char width to the left so it lines up with the logo.
    int user_left = (USER_W - rendered_w) / 2 - 24;
    if (user_left < 0) user_left = 0;
    lv_obj_set_pos(splash_status, STATUS_USER_TOP, (NATIVE_H - 1) - user_left);
}

void splash_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    splash_root = lv_obj_create(scr);
    lv_obj_remove_style_all(splash_root);
    lv_obj_set_size(splash_root, NATIVE_W, NATIVE_H);
    lv_obj_set_pos(splash_root, 0, 0);
    lv_obj_set_style_bg_color(splash_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(splash_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(splash_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_invalidate(scr);

    // Logo (pre-rotated 182w x 596h native) so its user-view appears as
    // 596w x 182h, centered with a 30 px margin from the user's top.
    const int logo_user_top    = 30;
    const int logo_user_left   = (USER_W - logo90c.header.h) / 2;
    const int logo_native_x    = logo_user_top;
    const int logo_native_y    = (NATIVE_H - 1) - (logo_user_left + (int)logo90c.header.h - 1);

    lv_obj_t *img = lv_image_create(splash_root);
    lv_image_set_src(img, &logo90c);
    lv_obj_set_pos(img, logo_native_x, logo_native_y);

    // Status label: rendered horizontally in native, then rotated 90° CCW
    // (LVGL CW-positive units => 2700) around the widget's top-left corner.
    // 2x transform scale to make the text twice the default font size.
    // After rotation the widget's position is the *left edge* of the
    // text in the user's landscape view; centering is recomputed from
    // the laid-out width in center_status_horizontally().
    //
    //   user (ux, uy)  ->  native (uy, NATIVE_H-1-ux)
    splash_status = lv_label_create(splash_root);
    lv_label_set_text(splash_status, "Scanning for Bluetooth keyboard...");
    lv_obj_set_style_text_color(splash_status, lv_color_white(), 0);
    lv_obj_set_style_transform_rotation(splash_status, 2700, 0);
    lv_obj_set_style_transform_scale(splash_status, STATUS_SCALE, 0);
    center_status_horizontally();

    ESP_LOGI(TAG, "splash created (native %dx%d, user %dx%d)",
             NATIVE_W, NATIVE_H, USER_W, USER_H);
}

void splash_set_status(const char *text)
{
    pending_status = text;
}

void splash_set_paired(void)
{
    splash_set_status("Paired! Press ENTER on your keyboard to continue.");
}

void splash_tick(void)
{
    const char *t = pending_status;
    if (t != NULL && splash_status != NULL) {
        pending_status = NULL;
        lv_label_set_text(splash_status, t);
        center_status_horizontally();
    }
}

void splash_dismiss(void)
{
    if (splash_root != NULL) {
        lv_obj_del(splash_root);
        splash_root = NULL;
        splash_status = NULL;
    }
}
