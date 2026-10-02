// See input.hpp. Merges the BT keyboard and the physical buttons into
// one HID-report stream, and owns the ASCII translation (moved here
// from BTKeyboard so BTKeyboard is a pure producer).

#include "input.hpp"

#include <string.h>

#include "buttons.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

using KeyInfo = BTKeyboard::KeyInfo;

static const int MAXK = BTKeyboard::MAX_KEY_DATA_SIZE;  // 20

// Modifier masks (were BTKeyboard members; only the ASCII path uses them).
static const uint8_t CTRL_MASK     = 0x11;  // L_CTRL | R_CTRL
static const uint8_t SHIFT_MASK    = 0x22;  // L_SHIFT | R_SHIFT
static const uint8_t KEY_CAPS_LOCK = 0x39;

// HID usage -> ASCII (pairs: normal, shifted), indexed by
// (keycode - 4) * 2. Moved verbatim from BTKeyboard.
static const char shift_trans_dict_[] =
    "aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ"
    "1!2@3#4$5%6^7&8*9(0)"
    "\r\r" "\033\033" "\b\b" "\t\t" "  " "-_" "=+" "[{"
    "]}" "\\|" "??" ";:" "'\"" "`~" ",<" ".>" "/?"
    "\200\200"
    "\201\201\202\202\203\203\204\204\205\205\206\206"
    "\207\207\210\210\211\211\212\212\213\213\214\214"
    "\215\215\216\216\217\217"
    "\220\220" "\221\221" "\222\222" "\177\177" "\223\223"
    "\224\224" "\225\225" "\226\226" "\227\227" "\230\230";

// Button (pressed = 0 on the port) -> HID usage code + a FIXED slot in
// the merged report. Slots are >= 8 so they never collide with the BT
// boot report, which only ever uses keys[0..7]. Fixed slots also keep the
// ASCII path's per-index "newly pressed" detection stable.
//
// The two D-pads are double-assigned: the left and the right one share a
// key and a slot per direction, so pressing either produces the same key.
// The action buttons differ per side: left is CLEAR / SPACE, right is
// Enter / Esc. The shoulder buttons are not mapped.
struct BtnMap { button_t button; uint8_t hid; uint8_t slot; };
static const BtnMap BTN_MAP[] = {
  { BTN_L_DPAD_UP,      0x52, 8 },   // Up
  { BTN_R_DPAD_UP,      0x52, 8 },
  { BTN_L_DPAD_RIGHT,   0x4F, 9 },   // Right
  { BTN_R_DPAD_RIGHT,   0x4F, 9 },
  { BTN_L_DPAD_DOWN,    0x51, 10 },  // Down
  { BTN_R_DPAD_DOWN,    0x51, 10 },
  { BTN_L_DPAD_LEFT,    0x50, 11 },  // Left
  { BTN_R_DPAD_LEFT,    0x50, 11 },
  { BTN_L_ACTION_UPPER, 0x4A, 12 },  // CLEAR (HID Home; emulator maps to VK_HOME)
  { BTN_R_ACTION_UPPER, 0x28, 13 },  // Enter
  { BTN_L_ACTION_LOWER, 0x2C, 14 },  // Space
  { BTN_R_ACTION_LOWER, 0x29, 15 },  // Esc
  { BTN_OSK,            0x4E, 16 },  // OSK toggle (HID PageDown; see main.cpp)
  { BTN_MENU,           0x4B, 17 },  // menu/home (HID PageUp; see main.cpp)
};

// Merged-report slot for the on-screen keyboard's single key.
#define OSK_SLOT 18

static QueueHandle_t     s_queue = NULL;
static SemaphoreHandle_t s_lock  = NULL;
static KeyInfo           s_bt;            // latest BT report
static KeyInfo           s_btn;          // latest synthesized button report
static KeyInfo           s_osk;          // latest on-screen keyboard report
static KeyInfo           s_last_emitted; // for dedup

// ASCII-translation state (were BTKeyboard members).
static char       s_last_ch = 0;
static TickType_t s_repeat_period = 0;
static bool       s_key_avail[BTKeyboard::MAX_KEY_DATA_SIZE];
static bool       s_caps_lock = false;

static void key_zero(KeyInfo &k) {
  k.size = 0;
  k.modifier = (BTKeyboard::KeyModifier) 0;
  memset(k.keys, 0, sizeof(k.keys));
}

void input_init() {
  s_queue = xQueueCreate(10, sizeof(KeyInfo));
  s_lock  = xSemaphoreCreateMutex();
  key_zero(s_bt);
  key_zero(s_btn);
  key_zero(s_osk);
  key_zero(s_last_emitted);
  for (int i = 0; i < MAXK; i++) s_key_avail[i] = true;
}

