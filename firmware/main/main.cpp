#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "bt_keyboard.hpp"
#include "input.hpp"
#include "trs-keyboard.h"
#include "ui.h"
#include <iostream>

extern "C" {
#include "TCA9554PWR.h"
#include "I2C_Driver.h"
#include "mcp23017.h"
#include "ST7701S.h"
#include "LVGL_Driver.h"
#include "trs-lib.h"
#include "splash.h"
#include "wifi_manager.h"
#include "input_test.h"
}
#include "settings.h"
#include "trs_memory.h"
#include "trs_screen.h"
#include "trs.h"
#include "wifi.h"
#include "storage.h"
#include "trs-io.h"
#include "trs-fs.h"
#include "event.h"
#include "sound.h"
#include "games_cache.h"
#include <string>
#include <vector>

static constexpr char const *TAG = "Main";

BTKeyboard bt_keyboard;

static ScreenBuffer* origScreenBuffer = nullptr;
static ScreenBuffer* backgroundBuffer = nullptr;

void pairing_handler(uint32_t pid) {
  window_t wnd;

  z80_pause();
  std::cout << "Please enter the following pairing code, " << std::endl
            << "followed with ENTER on your keyboard: " << std::dec << pid << std::endl;
  
  if (origScreenBuffer == nullptr) {
    origScreenBuffer = trs_screen.getTop();
    backgroundBuffer = new ScreenBuffer(origScreenBuffer->getMode());
    trs_screen.push(backgroundBuffer);
    ScreenBuffer* screenBuffer = new ScreenBuffer(origScreenBuffer->getMode());
    trs_screen.push(screenBuffer);
    set_screen(screenBuffer->getBuffer(), backgroundBuffer->getBuffer(),
	     screenBuffer->getWidth(), screenBuffer->getHeight());
  }

  set_screen_to_background();
  init_window(&wnd, 0, 3, 0, 0);
  header("Bluetooth Pairing");
  wnd_print(&wnd, false, "\nPlease enter the following pairing code,\n");
  wnd_print(&wnd, false, "followed with ENTER on your keyboard: ");
  wnd_print_int32(&wnd, pid);
  screen_show(false);
}

void keyboard_lost_connection_handler() {
  ESP_LOGW(TAG, "====> Lost connection with keyboard <====");
}

void keyboard_connected_handler() {
  ESP_LOGI(TAG, "----> Connected to keyboard <----");
  if (origScreenBuffer != nullptr) {
    backgroundBuffer->copyBufferFrom(origScreenBuffer);
    screen_show(true);
    trs_screen.pop();
    trs_screen.pop();
    origScreenBuffer = nullptr;
    backgroundBuffer = nullptr;
    z80_resume();
  }
}

static volatile bool do_z80_reset = false;

// ---- UI mode machinery -----------------------------------------------------
//
// The LVGL menu UI (rotated 270, splash widgets) and the TRS-80 emulator
// (rotation 0, full-screen canvas) never coexist: the board's A7 "menu"
// button kills the running game and returns to the menu. All LVGL work —
// including the mode transition itself — happens on display_task; other
// tasks request a mode and block until it has been applied.

enum ui_mode_t { UI_MODE_MENU, UI_MODE_GAME };
static volatile ui_mode_t g_ui_mode_req = UI_MODE_MENU;
static volatile ui_mode_t g_ui_mode_cur = UI_MODE_MENU;  // written by display_task
static SemaphoreHandle_t  g_ui_mode_done = NULL;
static bool g_trs_screen_inited = false;   // display_task-only
static volatile bool g_z80_ready = false;  // z80_task subsystem inits done

// Request a UI mode and block until display_task has applied it.
static void ui_set_mode(ui_mode_t m) {
  if (g_ui_mode_cur == m) return;
  g_ui_mode_req = m;
  xSemaphoreTake(g_ui_mode_done, portMAX_DELAY);
}

static void wait_z80_ready() {
  while (!g_z80_ready) vTaskDelay(pdMS_TO_TICKS(50));
}

// If non-null when z80_task starts the emulator, it loads this CMD blob
// over the freshly-reset Z80 memory and jumps to the parsed entry point.
// Owned by main; lives in PSRAM via std::vector backing in main flow.
static const uint8_t *volatile g_launch_cmd_data = nullptr;
static volatile size_t          g_launch_cmd_size = 0;
static volatile uint16_t        g_launch_entry    = 0;

static bool key_report_contains(const BTKeyboard::KeyInfo &inf, uint8_t hid_code) {
  // keys[0] is the modifier byte; keys[1..size-1] are pressed HID usage codes.
  for (int i = 1; i < inf.size && i < BTKeyboard::MAX_KEY_DATA_SIZE; i++) {
    if (inf.keys[i] == hid_code) return true;
  }
  return false;
}

