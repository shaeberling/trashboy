// TrashBoy on the Seeed SenseCAP Watcher — experimental.
//
// No menus, no radios, no sound: the device boots straight into one game,
// which is part of the firmware image (EMBED_FILES in main/CMakeLists.txt).
// The wheel is the whole input:
//
//   turn left / right     LEFT / RIGHT arrow key
//   press                 SPACE (Breakdown: start, serve)
//   hold for 2 seconds    restart the game
//
// Three tasks: the Z80 on the main task (core 0), the wheel on core 0, the
// display on core 1. The emulator core is the one the Waveshare build uses
// (components/ptrs); only what draws its screen and feeds its keyboard
// differs.

#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "trs.h"
#include "trs-keyboard.h"
#include "trs_screen.h"

#include "watcher_board.h"
#include "watcher_screen.h"

#include "font/font_m3"

static constexpr char const *TAG = "Main";

// Breakdown, downloaded from RetroStore by scripts/watcher.sh build.
extern const uint8_t game_cmd_start[] asm("_binary_breakdown_cmd_start");
extern const uint8_t game_cmd_end[]   asm("_binary_breakdown_cmd_end");

// ---- Wheel -> keys ----------------------------------------------------------
//
// A key cannot be "inserted" into a TRS-80: games read the key matrix, and
// Breakdown samples it once per game tick, about every 35 ms, moving the
// paddle one column for each tick it finds an arrow key down (two columns
// once the key has been down for about 9 ticks). So each step of the wheel
// holds the arrow key down for a while, and steps that arrive while it is
// still down extend the hold. Turning the other way drops what is left and
// starts over in the new direction.
//
// One step is two quadrature counts, the unit Seeed's firmware reports as one
// "knob left" / "knob right". If the paddle moves too little or too much per
// click of the wheel, WHEEL_STEP_HOLD_MS is the number to change; if it runs
// on for too long after a fast spin, WHEEL_MAX_HOLD_MS.

#define INPUT_PERIOD_MS       10
#define WHEEL_COUNTS_PER_STEP 2
#define WHEEL_STEP_HOLD_MS    70    // two game ticks: about two columns
#define WHEEL_MAX_HOLD_MS     420
#define WHEEL_REST_MS         200   // a half step left over is forgotten after this
#define WHEEL_RIGHT_IS_POSITIVE 1   // 0 swaps the two directions

#define BUTTON_DEBOUNCE_SAMPLES 3
#define BUTTON_RESTART_HOLD_MS  2000

// HID usage codes, as process_key() expects them.
static constexpr uint8_t HID_SPACE = 0x2C;
static constexpr uint8_t HID_RIGHT = 0x4F;
static constexpr uint8_t HID_LEFT  = 0x50;

// Set by the wheel task, taken by the Z80 loop: load the game and start it.
static volatile bool g_restart_game = true;

// Hand the emulator the set of keys that are down, as a HID report.
static void send_keys(bool left, bool right, bool space) {
  BTKeyboard::KeyInfo inf;
  memset(&inf, 0, sizeof(inf));
  // keys[0] is the modifier byte; pressed keys follow.
  inf.size = 4;
  if (left)  inf.keys[1] = HID_LEFT;
  if (right) inf.keys[2] = HID_RIGHT;
  if (space) inf.keys[3] = HID_SPACE;
  process_key(inf);
}

static void input_task(void *arg) {
  (void) arg;

  int pending_counts = 0;   // counts not yet worth a step
  int rest_ms = 0;          // time since the wheel last moved
  int dir = 0;              // -1 left, +1 right: the arrow key that is down
  int hold_ms = 0;          // how much longer it stays down

  // The wheel may be held at power-on (on battery, pressing it is what turns
  // the board on): that is not a press, so wait for the first release.
  bool button = watcher_button_pressed();
  bool button_armed = !button;
  int button_changed = 0;
  int64_t pressed_at_us = 0;
  bool space = false;

  bool sent_left = false, sent_right = false, sent_space = false;
  TickType_t wake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(INPUT_PERIOD_MS));

    // Wheel.
    int counts = watcher_knob_read();
    if (!WHEEL_RIGHT_IS_POSITIVE) counts = -counts;
    if (counts != 0) {
      rest_ms = 0;
      pending_counts += counts;
      const int steps = pending_counts / WHEEL_COUNTS_PER_STEP;
      pending_counts -= steps * WHEEL_COUNTS_PER_STEP;
      if (steps != 0) {
        const int step_dir = steps > 0 ? 1 : -1;
        if (step_dir != dir) {
          dir = step_dir;
          hold_ms = 0;
        }
        hold_ms += (steps > 0 ? steps : -steps) * WHEEL_STEP_HOLD_MS;
        if (hold_ms > WHEEL_MAX_HOLD_MS) hold_ms = WHEEL_MAX_HOLD_MS;
      }
      // For tuning the constants above (log level DEBUG for this tag).
      ESP_LOGD(TAG, "wheel %+d counts: %s for %d ms", counts,
               dir < 0 ? "LEFT" : dir > 0 ? "RIGHT" : "-", hold_ms);
    } else if (rest_ms < WHEEL_REST_MS) {
      rest_ms += INPUT_PERIOD_MS;
      if (rest_ms >= WHEEL_REST_MS) pending_counts = 0;
    }
    const bool left = hold_ms > 0 && dir < 0;
    const bool right = hold_ms > 0 && dir > 0;
    if (hold_ms > 0) {
      hold_ms -= INPUT_PERIOD_MS;
      if (hold_ms <= 0) dir = 0;
    }

    // Push button.
    const bool raw = watcher_button_pressed();
    if (raw == button) {
      button_changed = 0;
    } else if (++button_changed >= BUTTON_DEBOUNCE_SAMPLES) {
      button_changed = 0;
      button = raw;
      if (!button) {
        space = false;
        button_armed = true;
      } else if (button_armed) {
        space = true;
        pressed_at_us = esp_timer_get_time();
      }
    }
    if (space && esp_timer_get_time() - pressed_at_us >=
                     (int64_t) BUTTON_RESTART_HOLD_MS * 1000) {
      ESP_LOGI(TAG, "wheel held: restarting the game");
      // Let go of SPACE, and stay deaf to the button until it is released:
      // still held, it would start the game from the title screen at once.
      space = false;
      button_armed = false;
      g_restart_game = true;
    }

    if (left != sent_left || right != sent_right || space != sent_space) {
      send_keys(left, right, space);
      sent_left = left;
      sent_right = right;
      sent_space = space;
    }
  }
}

