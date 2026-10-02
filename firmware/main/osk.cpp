// See osk.h. Coordinate conventions (see AGENTS.md "Display" rules):
//
//   USER frame     = what the player sees: landscape, 640 wide x 480 tall.
//   NATIVE frame   = panel-native portrait, 480 wide x 640 tall. In GAME
//                    mode LVGL runs at ROTATION_0, i.e. native coords.
//
//   draw:   user (ux,uy)  -> native (nx,ny) = (uy, 639 - ux)
//   touch:  Touch_Read gives mirrored native (ix,iy)
//           -> user (ux,uy) = (iy, 479 - ix)
//           (verified against the calibration table in Touch.c)

#include "osk.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "Touch_Driver/Touch.h"
#include "input.hpp"
#include "trs_screen.h"

// Authentic TRS-80 Model III glyphs: 256 chars x 12 row-bytes, bit 7 =
// leftmost pixel. `const` at namespace scope has internal linkage in C++,
// so including the definition here (as trs_screen.cpp does) is safe.
#include "font/font_m3"

static const char *TAG = "osk";

// ---------- geometry (USER frame) -------------------------------------------

#define USER_W        640
#define USER_H        480
#define NATIVE_W      480
#define NATIVE_H      640

#define KEY_H         52
#define KEY_W         59
#define KEY_GAP       4
#define SHIFT_W       90
#define GLYPH_SCALE   2                 // 8x12 font -> 16x24 on the key
#define KB_BOTTOM_PAD 4

#define ROWS          4
#define KB_TOP        (USER_H - ROWS * KEY_H - (ROWS - 1) * KEY_GAP - KB_BOTTOM_PAD)

#define MAX_KEYS      40
#define HID_LSHIFT_MODIFIER 0x02

typedef struct {
  uint8_t hid;      // 0 for the shift key
  char    label;    // glyph drawn on the key
  int16_t x, y, w, h;  // USER coords
  bool    is_shift;
} osk_key_t;

static osk_key_t s_keys[MAX_KEYS];
static int       s_nkeys = 0;

// ---------- state ------------------------------------------------------------

static bool s_touch_available = false;

// Cross-task requests (set anywhere, consumed by osk_tick on display_task).
static volatile bool s_toggle_pending = false;
static volatile bool s_hide_pending   = false;

// display_task-only state.
static lv_obj_t     *s_canvas = NULL;
static lv_color32_t *s_buf    = NULL;
static bool          s_visible = false;
static bool          s_shift = false;
static int           s_pressed_key = -1;   // index into s_keys, -1 = none
static bool          s_touching = false;
static int           s_release_misses = 0; // debounce touch-up
static TickType_t    s_last_poll = 0;

#define POLL_INTERVAL_MS   25
#define RELEASE_MISSES     2   // consecutive empty polls to count as touch-up

// ---------- colors (lv_color32_t = {blue, green, red, alpha}) ----------------

static const lv_color32_t COL_CLEAR    = { 0, 0, 0, 0 };
static const lv_color32_t COL_FILL     = { 25, 25, 25, 165 };
static const lv_color32_t COL_FILL_HIT = { 0x40, 0xFF, 0x40, 210 };  // green
static const lv_color32_t COL_BORDER   = { 230, 230, 230, 200 };
static const lv_color32_t COL_TEXT     = { 255, 255, 255, 255 };

// ---------- layout ------------------------------------------------------------

static uint8_t hid_for_char(char c) {
  if (c >= 'A' && c <= 'Z') return (uint8_t) (0x04 + (c - 'A'));
  if (c >= '1' && c <= '9') return (uint8_t) (0x1E + (c - '1'));
  if (c == '0') return 0x27;
  return 0;
}

static void add_key(char label, int x, int y, int w, bool is_shift) {
  if (s_nkeys >= MAX_KEYS) return;
  osk_key_t *k = &s_keys[s_nkeys++];
  k->hid = is_shift ? 0 : hid_for_char(label);
  k->label = label;
  k->x = (int16_t) x;
  k->y = (int16_t) y;
  k->w = (int16_t) w;
  k->h = KEY_H;
  k->is_shift = is_shift;
}