// HID usage codes we recognise directly from raw reports.
static constexpr uint8_t HID_ENTER = 0x28;
static constexpr uint8_t HID_ESC   = 0x29;
static constexpr uint8_t HID_MENU_BTN = 0x4B;  // board button A7 (HID PageUp)

// Block until a HID report contains the given usage-code as a press.
static void wait_for_hid_press(uint8_t hid_code) {
  ESP_LOGI(TAG, "wait_for_hid_press(0x%02x): connected=%d",
           hid_code, bt_keyboard.is_connected());
  BTKeyboard::KeyInfo inf;
  while (true) {
    if (!input_wait_event(inf, pdMS_TO_TICKS(2000))) {
      continue;
    }
    for (int i = 1; i < inf.size && i < BTKeyboard::MAX_KEY_DATA_SIZE; i++) {
      if (inf.keys[i] == hid_code) return;
    }
  }
}

// Block until a HID report contains ANY of the given usage codes pressed.
// Returns the matched code (one of `codes`). 0 if a 0-length list was passed.
static uint8_t wait_for_any_hid_press(const uint8_t *codes, int n) {
  BTKeyboard::KeyInfo inf;
  while (true) {
    if (!input_wait_event(inf, pdMS_TO_TICKS(2000))) continue;
    for (int i = 1; i < inf.size && i < BTKeyboard::MAX_KEY_DATA_SIZE; i++) {
      uint8_t k = inf.keys[i];
      if (k == 0) continue;
      for (int j = 0; j < n; j++) {
        if (k == codes[j]) return k;
      }
    }
  }
}

// Drop any pending input events. Used after we leave a sub-screen on a key
// press, so the matching release event (or a held repeat) doesn't trigger an
// action on the screen we just returned to. input_flush() also resets the
// ASCII translator's key-repeat state — emptying only the queue would eat
// the release report and leave the repeat armed, which made the next menu
// receive a phantom ENTER (e.g. Settings self-selecting "Wi-Fi Setup" right
// after leaving the TRS-80 config screen).
static void drain_bt_events(int settle_ms = 150) {
  vTaskDelay(pdMS_TO_TICKS(settle_ms));
  input_flush();
}

// ---- Wi-Fi setup flow (runs on flow_task, drawing into the splash UI) ----

// ASCII codes produced by input_wait_ascii for special keys.
static constexpr char K_ENTER     = 0x0D;
static constexpr char K_ESC       = 0x1B;
static constexpr char K_BACKSPACE = 0x08;
static constexpr char K_RIGHT     = (char)0x95;
static constexpr char K_LEFT      = (char)0x96;
static constexpr char K_DOWN      = (char)0x97;
static constexpr char K_UP        = (char)0x98;
static constexpr char K_MENU      = (char)0x92;  // A7 menu button (HID PageUp)

// Returns the chosen index in [0, n), or -1 if the user pressed ESC.
static int run_wifi_picker(const wifi_mgr_ap_t *aps, int n) {
  const char *items[WIFI_MGR_MAX_APS];
  for (int i = 0; i < n; i++) items[i] = aps[i].ssid;

  int sel = 0;
  splash_set_status("Select Wi-Fi network (UP/DOWN, ENTER):");
  splash_show_list(items, n, sel);

  while (true) {
    char ch = input_wait_ascii(true);
    if (ch == K_DOWN) {
      if (sel < n - 1) { sel++; splash_set_list_selection(sel); }
    } else if (ch == K_UP) {
      if (sel > 0)     { sel--; splash_set_list_selection(sel); }
    } else if (ch == K_ENTER) {
      return sel;
    } else if (ch == K_ESC || ch == K_MENU) {
      return -1;
    }
  }
}

// Returns true if the user submitted a password (written into `out`),
// false if they pressed ESC.
static bool run_password_input(const char *ssid, char *out, size_t out_len) {
  char buf[WIFI_MGR_PASS_LEN];
  buf[0] = '\0';
  size_t len = 0;

  char prompt[64];
  snprintf(prompt, sizeof(prompt), "Password for %s:", ssid);
  splash_set_status(prompt);
  splash_set_subtext(" ");  // start with a space so the line takes up vertical room

  while (true) {
    char ch = input_wait_ascii(true);
    if (ch == K_ENTER) {
      if (out_len > 0) {
        strncpy(out, buf, out_len - 1);
        out[out_len - 1] = '\0';
      }
      splash_set_subtext("");
      return true;
    } else if (ch == K_ESC || ch == K_MENU) {
      splash_set_subtext("");
      return false;
    } else if (ch == K_BACKSPACE) {
      if (len > 0) {
        buf[--len] = '\0';
        splash_set_subtext(len > 0 ? buf : " ");
      }
    } else if (ch >= 0x20 && ch < 0x7F && len < sizeof(buf) - 1) {
      buf[len++] = ch;
      buf[len] = '\0';
      // Static buffer is OK: splash copies into its pending slot, but
      // splash_set_subtext stores only the pointer until splash_tick reads
      // it. Make a static copy so the pointer remains valid.
      static char display[WIFI_MGR_PASS_LEN];
      strncpy(display, buf, sizeof(display) - 1);
      display[sizeof(display) - 1] = '\0';
      splash_set_subtext(display);
    }
  }
}