// Union the three sources into one report and enqueue it if it changed.
// Caller must hold s_lock. BT occupies keys[0..7]; buttons and the OSK
// occupy their fixed slots >= 8, so a per-index OR is a clean union.
static void emit_merged_locked() {
  KeyInfo m;
  memset(&m, 0, sizeof(m));
  for (int i = 0; i < MAXK; i++) {
    m.keys[i] = s_bt.keys[i] | s_btn.keys[i] | s_osk.keys[i];
  }
  const uint8_t mod = (uint8_t) s_bt.modifier | (uint8_t) s_btn.modifier |
                      (uint8_t) s_osk.modifier;
  m.modifier = (BTKeyboard::KeyModifier) mod;
  m.keys[0]  = mod;  // report byte 0 carries the modifier

  // Preserve the BT report's native size when no button/OSK key is held,
  // so the size-based F5 / Ctrl-Alt-Del heuristics keep working. When one
  // IS held, grow size just enough to cover the highest occupied slot.
  m.size = s_bt.size;
  for (int i = MAXK - 1; i >= 1; i--) {
    if (m.keys[i]) { if (i + 1 > m.size) m.size = i + 1; break; }
  }
  // A modifier-only report (e.g. latched OSK shift) still needs keys[0]
  // inside the reported size.
  if (mod != 0 && m.size == 0) m.size = 1;

  if (m.modifier == s_last_emitted.modifier &&
      memcmp(m.keys, s_last_emitted.keys, sizeof(m.keys)) == 0) {
    return;  // no change
  }
  s_last_emitted = m;
  xQueueSendToBack(s_queue, &m, 0);  // drop if full; consumers set the pace
}

void input_post_bt(const KeyInfo &report) {
  if (s_lock == NULL) return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_bt = report;
  emit_merged_locked();
  xSemaphoreGive(s_lock);
}

extern "C" void input_on_buttons(uint8_t port_a, uint8_t port_b) {
  if (s_lock == NULL) return;

  // Pressed = grounded pin; fold both ports into one bit per button.
  const uint16_t pressed = buttons_pressed(port_a, port_b);

  KeyInfo btn;
  memset(&btn, 0, sizeof(btn));
  for (unsigned i = 0; i < sizeof(BTN_MAP) / sizeof(BTN_MAP[0]); i++) {
    const BtnMap &m = BTN_MAP[i];
    if (button_is_down(pressed, m.button)) {
      btn.keys[m.slot] = m.hid;
    }
  }
  btn.modifier = (BTKeyboard::KeyModifier) 0;
  btn.size = MAXK;

  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_btn = btn;
  emit_merged_locked();
  xSemaphoreGive(s_lock);
}

void input_post_osk(uint8_t hid, uint8_t modifier) {
  if (s_lock == NULL) return;
  KeyInfo r;
  memset(&r, 0, sizeof(r));
  r.modifier = (BTKeyboard::KeyModifier) modifier;
  if (hid != 0) {
    r.keys[OSK_SLOT] = hid;
    r.size = OSK_SLOT + 1;
  } else {
    r.size = (modifier != 0) ? 1 : 0;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_osk = r;
  emit_merged_locked();
  xSemaphoreGive(s_lock);
}

bool input_wait_event(KeyInfo &inf, TickType_t timeout) {
  if (s_queue == NULL) return false;
  return xQueueReceive(s_queue, &inf, timeout);
}

void input_flush(void) {
  if (s_queue != NULL) {
    KeyInfo inf;
    while (xQueueReceive(s_queue, &inf, 0)) {}
  }
  // Reset the ASCII translator so a key consumed before the flush can't
  // fire a phantom repeat at the next consumer (its release report may
  // just have been discarded above).
  s_last_ch = 0;
  s_repeat_period = 0;
  for (int i = 0; i < MAXK; i++) s_key_avail[i] = true;
}

// Assemble ASCII characters from the merged HID stream. Verbatim port of
// the old BTKeyboard::wait_for_ascii_char, using module-static state and
// reading from the merged queue instead of BTKeyboard's own.
char input_wait_ascii(bool forever) {
  KeyInfo inf;

  while (true) {
    if (!input_wait_event(
            inf, (s_last_ch == 0) ? (forever ? portMAX_DELAY : 0) : s_repeat_period)) {
      s_repeat_period = pdMS_TO_TICKS(300);
      return s_last_ch;
    }

    int k = -1;
    for (int i = 0; i < MAXK; i++) {
      if ((k < 0) && s_key_avail[i] && (inf.keys[i] != 0)) {
        k = i;
      }
      s_key_avail[i] = inf.keys[i] == 0;
    }

    if (k < 0) {
      bool all_released = true;
      for (int i = 0; i < MAXK; i++) {
        if (inf.keys[i] != 0) { all_released = false; break; }
      }
      if (all_released) s_last_ch = 0;
      continue;
    }

    char ch = inf.keys[k];

    if (ch >= 4) {
      if ((uint8_t) inf.modifier & CTRL_MASK) {
        if (ch < (3 + 26)) {
          s_repeat_period = pdMS_TO_TICKS(500);
          return s_last_ch = (ch - 3);
        }
      } else if (ch <= 0x52) {
        if (ch == KEY_CAPS_LOCK) {
          s_caps_lock = !s_caps_lock;
        }

        const bool shift = (uint8_t) inf.modifier & SHIFT_MASK;
        s_repeat_period = pdMS_TO_TICKS(500);
        if (shift != s_caps_lock) {
          // Exactly one of Shift / Caps Lock active -> shifted glyph.
          return s_last_ch = shift_trans_dict_[((ch - 4) << 1) + 1];
        } else {
          return s_last_ch = shift_trans_dict_[(ch - 4) << 1];
        }
      }
    }

    s_last_ch = 0;
  }
}