static void build_layout(void) {
  if (s_nkeys > 0) return;
  static const char *rows[3] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL" };
  for (int r = 0; r < 3; r++) {
    const int n = (int) strlen(rows[r]);
    const int total = n * KEY_W + (n - 1) * KEY_GAP;
    int x = (USER_W - total) / 2;
    const int y = KB_TOP + r * (KEY_H + KEY_GAP);
    for (int i = 0; i < n; i++) {
      add_key(rows[r][i], x, y, KEY_W, false);
      x += KEY_W + KEY_GAP;
    }
  }
  // Bottom row: wide one-shot Shift + ZXCVBNM.
  {
    static const char *zrow = "ZXCVBNM";
    const int n = (int) strlen(zrow);
    const int total = SHIFT_W + KEY_GAP + n * KEY_W + (n - 1) * KEY_GAP;
    int x = (USER_W - total) / 2;
    const int y = KB_TOP + 3 * (KEY_H + KEY_GAP);
    add_key('^', x, y, SHIFT_W, true);
    x += SHIFT_W + KEY_GAP;
    for (int i = 0; i < n; i++) {
      add_key(zrow[i], x, y, KEY_W, false);
      x += KEY_W + KEY_GAP;
    }
  }
}

// ---------- pixel plumbing (USER coords in, NATIVE buffer out) ----------------

static inline void put_user_px(int ux, int uy, lv_color32_t c) {
  // user -> native: nx = uy, ny = (NATIVE_H-1) - ux
  s_buf[(NATIVE_H - 1 - ux) * NATIVE_W + uy] = c;
}

static void fill_user_rect(int x, int y, int w, int h, lv_color32_t c) {
  for (int uy = y; uy < y + h; uy++) {
    for (int ux = x; ux < x + w; ux++) {
      put_user_px(ux, uy, c);
    }
  }
}

// 2 px border just inside the rect.
static void border_user_rect(int x, int y, int w, int h, lv_color32_t c) {
  fill_user_rect(x, y, w, 2, c);
  fill_user_rect(x, y + h - 2, w, 2, c);
  fill_user_rect(x, y, 2, h, c);
  fill_user_rect(x + w - 2, y, 2, h, c);
}

// Blit one TRS-80 glyph at GLYPH_SCALE, top-left at user (x,y).
static void draw_user_glyph(char ch, int x, int y, lv_color32_t c) {
  const unsigned char *rows = &font_m3[(uint8_t) ch * 12];
  for (int gy = 0; gy < 12; gy++) {
    for (int gx = 0; gx < 8; gx++) {
      if ((rows[gy] >> (7 - gx)) & 1) {
        fill_user_rect(x + gx * GLYPH_SCALE, y + gy * GLYPH_SCALE,
                       GLYPH_SCALE, GLYPH_SCALE, c);
      }
    }
  }
}

// Invalidate the native-frame area of a user-frame rect (canvas sits at
// (0,0) full screen, so canvas-local == screen coords).
static void invalidate_user_rect(int x, int y, int w, int h) {
  lv_area_t a;
  a.x1 = y;
  a.y1 = (NATIVE_H - 1) - (x + w - 1);
  a.x2 = y + h - 1;
  a.y2 = (NATIVE_H - 1) - x;
  lv_obj_invalidate_area(s_canvas, &a);
}

static void draw_key(int idx) {
  const osk_key_t *k = &s_keys[idx];
  const bool active = (idx == s_pressed_key) || (k->is_shift && s_shift);
  fill_user_rect(k->x, k->y, k->w, k->h, active ? COL_FILL_HIT : COL_FILL);
  border_user_rect(k->x, k->y, k->w, k->h, COL_BORDER);
  const int gw = 8 * GLYPH_SCALE, gh = 12 * GLYPH_SCALE;
  draw_user_glyph(k->label, k->x + (k->w - gw) / 2, k->y + (k->h - gh) / 2,
                  COL_TEXT);
}

static void draw_all_keys(void) {
  for (int i = 0; i < s_nkeys; i++) draw_key(i);
}

static void redraw_and_invalidate_key(int idx) {
  draw_key(idx);
  const osk_key_t *k = &s_keys[idx];
  invalidate_user_rect(k->x, k->y, k->w, k->h);
}

// ---------- hit test -----------------------------------------------------------

static int hit_test(int ux, int uy) {
  for (int i = 0; i < s_nkeys; i++) {
    const osk_key_t *k = &s_keys[i];
    if (ux >= k->x && ux < k->x + k->w && uy >= k->y && uy < k->y + k->h) {
      return i;
    }
  }
  return -1;
}

// ---------- show / hide ---------------------------------------------------------