// Returns true on successful connect.
static bool try_connect(const char *ssid, const char *password) {
  char msg[80];
  snprintf(msg, sizeof(msg), "Connecting to %s...", ssid);
  splash_set_status(msg);
  bool ok = wifi_mgr_connect(ssid, password, 20000);
  if (ok) {
    snprintf(msg, sizeof(msg), "Connected to %s", ssid);
    splash_set_status(msg);
    ESP_LOGI(TAG, "Wi-Fi connected to '%s'", ssid);
  } else {
    splash_set_status("Connection failed");
    ESP_LOGW(TAG, "Wi-Fi connection to '%s' failed", ssid);
  }
  return ok;
}

// Interactive scan / pick / password flow, entered from Settings. The
// automatic preset/stored-creds connect lives in wifi_bg_task instead —
// the menu never blocks on Wi-Fi.
static void run_wifi_interactive_setup() {
  while (true) {
    splash_set_status("Scanning Wi-Fi networks...");
    wifi_mgr_ap_t aps[WIFI_MGR_MAX_APS];
    int n = wifi_mgr_scan(aps, WIFI_MGR_MAX_APS);
    if (n == 0) {
      splash_set_status("No networks found, retrying...");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    ESP_LOGI(TAG, "Found %d networks (deduped)", n);

    int idx = run_wifi_picker(aps, n);
    splash_hide_list();
    if (idx < 0) return;  // ESC / menu button: back to Settings

    char password[WIFI_MGR_PASS_LEN];
    password[0] = '\0';
    if (!run_password_input(aps[idx].ssid, password, sizeof(password))) {
      continue;  // ESC: back to picker (after rescan, simpler)
    }

    if (try_connect(aps[idx].ssid, password)) {
      wifi_mgr_save_creds(aps[idx].ssid, password);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));  // let the user read the failure message
  }
}

#define DETAIL_LINES_MAX 10
// Wrap detail/description text at ≈50 chars per line. Montserrat 20 in
// our 600-px wide text column (TEXT_WIDTH in splash.c) fits roughly 50
// characters with a small safety margin — splash uses LV_LABEL_LONG_DOT
// to elide gracefully if a line is still too long.
#define DETAIL_WRAP_COLS 50
#define DETAIL_LINE_LEN  64

// Storage outlives the splash_show_list call (which keeps a pointer to the
// caller's strings until splash_tick copies them into its own buffer).
static char g_detail_lines[DETAIL_LINES_MAX][DETAIL_LINE_LEN];

// Word-wrap `text` into the static g_detail_lines starting at index `line`.
// Returns the new line count.
static int append_wrapped(int line, const std::string &text) {
  size_t pos = 0;
  while (pos < text.size() && line < DETAIL_LINES_MAX) {
    size_t end = pos + DETAIL_WRAP_COLS;
    if (end >= text.size()) {
      end = text.size();
    } else {
      // Wrap at the last space before the limit, if there is one.
      size_t sp = text.rfind(' ', end);
      if (sp != std::string::npos && sp > pos) end = sp;
    }
    snprintf(g_detail_lines[line], DETAIL_LINE_LEN, "%.*s",
             (int)(end - pos), text.c_str() + pos);
    line++;
    pos = end;
    while (pos < text.size() && text[pos] == ' ') pos++;
  }
  return line;
}

// Storage for the downloaded CMD bytes — allocated *lazily* in PSRAM the
// first time the user starts a game. Originally this was a 64 KB static
// array in BSS (internal SRAM), but that displaced Wi-Fi/BT coex working
// memory and caused association timeouts. Z80 address space is 64 KB so
// no real-world program exceeds that.
#define LAUNCH_CMD_MAX_BYTES (64 * 1024)
static uint8_t *g_launch_cmd_storage = nullptr;

static uint8_t *get_launch_cmd_storage() {
  if (!g_launch_cmd_storage) {
    g_launch_cmd_storage = (uint8_t *)
        heap_caps_malloc(LAUNCH_CMD_MAX_BYTES, MALLOC_CAP_SPIRAM);
    if (!g_launch_cmd_storage) {
      ESP_LOGE(TAG, "Failed to allocate %d bytes for launch CMD in PSRAM",
               LAUNCH_CMD_MAX_BYTES);
    }
  }
  return g_launch_cmd_storage;
}

