// Splash UI — LANDSCAPE coordinates, courtesy of LVGL display rotation.
//
// LVGL_Driver installs LV_DISPLAY_ROTATION_270 and performs the pixel
// rotation in software inside the flush callback. From this file's
// perspective the display is plain 640 × 480 landscape: lv_obj_set_pos
// takes user-view coordinates, sizes are user-view sizes, and we use
// the original landscape-oriented logo (logo.c, 596 × 182). No widget
// transform_rotation / transform_scale tricks anywhere — those caused
// LVGL invalidation bugs in earlier iterations.

#include "splash.h"

#include <string.h>

#include "lvgl.h"
#include "esp_log.h"

extern const lv_image_dsc_t logoc;  // 596 x 182, landscape orientation, color

static const char *TAG = "splash";

#define USER_W   640
#define USER_H   480

// Logo placement (landscape coordinates).
#define LOGO_BIG_SCALE        256   // 1.0x → 596 × 182
#define LOGO_COMPACT_SCALE    128   // 0.5x → 298 ×  91
#define LOGO_BIG_USER_TOP      30
#define LOGO_COMPACT_USER_TOP   5

// Text layout (landscape).
#define TEXT_USER_LEFT         20
#define TEXT_WIDTH             (USER_W - 2 * TEXT_USER_LEFT)
// Status @ MS24, list/description @ MS20 (between the too-small MS14 and
// the too-big MS28). Subtext stays compact at MS14 since it's a hint.
#define LIST_ROW_HEIGHT        28
#define LIST_MAX_ITEMS         10
#define LIST_ITEM_TEXT_MAX     128

#define STATUS_FONT            (&lv_font_montserrat_24)
#define LIST_FONT              (&lv_font_montserrat_20)
#define SUBTEXT_FONT           (&lv_font_montserrat_14)

static int g_status_user_top  = 0;
static int g_list_user_top    = 0;
static int g_subtext_user_top = 0;

static lv_obj_t *splash_root    = NULL;
static lv_obj_t *splash_logo    = NULL;
static lv_obj_t *splash_status  = NULL;
static lv_obj_t *splash_subtext = NULL;

static const char * volatile pending_status   = NULL;
static const char * volatile pending_subtext  = NULL;
static volatile bool pending_compact = false;

typedef enum { LIST_OP_NONE, LIST_OP_SHOW, LIST_OP_SEL, LIST_OP_HIDE } list_op_t;
static volatile list_op_t pending_list_op = LIST_OP_NONE;
static int pending_list_count    = 0;
static int pending_list_selected = 0;
static char pending_list_items[LIST_MAX_ITEMS][LIST_ITEM_TEXT_MAX];

static lv_obj_t *list_labels[LIST_MAX_ITEMS] = {0};
static int       list_count    = 0;
static int       list_selected = 0;

// -----------------------------------------------------------------------------

static void apply_layout(bool compact)
{
    const int scale = compact ? LOGO_COMPACT_SCALE : LOGO_BIG_SCALE;
    const int user_top = compact ? LOGO_COMPACT_USER_TOP : LOGO_BIG_USER_TOP;
    const int logo_w = (int)logoc.header.w * scale / 256;
    const int logo_h = (int)logoc.header.h * scale / 256;
    const int logo_x = (USER_W - logo_w) / 2;

    if (splash_logo) {
        lv_image_set_scale(splash_logo, scale);
        lv_obj_set_pos(splash_logo, logo_x, user_top);
    }

    // Vertical gaps tuned for the smaller fonts (status @ MS24 ≈ 24 px,
    // list/subtext @ MS14 ≈ 14 px).
    g_status_user_top  = user_top + logo_h + (compact ? 14 : 36);
    g_list_user_top    = g_status_user_top + 32;     // clear of MS24 status
    g_subtext_user_top = USER_H - 22;                 // clear of MS14 subtext

    if (splash_status) {
        lv_obj_set_pos(splash_status, TEXT_USER_LEFT, g_status_user_top);
    }
    if (splash_subtext) {
        lv_obj_set_pos(splash_subtext, TEXT_USER_LEFT, g_subtext_user_top);
    }
    for (int i = 0; i < list_count; i++) {
        if (!list_labels[i]) continue;
        lv_obj_set_pos(list_labels[i],
                       TEXT_USER_LEFT,
                       g_list_user_top + i * LIST_ROW_HEIGHT);
    }
}

// -----------------------------------------------------------------------------