static bool ensure_canvas(void) {
  if (s_canvas != NULL) return true;
  const size_t sz = (size_t) NATIVE_W * NATIVE_H * sizeof(lv_color32_t);
  s_buf = (lv_color32_t *) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if (s_buf == NULL) {
    ESP_LOGE(TAG, "no PSRAM for OSK canvas (%u bytes)", (unsigned) sz);
    return false;
  }
  memset(s_buf, 0, sz);  // fully transparent
  s_canvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(s_canvas, s_buf, NATIVE_W, NATIVE_H,
                       LV_COLOR_FORMAT_ARGB8888);
  lv_obj_remove_style_all(s_canvas);
  lv_obj_set_size(s_canvas, NATIVE_W, NATIVE_H);
  lv_obj_set_pos(s_canvas, 0, 0);
  lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
  build_layout();
  return true;
}

// Release anything the OSK is holding down in the input hub.
static void release_input_state(void) {
  s_pressed_key = -1;
  s_touching = false;
  s_release_misses = 0;
  s_shift = false;
  input_post_osk(0, 0);
}

static void apply_show(void) {
  if (!ensure_canvas()) return;
  s_visible = true;
  // The emulator screen normally writes straight to the panel, which would
  // paint over the keys: route it through LVGL while the overlay is up.
  trs_screen.setOverlayActive(true);
  release_input_state();
  draw_all_keys();
  lv_obj_move_foreground(s_canvas);  // above the TRS canvas (created later)
  lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
  invalidate_user_rect(0, KB_TOP, USER_W, USER_H - KB_TOP);
  ESP_LOGI(TAG, "shown");
}

static void apply_hide(void) {
  if (!s_visible) return;
  s_visible = false;
  release_input_state();
  if (s_canvas != NULL) {
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);  // invalidates its area
  }
  trs_screen.setOverlayActive(false);
  ESP_LOGI(TAG, "hidden");
}

// ---------- touch polling --------------------------------------------------------

static void poll_touch(void) {
  const TickType_t now = xTaskGetTickCount();
  if (now - s_last_poll < pdMS_TO_TICKS(POLL_INTERVAL_MS)) return;
  s_last_poll = now;

  int ix = 0, iy = 0;
  const bool touched = Touch_Read(&ix, &iy);

  if (touched) {
    s_release_misses = 0;
    if (!s_touching) {
      s_touching = true;
      // touch -> user frame (see header comment)
      const int ux = iy;
      const int uy = (NATIVE_W - 1) - ix;
      const int idx = hit_test(ux, uy);
      if (idx >= 0) {
        const osk_key_t *k = &s_keys[idx];
        if (k->is_shift) {
          s_shift = !s_shift;
          input_post_osk(0, s_shift ? HID_LSHIFT_MODIFIER : 0);
          redraw_and_invalidate_key(idx);
        } else {
          s_pressed_key = idx;
          input_post_osk(k->hid, s_shift ? HID_LSHIFT_MODIFIER : 0);
          redraw_and_invalidate_key(idx);
        }
      }
    }
    // While held: key stays down (like a real keyboard); ignore drags.
    return;
  }

  if (s_touching && ++s_release_misses >= RELEASE_MISSES) {
    s_touching = false;
    s_release_misses = 0;
    if (s_pressed_key >= 0) {
      const int idx = s_pressed_key;
      s_pressed_key = -1;
      // Key up; one-shot shift clears after a character.
      if (s_shift) {
        s_shift = false;
        input_post_osk(0, 0);
        // Un-highlight the shift key too.
        for (int i = 0; i < s_nkeys; i++) {
          if (s_keys[i].is_shift) { redraw_and_invalidate_key(i); break; }
        }
      } else {
        input_post_osk(0, 0);
      }
      redraw_and_invalidate_key(idx);
    }
  }
}

// ---------- public API ------------------------------------------------------------

void osk_set_touch_available(bool available) {
  s_touch_available = available;
  if (!available) {
    ESP_LOGW(TAG, "touch unavailable - on-screen keyboard disabled");
  }
}

void osk_request_toggle(void) {
  if (!s_touch_available) {
    ESP_LOGW(TAG, "toggle ignored: touch unavailable");
    return;
  }
  s_toggle_pending = true;
}

void osk_force_hide(void) {
  s_hide_pending = true;
}

void osk_tick(void) {
  if (s_hide_pending) {
    s_hide_pending = false;
    s_toggle_pending = false;
    apply_hide();
  }
  if (s_toggle_pending) {
    s_toggle_pending = false;
    if (s_visible) apply_hide(); else apply_show();
  }
  if (s_visible) poll_touch();
}