// ---- Cached-games UI --------------------------------------------------------
//
// The Games menu is served entirely from the offline cache (games_cache);
// "Sync Games" in Settings populates it from RetroStore. No Wi-Fi needed
// to browse or play once synced.

// Result from show_cached_details so the caller knows whether to resume the
// picker or hand off to the emulator.
enum show_app_result_t {
  SHOW_APP_BACK,   // user pressed ESC; resume picker
  SHOW_APP_LAUNCH, // user pressed ENTER; g_launch_cmd_* are now armed
  SHOW_APP_MENU    // user pressed the A7 menu button; exit to main menu
};

// Show details for cached game `index`; ENTER loads its CMD from the cache
// and stages it for launch.
static show_app_result_t show_cached_details(int index) {
  const cached_game_t *g = games_cache_get(index);
  if (g == nullptr) return SHOW_APP_BACK;

  splash_hide_list();
  splash_set_subtext("");

  // Build on-screen detail lines. Each line stays within ~DETAIL_WRAP_COLS
  // chars so the splash's LV_LABEL_LONG_DOT elision doesn't kick in.
  int line = 0;
  // Truncate author (%.20s) to keep the "by X (year)" line short.
  snprintf(g_detail_lines[line++], DETAIL_LINE_LEN, "by %.20s (%d)",
           g->author, g->release_year);
  snprintf(g_detail_lines[line++], DETAIL_LINE_LEN, "Model %d  v%s",
           (int)g->model, g->version);
  if (g->description[0] == '\0') {
    snprintf(g_detail_lines[line++], DETAIL_LINE_LEN, "(no description)");
  } else {
    line = append_wrapped(line, std::string(g->description));
  }

  const char *lines[DETAIL_LINES_MAX];
  for (int i = 0; i < line; i++) lines[i] = g_detail_lines[i];

  splash_set_status(g->name);
  splash_show_list(lines, line, -1);  // -1 = no highlight
  splash_set_subtext("ESC: back to list");
  splash_set_subtext_right(g->has_cmd ? "ENTER: start" : "(no runnable image)");

  // Flush the ENTER that opened this screen (and its release) so it can't
  // immediately trigger the launch below.
  drain_bt_events();

  const uint8_t keys[] = { HID_ESC, HID_ENTER, HID_MENU_BTN };
  uint8_t pressed = wait_for_any_hid_press(keys, 3);

  splash_set_subtext("");
  splash_set_subtext_right("");

  if (pressed == HID_ENTER && g->has_cmd) {
    uint8_t *buf = get_launch_cmd_storage();
    size_t size = 0;
    if (buf != nullptr &&
        games_cache_load_cmd(index, buf, LAUNCH_CMD_MAX_BYTES, &size)) {
      g_launch_cmd_data = buf;
      g_launch_cmd_size = size;
      ESP_LOGI(TAG, "Staged %u-byte cached CMD for '%s'",
               (unsigned) size, g->name);
      char msg[80];
      snprintf(msg, sizeof(msg), "Starting %s...", g->name);
      splash_set_status(msg);
      vTaskDelay(pdMS_TO_TICKS(500));  // let the tick apply the stack buffer
      return SHOW_APP_LAUNCH;
    }
    splash_set_status("Failed to load game from cache");
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
  return (pressed == HID_MENU_BTN) ? SHOW_APP_MENU : SHOW_APP_BACK;
}

// The splash list shows at most 10 rows; scroll a window over the catalog.
#define GAMES_WINDOW 10

static void games_show_window(int top, int count, int sel) {
  const char *items[GAMES_WINDOW];
  int n = count - top;
  if (n > GAMES_WINDOW) n = GAMES_WINDOW;
  for (int i = 0; i < n; i++) items[i] = games_cache_get(top + i)->name;
  splash_show_list(items, n, sel - top);
}

// Browse the cached games. Returns true when a game has been staged for
// launch (g_launch_cmd_* set).
static bool run_games_menu() {
  splash_set_subtext("");
  const int count = games_cache_count();
  if (count == 0) {
    splash_hide_list();
    splash_set_status("No games cached - use Settings > Sync Games");
    vTaskDelay(pdMS_TO_TICKS(2500));
    return false;
  }

  static char title[32];
  snprintf(title, sizeof(title), "Games (%d)", count);
  splash_set_status(title);

  int sel = 0, top = 0;
  games_show_window(top, count, sel);

  while (true) {
    char ch = input_wait_ascii(true);
    if (ch == K_DOWN) {
      if (sel < count - 1) {
        sel++;
        if (sel >= top + GAMES_WINDOW) {
          top = sel - GAMES_WINDOW + 1;
          games_show_window(top, count, sel);
        } else {
          splash_set_list_selection(sel - top);
        }
      }
    } else if (ch == K_UP) {
      if (sel > 0) {
        sel--;
        if (sel < top) {
          top = sel;
          games_show_window(top, count, sel);
        } else {
          splash_set_list_selection(sel - top);
        }
      }
    } else if (ch == K_ENTER) {
      show_app_result_t r = show_cached_details(sel);
      if (r == SHOW_APP_LAUNCH) return true;
      if (r == SHOW_APP_MENU) break;  // A7: straight back to the main menu
      drain_bt_events();
      splash_set_status(title);
      games_show_window(top, count, sel);
    } else if (ch == K_ESC || ch == K_MENU) {
      break;
    }
  }

  splash_hide_list();
  return false;
}

// Realign the RGB panel's DMA after a burst of internal-flash writes.
// Flash erases stall the shared flash/PSRAM bus long enough to underrun the
// panel's bounce buffers, which can leave the picture shifted/wrapped. The
// driver catches most underruns itself; one explicit restart after the burst
// guarantees a clean frame. (Deliberately NOT using LCD_RGB_RESTART_IN_VSYNC:
// that restarts every frame, unconditionally, and flickers constantly.)
static void lcd_resync_after_flash_writes() {
  esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)
      lv_display_get_user_data(lv_display_get_default());
  if (panel != NULL) {
    esp_lcd_rgb_panel_restart(panel);
  }
}