// ---- Display ----------------------------------------------------------------

// Where the picture sits on the panel: centred, with x on a 4-pixel column.
#define SCREEN_X ((WATCHER_LCD_RES - WSCR_FB_W) / 2)
#define SCREEN_Y ((WATCHER_LCD_RES - WSCR_FB_H) / 2)
static_assert(SCREEN_X % 4 == 0 && WSCR_FB_W % 4 == 0,
              "the SPD2010 takes windows on 4-pixel columns only");

#define BACKLIGHT_PERCENT 80

// Redraws what changed on the TRS-80 screen, once per FreeRTOS tick. The
// frame buffer is internal RAM the panel's SPI DMA can send from directly;
// only the text rows that changed go out (0.35 ms each on the wire, 6 ms for
// the whole screen).
static void display_task(void *arg) {
  (void) arg;

  const size_t fb_bytes = WSCR_FB_W * WSCR_FB_H * sizeof(uint16_t);
  uint16_t *fb = (uint16_t *)
      heap_caps_malloc(fb_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (fb == nullptr || !wscr_init(fb, font_m3)) {
    ESP_LOGE(TAG, "no memory for the frame buffer");
    vTaskDelete(NULL);
    return;
  }
  esp_err_t err = watcher_lcd_init();
  if (err == ESP_OK) err = watcher_lcd_clear();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "display up: %dx%d picture at (%d, %d)",
           WSCR_FB_W, WSCR_FB_H, SCREEN_X, SCREEN_Y);

  bool lit = false;
  for (;;) {
    const uint32_t dirty = wscr_update(trs_screen.getTop()->getBuffer());
    // Send each run of changed text rows as one block.
    for (int row = 0; row < WSCR_ROWS; ) {
      if (!(dirty & (1u << row))) {
        row++;
        continue;
      }
      const int first = row;
      while (row < WSCR_ROWS && (dirty & (1u << row))) row++;
      err = watcher_lcd_draw(SCREEN_X, SCREEN_Y + first * WSCR_CELL_H,
                             WSCR_FB_W, (row - first) * WSCR_CELL_H,
                             fb + first * WSCR_CELL_H * WSCR_FB_W);
      if (err != ESP_OK) {
        // Draw everything again on the next pass rather than leave a hole.
        ESP_LOGW(TAG, "display write failed: %s", esp_err_to_name(err));
        wscr_invalidate();
        break;
      }
    }
    if (!lit) {
      // Only now, with the first picture on the panel.
      watcher_lcd_backlight(BACKLIGHT_PERCENT);
      lit = true;
    }
    vTaskDelay(1);
  }
}

// ---- Z80 --------------------------------------------------------------------

// Same launch as the Waveshare build's run_game_session(): reset the machine
// and load the program over zeroed RAM; the ROM never boots.
static void start_game() {
  z80_reset();
  trs_screen.clear();
  const uint16_t entry = trs_load_cmd(game_cmd_start,
                                      game_cmd_end - game_cmd_start);
  if (entry != 0) {
    z80_set_pc(entry);
  } else {
    ESP_LOGE(TAG, "embedded CMD has no entry address");
  }
  ESP_LOGI(TAG, "game started: %u-byte CMD, entry 0x%04x",
           (unsigned) (game_cmd_end - game_cmd_start), entry);
}

extern "C" void app_main(void)
{
  // First: this is what keeps the board powered when it runs from battery.
  ESP_ERROR_CHECK(watcher_board_init());

  trs_screen.push(new ScreenBuffer(MODE_TEXT_64x16));

  xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(input_task, "wheel", 4096, NULL, 6, NULL, 0);

  while (true) {
    if (g_restart_game) {
      g_restart_game = false;
      start_game();
    }
    z80_run();
  }
}