void splash_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    splash_root = lv_obj_create(scr);
    lv_obj_remove_style_all(splash_root);
    lv_obj_set_size(splash_root, USER_W, USER_H);
    lv_obj_set_pos(splash_root, 0, 0);
    lv_obj_set_style_bg_color(splash_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(splash_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(splash_root, LV_OBJ_FLAG_SCROLLABLE);

    splash_logo = lv_image_create(splash_root);
    lv_image_set_src(splash_logo, &logoc);
    lv_image_set_pivot(splash_logo, 0, 0);
    lv_obj_set_style_bg_opa(splash_logo, LV_OPA_TRANSP, 0);

    splash_status = lv_label_create(splash_root);
    lv_label_set_text(splash_status, "Scanning for Bluetooth keyboard...");
    lv_obj_set_style_text_color(splash_status, lv_color_white(), 0);
    lv_obj_set_style_text_font(splash_status, STATUS_FONT, 0);
    lv_obj_set_style_bg_opa(splash_status, LV_OPA_TRANSP, 0);
    lv_obj_set_width(splash_status, TEXT_WIDTH);
    lv_label_set_long_mode(splash_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(splash_status, LV_TEXT_ALIGN_CENTER, 0);

    apply_layout(false /* start big */);

    ESP_LOGI(TAG, "splash created (landscape %dx%d via lv_display rotation)",
             USER_W, USER_H);
}

void splash_set_status(const char *text)
{
    ESP_LOGI(TAG, "queued status: '%s'", text ? text : "(null)");
    pending_status = text;
}

void splash_set_paired(void)
{
    splash_set_status("Paired! Press ENTER to continue.");
}

void splash_set_compact(void)
{
    pending_compact = true;
}

void splash_set_subtext(const char *text)
{
    pending_subtext = text ? text : "";
}

void splash_show_list(const char * const *items, int count, int selected)
{
    if (count > LIST_MAX_ITEMS) count = LIST_MAX_ITEMS;
    pending_list_count = count;
    pending_list_selected = selected;
    for (int i = 0; i < count; i++) {
        const char *src = items[i] ? items[i] : "";
        strncpy(pending_list_items[i], src, LIST_ITEM_TEXT_MAX - 1);
        pending_list_items[i][LIST_ITEM_TEXT_MAX - 1] = '\0';
    }
    pending_list_op = LIST_OP_SHOW;
}

void splash_set_list_selection(int selected)
{
    pending_list_selected = selected;
    if (pending_list_op != LIST_OP_SHOW) {
        pending_list_op = LIST_OP_SEL;
    }
}

void splash_hide_list(void)
{
    pending_list_op = LIST_OP_HIDE;
}

// -----------------------------------------------------------------------------

static void apply_list_show(void)
{
    for (int i = 0; i < list_count; i++) {
        if (list_labels[i]) { lv_obj_del(list_labels[i]); list_labels[i] = NULL; }
    }
    list_count = pending_list_count;
    list_selected = pending_list_selected;

    ESP_LOGI(TAG, "apply_list_show: count=%d", list_count);
    for (int i = 0; i < list_count; i++) {
        lv_obj_t *lbl = lv_label_create(splash_root);
        lv_label_set_text(lbl, pending_list_items[i]);
        lv_obj_set_style_text_color(lbl,
            i == list_selected ? lv_color_white() : lv_color_hex(0x808080), 0);
        lv_obj_set_style_text_font(lbl, LIST_FONT, 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        lv_obj_set_width(lbl, TEXT_WIDTH);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(lbl, TEXT_USER_LEFT,
                       g_list_user_top + i * LIST_ROW_HEIGHT);
        list_labels[i] = lbl;
    }
}

static void apply_list_sel(void)
{
    if (pending_list_selected < 0 || pending_list_selected >= list_count) return;
    list_selected = pending_list_selected;
    for (int i = 0; i < list_count; i++) {
        if (!list_labels[i]) continue;
        lv_obj_set_style_text_color(list_labels[i],
            i == list_selected ? lv_color_white() : lv_color_hex(0x808080), 0);
    }
}

static void apply_list_hide(void)
{
    for (int i = 0; i < list_count; i++) {
        if (list_labels[i]) { lv_obj_del(list_labels[i]); list_labels[i] = NULL; }
    }
    list_count = 0;
}

static void apply_subtext(const char *t)
{
    if (t[0] == '\0') {
        if (splash_subtext) { lv_obj_del(splash_subtext); splash_subtext = NULL; }
        return;
    }
    if (!splash_subtext) {
        splash_subtext = lv_label_create(splash_root);
        lv_obj_set_style_text_color(splash_subtext, lv_color_white(), 0);
        lv_obj_set_style_text_font(splash_subtext, SUBTEXT_FONT, 0);
        lv_obj_set_style_bg_opa(splash_subtext, LV_OPA_TRANSP, 0);
        lv_obj_set_width(splash_subtext, TEXT_WIDTH);
        lv_label_set_long_mode(splash_subtext, LV_LABEL_LONG_WRAP);
    }
    lv_label_set_text(splash_subtext, t);
    lv_obj_set_pos(splash_subtext, TEXT_USER_LEFT, g_subtext_user_top);
}

void splash_tick(void)
{
    if (pending_compact) {
        pending_compact = false;
        apply_layout(true);
    }

    const char *t = pending_status;
    if (t != NULL) {
        pending_status = NULL;
        if (splash_status != NULL) {
            lv_label_set_text(splash_status, t);
            ESP_LOGI(TAG, "applied status: '%s'", t);
        }
    }

    const char *st = pending_subtext;
    if (st != NULL) {
        pending_subtext = NULL;
        apply_subtext(st);
    }

    list_op_t op = pending_list_op;
    if (op != LIST_OP_NONE) {
        pending_list_op = LIST_OP_NONE;
        switch (op) {
            case LIST_OP_SHOW: apply_list_show(); break;
            case LIST_OP_SEL:  apply_list_sel();  break;
            case LIST_OP_HIDE: apply_list_hide(); break;
            default: break;
        }
    }
}

void splash_dismiss(void)
{
    apply_list_hide();
    if (splash_subtext) { lv_obj_del(splash_subtext); splash_subtext = NULL; }
    if (splash_root != NULL) {
        lv_obj_del(splash_root);
        splash_root = NULL;
        splash_status = NULL;
        splash_logo = NULL;
    }
}