// ---- Sync Games (Settings) --------------------------------------------------

// Progress text ping-pongs between two buffers so a snprintf never tears
// the string the LVGL task is currently applying.
static char g_sync_msg[2][96];
static int  g_sync_msg_idx = 0;

static void sync_progress_cb(int done, int total, const char *name) {
  g_sync_msg_idx ^= 1;
  snprintf(g_sync_msg[g_sync_msg_idx], sizeof(g_sync_msg[0]),
           "Syncing %d/%d: %s", done + 1, total, name);
  splash_set_status(g_sync_msg[g_sync_msg_idx]);
}

// Bracket around each flash-write burst: warn the user (the picture may
// drift while flash erases stall the panel's PSRAM feed), then realign the
// panel the moment the burst ends.
static void sync_flash_phase_cb(bool writing) {
  if (writing) {
    splash_set_status("Writing to flash (screen may glitch)...");
    vTaskDelay(pdMS_TO_TICKS(120));  // let the notice reach the panel first
  } else {
    lcd_resync_after_flash_writes();
  }
}

static void run_games_sync() {
  if (!wifi_mgr_is_connected()) {
    splash_set_status("Wi-Fi not connected - can't sync");
    vTaskDelay(pdMS_TO_TICKS(2000));
    return;
  }
  splash_hide_list();
  splash_set_status("Fetching game catalog...");
  int n = games_cache_sync(sync_progress_cb, sync_flash_phase_cb);
  g_sync_msg_idx ^= 1;
  if (n < 0) {
    snprintf(g_sync_msg[g_sync_msg_idx], sizeof(g_sync_msg[0]), "Sync failed");
  } else {
    snprintf(g_sync_msg[g_sync_msg_idx], sizeof(g_sync_msg[0]),
             "Synced %d games", n);
  }
  splash_set_status(g_sync_msg[g_sync_msg_idx]);
  vTaskDelay(pdMS_TO_TICKS(1800));
}

// ---- Background connectivity tasks -----------------------------------------
//
// BT and Wi-Fi both come up in the background; the main menu never blocks
// on either. The board buttons work from the first frame.

static void bt_task(void *arg) {
  (void) arg;
  if (bt_keyboard.setup(pairing_handler, keyboard_connected_handler,
                        keyboard_lost_connection_handler)) { // Must be called once
#if CONFIG_TRASHBOY_BT_SCAN_ENABLED
    // Try to auto-connect a previously paired keyboard; fall back to a
    // pairing scan until something connects.
    while (!bt_keyboard.is_connected()) {
      bt_keyboard.auto_connect_bonded_device();
      vTaskDelay(pdMS_TO_TICKS(2000));
      if (!bt_keyboard.is_connected()) {
        bt_keyboard.auto_connect_bonded_device();
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!bt_keyboard.is_connected()) {
          ESP_LOGI(TAG, "Scanning for keyboards to pair...");
          bt_keyboard.devices_scan();  // default duration is 5 seconds
          vTaskDelay(pdMS_TO_TICKS(5000));
        }
      }
    }
    ESP_LOGI(TAG, "----> BT keyboard ready <----");
#else
    // Developer toggle (CONFIG_TRASHBOY_BT_SCAN_ENABLED=n): one best-effort
    // auto-connect attempt so a paired keyboard still works if present.
    ESP_LOGW(TAG, "Bluetooth keyboard scanning disabled "
                  "(CONFIG_TRASHBOY_BT_SCAN_ENABLED=n)");
    bt_keyboard.auto_connect_bonded_device();
#endif
  }
  vTaskDelete(NULL);
}

