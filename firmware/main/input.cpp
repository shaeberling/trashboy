// See input.hpp. Merges the BT keyboard and the physical buttons into
// one HID-report stream, and owns the ASCII translation (moved here
// from BTKeyboard so BTKeyboard is a pure producer).

#include "input.hpp"

#include <string.h>

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
// The D-pad (bits 1-4) is double-assigned: pressing either column's
// button produces the same key. Bits 5-6 are column-specific: column A
// is CLEAR / SPACE, column B is Enter / Esc.
enum BtnPort { PORT_A, PORT_B, PORT_AB };
struct BtnMap { BtnPort port; uint8_t bit; uint8_t hid; uint8_t slot; };
static const BtnMap BTN_MAP[] = {
  { PORT_AB, 1, 0x52, 8 },   // A1/B1 -> Up
  { PORT_AB, 2, 0x4F, 9 },   // A2/B2 -> Right
  { PORT_AB, 3, 0x51, 10 },  // A3/B3 -> Down
  { PORT_AB, 4, 0x50, 11 },  // A4/B4 -> Left
  { PORT_A,  5, 0x4A, 12 },  // A5 -> CLEAR (HID Home; emulator maps to VK_HOME)
  { PORT_B,  5, 0x28, 13 },  // B5 -> Enter
  { PORT_A,  6, 0x2C, 14 },  // A6 -> Space
  { PORT_B,  6, 0x29, 15 },  // B6 -> Esc
  { PORT_B,  7, 0x1E, 16 },  // B7 -> "1"
  { PORT_A,  7, 0x4B, 17 },  // A7 -> menu/home (HID PageUp; see main.cpp)
};

static QueueHandle_t     s_queue = NULL;
static SemaphoreHandle_t s_lock  = NULL;
static KeyInfo           s_bt;            // latest BT report
static KeyInfo           s_btn;          // latest synthesized button report
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
  key_zero(s_last_emitted);
  for (int i = 0; i < MAXK; i++) s_key_avail[i] = true;
}

// Union the two sources into one report and enqueue it if it changed.
// Caller must hold s_lock. BT occupies keys[0..7]; buttons occupy their
// fixed slots >= 8, so a per-index OR is a clean union.
static void emit_merged_locked() {
  KeyInfo m;
  memset(&m, 0, sizeof(m));
  for (int i = 0; i < MAXK; i++) {
    m.keys[i] = s_bt.keys[i] | s_btn.keys[i];
  }
  const uint8_t mod = (uint8_t) s_bt.modifier | (uint8_t) s_btn.modifier;
  m.modifier = (BTKeyboard::KeyModifier) mod;
  m.keys[0]  = mod;  // report byte 0 carries the modifier

  // Preserve the BT report's native size when no button is held, so the
  // size-based F5 / Ctrl-Alt-Del heuristics in keyb_task keep working.
  // When a button IS held, grow size just enough to cover its fixed slot.
  m.size = s_bt.size;
  for (int i = MAXK - 1; i >= 0; i--) {
    if (s_btn.keys[i]) { if (i + 1 > m.size) m.size = i + 1; break; }
  }

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

  // Pressed = grounded pin. One mask per column (bit set = that column's
  // button is down).
  const uint8_t pa = (uint8_t) ~port_a;
  const uint8_t pb = (uint8_t) ~port_b;

  KeyInfo btn;
  memset(&btn, 0, sizeof(btn));
  for (unsigned i = 0; i < sizeof(BTN_MAP) / sizeof(BTN_MAP[0]); i++) {
    const BtnMap &m = BTN_MAP[i];
    uint8_t src = (m.port == PORT_A) ? pa : (m.port == PORT_B) ? pb : (pa | pb);
    if ((src >> m.bit) & 1) {
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