// Wi-Fi status-bar text. Ping-pong between two buffers so a snprintf into
// one never tears the string the LVGL task is currently applying.
static char g_wifi_bar_buf[2][64];
static int  g_wifi_bar_idx = 0;

static void wifi_bar_set(const char *fmt, ...) {
  g_wifi_bar_idx ^= 1;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_wifi_bar_buf[g_wifi_bar_idx], sizeof(g_wifi_bar_buf[0]), fmt, ap);
  va_end(ap);
  splash_set_statusbar(g_wifi_bar_buf[g_wifi_bar_idx]);
}

// Auto-connect from preset/stored credentials, then keep the status bar in
// sync with reality (Settings may connect later, or the AP may drop).
static void wifi_bg_task(void *arg) {
  (void) arg;
  wifi_mgr_init();

  char ssid[WIFI_MGR_SSID_LEN];
  char password[WIFI_MGR_PASS_LEN];
  bool have = false;
#if CONFIG_TRASHBOY_WIFI_USE_PRESET
  // Developer toggle: SSID/password baked into the firmware.
  if (CONFIG_TRASHBOY_WIFI_PRESET_SSID[0] != '\0') {
    strlcpy(ssid, CONFIG_TRASHBOY_WIFI_PRESET_SSID, sizeof(ssid));
    strlcpy(password, CONFIG_TRASHBOY_WIFI_PRESET_PASSWORD, sizeof(password));
    have = true;
  } else {
    ESP_LOGW(TAG, "CONFIG_TRASHBOY_WIFI_USE_PRESET=y but SSID is empty");
  }
#endif
  if (!have) {
    have = wifi_mgr_load_creds(ssid, sizeof(ssid), password, sizeof(password));
  }

  if (have) {
    wifi_bar_set("connecting to %s...", ssid);
    ESP_LOGI(TAG, "Wi-Fi auto-connect to '%s'", ssid);
    wifi_mgr_connect(ssid, password, 25000);
  } else {
    wifi_bar_set("not configured - see Settings");
  }

  bool last = false, first = true;
  for (;;) {
    bool c = wifi_mgr_is_connected();
    if (c != last || first) {
      first = false;
      last = c;
      if (c) {
        // Settings saves creds on success, so NVS knows the current SSID.
        char cs[WIFI_MGR_SSID_LEN], cp[WIFI_MGR_PASS_LEN];
        if (wifi_mgr_load_creds(cs, sizeof(cs), cp, sizeof(cp))) {
          wifi_bar_set("%s", cs);
        } else if (have) {
          wifi_bar_set("%s", ssid);
        } else {
          wifi_bar_set("connected");
        }
      } else {
        wifi_bar_set(have ? "offline" : "not configured - see Settings");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ---- Menu flows (flow_task is the single input consumer) -------------------

// Show a list menu; returns the chosen index, or -1 on ESC / A7.
static int run_menu_select(const char *title, const char * const *items, int n) {
  int sel = 0;
  // Kill any armed key-repeat from the keypress that got us here, so a
  // slowly-released ENTER can't phantom-select item 0.
  input_flush();
  splash_set_status(title);
  splash_show_list(items, n, sel);
  while (true) {
    char ch = input_wait_ascii(true);
    if (ch == K_DOWN) {
      if (sel < n - 1) splash_set_list_selection(++sel);
    } else if (ch == K_UP) {
      if (sel > 0) splash_set_list_selection(--sel);
    } else if (ch == K_ENTER) {
      splash_hide_list();
      return sel;
    } else if (ch == K_ESC || ch == K_MENU) {
      splash_hide_list();
      return -1;
    }
  }
}

// Run one game session: switch the display to the emulator, boot the Z80
// (optionally straight into a downloaded CMD), and pump input into the
// emulator until the A7 menu button ends the session.
static void run_game_session() {
  wait_z80_ready();
  ui_set_mode(UI_MODE_GAME);

  mem_init();
  z80_reset();
  if (g_launch_cmd_data != nullptr && g_launch_cmd_size > 0) {
    ESP_LOGI(TAG, "Loading launched CMD (%u bytes) into Z80 memory",
             (unsigned) g_launch_cmd_size);
    uint16_t entry = trs_load_cmd(g_launch_cmd_data, g_launch_cmd_size);
    if (entry != 0) {
      ESP_LOGI(TAG, "Launched CMD entry = 0x%04x", entry);
      z80_set_pc(entry);
    } else {
      ESP_LOGW(TAG, "Launched CMD has no transfer (entry) block; "
                    "falling back to ROM boot");
    }
    g_launch_cmd_data = nullptr;
    g_launch_cmd_size = 0;
  }
  z80_resume();

  while (true) {
    BTKeyboard::KeyInfo inf;
    if (!input_wait_event(inf, pdMS_TO_TICKS(500))) continue;
    if (key_report_contains(inf, HID_MENU_BTN)) {
      break;  // A7: kill the game, back to the main menu
    }
    if ((inf.size == 4 && inf.keys[2] == 2    /* F5 */) ||
        (inf.size == 8 && inf.keys[2] == 0x3e /* F5 on Rii */)) {
      z80_pause();
      configure_pocket_trs();
      z80_resume();
    } else if (inf.size > 4 && inf.keys[0] == 5 &&
               (inf.keys[1] == 0x4c || inf.keys[2] == 0x4c) /* Ctrl+Alt+Del */) {
      do_z80_reset = true;
    } else {
      process_key(inf);
    }
  }

  z80_pause();
  if (trsSamplesGenerator != nullptr) {
    trsSamplesGenerator->flush();  // don't let a dying beep linger
  }
  ui_set_mode(UI_MODE_MENU);
  drain_bt_events();
  ESP_LOGI(TAG, "Game session ended - back to main menu");
}

// The trs-lib settings UI renders into the TRS screen, so show the emulator
// screen for it — with the Z80 kept paused. Also reachable in-game via F5.
static void run_trs_config() {
  wait_z80_ready();
  ui_set_mode(UI_MODE_GAME);
  // Settle + flush so the ENTER that selected this entry can't leak into
  // (or phantom-repeat inside) the trs-lib menu and auto-open "Configure".
  drain_bt_events();
  configure_pocket_trs();
  ui_set_mode(UI_MODE_MENU);
  drain_bt_events();
}

static void run_settings_menu() {
  while (true) {
    static const char *items[] = { "Wi-Fi Setup", "Sync Games",
                                   "TRS-80 Config", "Back" };
    int sel = run_menu_select("Settings", items, 4);
    if (sel == 0) {
      run_wifi_interactive_setup();
      splash_hide_list();
    } else if (sel == 1) {
      run_games_sync();
    } else if (sel == 2) {
      run_trs_config();
    } else {
      return;  // "Back", ESC or A7
    }
  }
}

static void flow_task(void *arg) {
  (void) arg;
  splash_set_compact();
  // NOTE: games_cache_mount() happens in app_main BEFORE LCD_Init — flash
  // work during early panel streaming kills the RGB DMA (see app_main).
  while (true) {
    // Show the cache size in the entry itself, e.g. "Games (34)".
    static char games_item[24];
    snprintf(games_item, sizeof(games_item), "Games (%d)",
             games_cache_count());
    const char *items[] = { games_item, "Settings" };
    int sel = run_menu_select("Main Menu", items, 2);
    if (sel == 0) {
      // Served from the offline cache — no Wi-Fi needed to browse or play.
      if (run_games_menu()) {
        run_game_session();
      }
    } else if (sel == 1) {
      run_settings_menu();
    }
  }
}

void z80_task(void *arg)
{
  init_settings();
  init_storage();
  init_events();
  init_trs_io();
  // init_trs_fs_posix() (SD-card FS for FreHD) is intentionally NOT called:
  // we don't use the SD slot (games come from the internal-flash cache), the
  // probe fails noisily on every boot with no card inserted, and it re-muxes
  // GPIO 1/2 — the same pins the ST7701S panel-init SPI uses. FreHD file ops
  // are null-safe without it (fileio.cpp returns FR_NOT_READY). Re-enable if
  // SD storage is ever actually used (e.g. disk-image support).
  // init_wifi() (from trs-io) is intentionally NOT called: it auto-connects
  // from its own NVS keys and starts the web-config AP if no creds are
  // stored. We drive the whole Wi-Fi lifecycle from the menu via
  // wifi_manager.
  init_trs_lib();
  init_sound();

  // Idle until the first game session resumes us. z80_run() sleeps in
  // 100 ms slices while paused; run_game_session() does the per-launch
  // mem_init / z80_reset / CMD load and then z80_resume().
  z80_pause();
  g_z80_ready = true;

  while (1) {
    if (do_z80_reset) {
      z80_reset();
      do_z80_reset = false;
    }
    z80_run();
  }
}

// Owns ALL LVGL work forever: applies UI-mode transitions, ticks whichever
// UI is active, and pumps lv_timer_handler. Keeping every LVGL call on this
// one task (core 1) avoids the cross-core render races we fought earlier.
static void display_task(void *arg)
{
  (void) arg;
  while (true) {
    if (g_ui_mode_req != g_ui_mode_cur) {
      if (g_ui_mode_req == UI_MODE_GAME) {
        // TRSCanvas pre-rotates its pixels into an unrotated 480x640 native
        // canvas, so LVGL must be at ROTATION_0 (the adaptive flush callback
        // then blits straight through). trs_screen.init() must run at
        // ROTATION_0 too — it reads the display resolution.
        lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_0);
        if (!g_trs_screen_inited) {
          trs_screen.init();
          trs_screen.push(new ScreenBuffer(MODE_TEXT_64x16));
          g_trs_screen_inited = true;
        } else {
          trs_screen.setVisible(true);
        }
        trs_screen.refresh();
      } else {
        // Hide the emulator canvas and give the display back to the menu UI
        // (rotated landscape). The splash widgets survived underneath the
        // canvas; a full invalidate repaints them through the sw-rotate path.
        trs_screen.setVisible(false);
        lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_270);
        lv_obj_invalidate(lv_scr_act());
      }
      g_ui_mode_cur = g_ui_mode_req;
      xSemaphoreGive(g_ui_mode_done);
    }

    if (g_ui_mode_cur == UI_MODE_GAME) {
      trs_screen.render();
    } else {
      splash_tick();
    }
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// Periodically log free heap in internal SRAM and PSRAM, plus the largest
// contiguous block in each. Helps diagnose memory pressure that affects
// Wi-Fi/BT coex.
static void heap_diag_task(void *arg) {
  while (true) {
    size_t free_sram      = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psram     = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_sram   = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t largest_psram  = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI("heap",
             "SRAM free=%u KB (largest %u KB)  PSRAM free=%u KB (largest %u KB)",
             (unsigned)(free_sram     / 1024),
             (unsigned)(largest_sram  / 1024),
             (unsigned)(free_psram    / 1024),
             (unsigned)(largest_psram / 1024));
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

extern "C" void app_main(void)
{
  // Bring up the input hub before any producer can post. BT reports are
  // routed here via the sink; physical buttons via the MCP poll callback.
  input_init();
  bt_keyboard.set_report_sink(input_post_bt);

  // ALL flash-heavy initialization must happen BEFORE LCD_Init: once the
  // RGB panel starts streaming, a multi-ms flash erase/write stalls the
  // shared flash/PSRAM bus and can kill the panel's DMA stream while it is
  // still establishing its first frames — with no VSYNC events left, not
  // even esp_lcd_rgb_panel_restart() can recover it (it runs in the VSYNC
  // handler). This was the intermittent black-screen-at-boot bug. NVS init
  // can write during recovery/migration; the games-cache mount does
  // wear-levelling/FAT work (and formats the partition on first boot).
  esp_err_t ret = nvs_flash_init();
  if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  games_cache_mount();

  // Initialize I2C (required by EXIO)
  I2C_Init();

  // Probe + configure MCP23017 button expander on the same I2C bus.
  // Non-fatal: missing chip just means buttons won't work.
  if (mcp23017_probe() == ESP_OK) {
    mcp23017_init();
    // Polled over I2C (no INT GPIO; GPIO4 = audio). Presses feed the input
    // hub, so board buttons drive the menus and the emulator like the BT
    // keyboard does.
    mcp23017_start_button_task(input_on_buttons);
  }

  // Initialize EXIO (required by LCD)
  EXIO_Init();

  // Initialize LCD
  LCD_Init();

  // Initialize LVGL
  LVGL_Init();

#if CONFIG_TRASHBOY_INPUT_TEST_MODE
  // Developer toggle: skip the whole BT / Wi-Fi / splash / RetroStore path
  // and boot straight into the touch + button input test screen. Never
  // returns.
  input_test_run();
#endif

  splash_init();
  splash_set_statusbar("starting...");
  g_ui_mode_done = xSemaphoreCreateBinary();

  // display_task owns all LVGL work (8 KB stack: the render pipeline
  // recurses deep enough to overflow 4 KB). flow_task drives the menus and
  // is the single input consumer; BT + Wi-Fi come up in the background.
  xTaskCreatePinnedToCore(display_task, "display", 8192, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(flow_task, "ui_flow", 8192, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(bt_task, "bt_task", 6000, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(wifi_bg_task, "wifi_bg", 4096, NULL, 4, NULL, 0);
  xTaskCreatePinnedToCore(heap_diag_task, "heap_diag", 3072, NULL, 1, NULL, 0);
  z80_task(NULL);
}
