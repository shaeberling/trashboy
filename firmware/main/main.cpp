#include <stdio.h>
#include <assert.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "bt_keyboard.hpp"
#include "trs-keyboard.h"
#include "ui.h"
#include <iostream>

extern "C" {
#include "TCA9554PWR.h"
#include "I2C_Driver.h"
#include "ST7701S.h"
#include "LVGL_Driver.h"
#include "trs-lib.h"
#include "splash.h"
#include "wifi_manager.h"
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
#include "retrostore.h"
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
static volatile bool splash_dismiss_requested = false;
static volatile bool splash_dismissed = false;

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
static constexpr uint8_t HID_S     = 0x16;   // letter 'S' / 's'

// Block until a HID report contains the given usage-code as a press.
static void wait_for_hid_press(uint8_t hid_code) {
  ESP_LOGI(TAG, "wait_for_hid_press(0x%02x): connected=%d",
           hid_code, bt_keyboard.is_connected());
  BTKeyboard::KeyInfo inf;
  while (true) {
    if (!bt_keyboard.wait_for_low_event(inf, pdMS_TO_TICKS(2000))) {
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
    if (!bt_keyboard.wait_for_low_event(inf, pdMS_TO_TICKS(2000))) continue;
    for (int i = 1; i < inf.size && i < BTKeyboard::MAX_KEY_DATA_SIZE; i++) {
      uint8_t k = inf.keys[i];
      if (k == 0) continue;
      for (int j = 0; j < n; j++) {
        if (k == codes[j]) return k;
      }
    }
  }
}

// Drop any events currently in the BT keyboard queue. Used after we leave
// a sub-screen on a key press, so the matching release event (or a held
// repeat) doesn't trigger an action on the screen we just returned to.
static void drain_bt_events(int settle_ms = 150) {
  vTaskDelay(pdMS_TO_TICKS(settle_ms));
  BTKeyboard::KeyInfo inf;
  while (bt_keyboard.wait_for_low_event(inf, 0)) {}
}

// ---- Wi-Fi setup flow (runs on keyb_task while the splash is still up) ----

// ASCII codes produced by BTKeyboard::wait_for_ascii_char for special keys.
static constexpr char K_ENTER     = 0x0D;
static constexpr char K_ESC       = 0x1B;
static constexpr char K_BACKSPACE = 0x08;
static constexpr char K_RIGHT     = (char)0x95;
static constexpr char K_LEFT      = (char)0x96;
static constexpr char K_DOWN      = (char)0x97;
static constexpr char K_UP        = (char)0x98;

// Returns the chosen index in [0, n), or -1 if the user pressed ESC.
static int run_wifi_picker(const wifi_mgr_ap_t *aps, int n) {
  const char *items[WIFI_MGR_MAX_APS];
  for (int i = 0; i < n; i++) items[i] = aps[i].ssid;

  int sel = 0;
  splash_set_status("Select Wi-Fi network (UP/DOWN, ENTER):");
  splash_show_list(items, n, sel);

  while (true) {
    char ch = bt_keyboard.wait_for_ascii_char(true);
    if (ch == K_DOWN) {
      if (sel < n - 1) { sel++; splash_set_list_selection(sel); }
    } else if (ch == K_UP) {
      if (sel > 0)     { sel--; splash_set_list_selection(sel); }
    } else if (ch == K_ENTER) {
      return sel;
    } else if (ch == K_ESC) {
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
    char ch = bt_keyboard.wait_for_ascii_char(true);
    if (ch == K_ENTER) {
      if (out_len > 0) {
        strncpy(out, buf, out_len - 1);
        out[out_len - 1] = '\0';
      }
      splash_set_subtext("");
      return true;
    } else if (ch == K_ESC) {
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

// Drives the splash through Wi-Fi setup until we're connected.
static void run_wifi_setup() {
  splash_set_compact();   // shrink logo to free up vertical room for the list
  splash_set_status("Initializing Wi-Fi...");
  wifi_mgr_init();

#if CONFIG_TRASHBOY_WIFI_USE_PRESET
  // Developer toggle (CONFIG_TRASHBOY_WIFI_USE_PRESET=y): connect to the
  // SSID/password baked into the firmware. Falls through to the normal
  // stored-creds / picker flow on failure so testing still has a recovery
  // path if the preset is wrong.
  {
    const char *preset_ssid = CONFIG_TRASHBOY_WIFI_PRESET_SSID;
    const char *preset_pass = CONFIG_TRASHBOY_WIFI_PRESET_PASSWORD;
    if (preset_ssid[0] != '\0') {
      ESP_LOGI(TAG, "Using preset Wi-Fi credentials for '%s'", preset_ssid);
      if (try_connect(preset_ssid, preset_pass)) return;
      splash_set_status("Preset Wi-Fi failed, falling back to picker...");
      vTaskDelay(pdMS_TO_TICKS(1500));
    } else {
      ESP_LOGW(TAG, "CONFIG_TRASHBOY_WIFI_USE_PRESET=y but SSID is empty");
    }
  }
#endif

  // 1) Try stored credentials first.
  {
    char ssid[WIFI_MGR_SSID_LEN];
    char password[WIFI_MGR_PASS_LEN];
    if (wifi_mgr_load_creds(ssid, sizeof(ssid), password, sizeof(password))) {
      ESP_LOGI(TAG, "Trying stored credentials for '%s'", ssid);
      if (try_connect(ssid, password)) return;
      splash_set_status("Stored credentials failed, rescanning...");
      vTaskDelay(pdMS_TO_TICKS(1500));
    }
  }

  // 2) Manual scan / pick / password loop.
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
    if (idx < 0) continue;  // ESC: rescan

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

#define APP_LIST_MAX  10
#define DETAIL_LINES_MAX 10
// Wrap detail/description text at ≈50 chars per line. Montserrat 20 in
// our 600-px wide text column (TEXT_WIDTH in splash.c) fits roughly 50
// characters with a small safety margin — splash uses LV_LABEL_LONG_DOT
// to elide gracefully if a line is still too long.
#define DETAIL_WRAP_COLS 50
#define DETAIL_LINE_LEN  64

// Storage outlives the splash_show_list call (which keeps a pointer to the
// caller's strings until splash_tick copies them into its own buffer).
static char g_app_titles[APP_LIST_MAX][DETAIL_LINE_LEN];
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

// Fetch the COMMAND-type media image for `app_id`, copy into
// g_launch_cmd_storage, parse the CMD entry address, and set the
// g_launch_cmd_data / g_launch_cmd_size / g_launch_entry handoff state.
// Returns true if the program is ready to be started by z80_task.
static bool download_and_prepare_launch(retrostore::RetroStore &rs,
                                        const std::string &app_id,
                                        const std::string &app_name) {
  splash_set_status("Downloading game...");
  splash_hide_list();
  splash_set_subtext("");
  splash_set_subtext_right("");

  std::vector<retrostore::RsMediaImage> images;
  std::vector<retrostore::RsMediaType> types = { retrostore::RsMediaType_COMMAND };
  if (!rs.FetchMediaImages(app_id, types, &images) || images.empty()) {
    ESP_LOGE(TAG, "No COMMAND image for app %s", app_id.c_str());
    splash_set_status("No .cmd image available for this game");
    vTaskDelay(pdMS_TO_TICKS(2500));
    return false;
  }

  const auto &img = images[0];
  if (img.data_size <= 0 || (size_t)img.data_size > LAUNCH_CMD_MAX_BYTES) {
    ESP_LOGE(TAG, "CMD size %d out of bounds (max %d)",
             img.data_size, LAUNCH_CMD_MAX_BYTES);
    splash_set_status("Game image too large");
    vTaskDelay(pdMS_TO_TICKS(2500));
    return false;
  }
  uint8_t *buf = get_launch_cmd_storage();
  if (!buf) {
    splash_set_status("Out of memory for game image");
    vTaskDelay(pdMS_TO_TICKS(2500));
    return false;
  }
  memcpy(buf, img.data.get(), img.data_size);
  g_launch_cmd_data = buf;
  g_launch_cmd_size = (size_t) img.data_size;
  ESP_LOGI(TAG, "Loaded %d-byte CMD '%s' for %s",
           img.data_size, img.filename.c_str(), app_name.c_str());

  char msg[80];
  snprintf(msg, sizeof(msg), "Starting %s...", app_name.c_str());
  splash_set_status(msg);
  vTaskDelay(pdMS_TO_TICKS(600));
  return true;
}

// Result from show_app_details so the caller knows whether to resume the
// picker or hand off to the emulator.
enum show_app_result_t {
  SHOW_APP_BACK,   // user pressed ESC; resume picker
  SHOW_APP_LAUNCH  // user pressed S; g_launch_cmd_* are now armed
};

// Show details for the given app and block until ESC (back) or S (start).
static show_app_result_t show_app_details(retrostore::RetroStore &rs,
                                          const std::string &app_id) {
  splash_set_status("Loading details...");
  splash_hide_list();
  splash_set_subtext("");

  retrostore::RsApp app;
  if (!rs.FetchApp(app_id, &app)) {
    ESP_LOGE(TAG, "RetroStore::FetchApp(%s) failed", app_id.c_str());
    splash_set_status("Failed to load details");
    vTaskDelay(pdMS_TO_TICKS(2000));
    return SHOW_APP_BACK;
  }

  ESP_LOGI(TAG, "App detail: %s -- %s (%d) model=%d v%s  desc_len=%u",
           app.name.c_str(), app.author.c_str(),
           app.release_year, (int)app.model, app.version.c_str(),
           (unsigned)app.description.size());
  ESP_LOGI(TAG, "  description: %s",
           app.description.empty() ? "(empty)" : app.description.c_str());

  // Build on-screen detail lines. Each line's unrotated bbox must stay
  // within ~DETAIL_WRAP_COLS chars wide so LVGL's transformed render
  // pipeline doesn't wedge on off-screen widgets.
  int line = 0;

  // Truncate author to first ~20 chars to keep "by X (year)" line short.
  {
    char who[24];
    snprintf(who, sizeof(who), "%s", app.author.c_str());
    snprintf(g_detail_lines[line++], DETAIL_LINE_LEN, "by %s (%d)",
             who, app.release_year);
  }
  snprintf(g_detail_lines[line++], DETAIL_LINE_LEN, "Model %d  v%s",
           (int)app.model, app.version.c_str());
  if (app.description.empty()) {
    if (line < DETAIL_LINES_MAX) {
      snprintf(g_detail_lines[line++], DETAIL_LINE_LEN, "(no description)");
    }
  } else {
    line = append_wrapped(line, app.description);
  }

  ESP_LOGI(TAG, "Showing %d detail lines:", line);
  for (int i = 0; i < line; i++) {
    ESP_LOGI(TAG, "  [%d] %s", i, g_detail_lines[i]);
  }

  const char *lines[DETAIL_LINES_MAX];
  for (int i = 0; i < line; i++) lines[i] = g_detail_lines[i];

  splash_set_status(app.name.c_str());
  splash_show_list(lines, line, -1);  // -1 = no highlight
  splash_set_subtext("ESC: back to list");
  splash_set_subtext_right("S: download & start");

  // Wait for ESC (back) or S (download + start).
  const uint8_t keys[] = { HID_ESC, HID_S };
  uint8_t pressed = wait_for_any_hid_press(keys, 2);

  splash_set_subtext("");
  splash_set_subtext_right("");

  if (pressed == HID_S) {
    if (download_and_prepare_launch(rs, app_id, app.name)) {
      return SHOW_APP_LAUNCH;
    }
    // Fetch failed; fall through to "back" so user can pick something else.
  }
  return SHOW_APP_BACK;
}

static void run_retrostore_browse() {
  splash_set_status("Fetching apps from RetroStore...");
  splash_hide_list();
  splash_set_subtext("");

  retrostore::RetroStore rs;
  std::vector<retrostore::RsAppNano> apps;
  bool ok = rs.FetchAppsNano(0, APP_LIST_MAX, &apps);
  if (!ok || apps.empty()) {
    ESP_LOGE(TAG, "RetroStore FetchAppsNano(0,%d) failed or empty", APP_LIST_MAX);
    splash_set_status("RetroStore fetch failed");
    vTaskDelay(pdMS_TO_TICKS(2500));
    return;
  }

  ESP_LOGI(TAG, "RetroStore returned %u apps:", (unsigned)apps.size());
  for (size_t i = 0; i < apps.size(); i++) {
    const auto &a = apps[i];
    ESP_LOGI(TAG, "  [%2u] %s -- %s (%d) model=%d v%s",
             (unsigned)i, a.name.c_str(), a.author.c_str(),
             a.release_year, (int)a.model, a.version.c_str());
  }

  int n = (int)apps.size();
  if (n > APP_LIST_MAX) n = APP_LIST_MAX;
  const char *items[APP_LIST_MAX];
  for (int i = 0; i < n; i++) {
    snprintf(g_app_titles[i], sizeof(g_app_titles[i]), "%s", apps[i].name.c_str());
    items[i] = g_app_titles[i];
  }

  int sel = 0;
  splash_set_status("Select a game (UP/DOWN, ENTER):");
  splash_show_list(items, n, sel);

  while (true) {
    char ch = bt_keyboard.wait_for_ascii_char(true);
    if (ch == K_DOWN) {
      if (sel < n - 1) { sel++; splash_set_list_selection(sel); }
    } else if (ch == K_UP) {
      if (sel > 0)     { sel--; splash_set_list_selection(sel); }
    } else if (ch == K_ENTER) {
      show_app_result_t r = show_app_details(rs, apps[sel].id);
      if (r == SHOW_APP_LAUNCH) {
        // Game image is staged in g_launch_cmd_*; let the caller dismiss
        // the splash and hand off to z80_task.
        return;
      }
      // Drain any stale key events (e.g. the release of the ESC the user
      // hit to leave the details screen) before resuming the picker.
      drain_bt_events();
      splash_set_status("Select a game (UP/DOWN, ENTER):");
      splash_show_list(items, n, sel);
    } else if (ch == K_ESC) {
      break;
    }
  }

  splash_hide_list();
}

void keyb_task(void* arg) {
  esp_err_t ret;

  // To test the Pairing code entry, uncomment the following line as pairing info is
  // kept in the nvs. Pairing will then be required on every boot.
  // ESP_ERROR_CHECK(nvs_flash_erase());

  ret = nvs_flash_init();
  if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  if (bt_keyboard.setup(pairing_handler, keyboard_connected_handler,
                        keyboard_lost_connection_handler)) { // Must be called once

#if CONFIG_TRASHBOY_BT_SCAN_ENABLED
    // Try to auto-connect to previously paired keyboard, retry continuously if paired but not connected
    while (!bt_keyboard.is_connected()) {
      bt_keyboard.auto_connect_bonded_device();

      // Wait a bit to see if connection establishes
      vTaskDelay(pdMS_TO_TICKS(2000));

      // If still not connected after auto-connect attempt
      if (!bt_keyboard.is_connected()) {
        // Check if we have bonded devices by attempting another auto-connect
        // (auto_connect_bonded_device returns early if no bonded devices exist)
        bt_keyboard.auto_connect_bonded_device();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // If still not connected, it means either no bonded device exists
        // or the bonded device is not available. Try scanning for new devices.
        if (!bt_keyboard.is_connected()) {
          ESP_LOGI(TAG, "Scanning for keyboards to pair...");
          splash_set_status("Scanning for Bluetooth keyboard...");
          bt_keyboard.devices_scan(); // Required to discover new keyboards and for pairing
                                      // Default duration is 5 seconds
          vTaskDelay(pdMS_TO_TICKS(5000));
        }
      }
    }
#else
    // Developer toggle (CONFIG_TRASHBOY_BT_SCAN_ENABLED=n): skip the
    // connect/scan loop. Make one best-effort auto-connect attempt so a
    // paired keyboard still works if present, then continue regardless.
    ESP_LOGW(TAG, "Bluetooth keyboard scanning disabled "
                  "(CONFIG_TRASHBOY_BT_SCAN_ENABLED=n)");
    bt_keyboard.auto_connect_bonded_device();
    vTaskDelay(pdMS_TO_TICKS(1000));
#endif

    splash_set_paired();

#if CONFIG_TRASHBOY_BT_SCAN_ENABLED
    // Wait for the user to press ENTER (HID 0x28) or keypad ENTER (0x58)
    // to dismiss the splash and let the TRS-80 boot.
    while (true) {
      BTKeyboard::KeyInfo inf;
      if (!bt_keyboard.wait_for_low_event(inf, pdMS_TO_TICKS(100))) continue;
      if (key_report_contains(inf, 0x28) || key_report_contains(inf, 0x58)) break;
    }
#else
    // Without a keyboard to press ENTER, just give the splash a moment of
    // visibility, then proceed automatically.
    vTaskDelay(pdMS_TO_TICKS(1500));
#endif

    // Drive the splash through Wi-Fi setup (scan -> pick -> password ->
    // connect, or auto-connect from NVS-stored credentials).
    run_wifi_setup();
    vTaskDelay(pdMS_TO_TICKS(800));  // brief pause so the user sees "Connected"

    // Browse the RetroStore catalog: list, select with arrows + ENTER,
    // ENTER opens details (any key returns), ESC continues to TRS-80 boot.
    run_retrostore_browse();

    // Hand the LVGL teardown off to the pump task so it happens on the
    // same core that drives lv_timer_handler.
    splash_dismiss_requested = true;
    while (!splash_dismissed) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (true) {
      vTaskDelay(pdMS_TO_TICKS(5));
      BTKeyboard::KeyInfo inf;

      bt_keyboard.wait_for_low_event(inf);
      if ((inf.size == 4 && inf.keys[2] == 2    /* F5 */) ||
          (inf.size == 8 && inf.keys[2] == 0x3e /* F5 on Rii */)) {
        z80_pause();
        configure_pocket_trs();
        z80_resume();
      } else if (inf.size > 4 && inf.keys[0] == 5 && (inf.keys[1] == 0x4c || inf.keys[2] == 0x4c) /* Ctrl+Alt+Del */) {
        do_z80_reset = true;
      } else {
        process_key(inf);
      }
    }
  }
}

void ui_task(void *arg)
{

  while (1) {
    trs_screen.render();
    vTaskDelay(pdMS_TO_TICKS(5));
    lv_timer_handler();
  }
}

void z80_task(void *arg)
{
  init_settings();
  init_storage();
  init_events();
  init_trs_io();
  init_trs_fs_posix();
  // init_wifi() (from trs-io) is intentionally NOT called: it auto-connects
  // from its own NVS keys and starts the web-config AP if no creds are
  // stored. We drive the whole Wi-Fi lifecycle from the splash via
  // wifi_manager so the user can pick a network with the BT keyboard.
  init_trs_lib();
  init_sound();

  // Wait for the splash screen to be dismissed (keyboard paired and ENTER pressed)
  // before bringing up the TRS-80 screen, otherwise trs_screen.init() would create
  // a full-screen canvas that hides the splash.
  while (!splash_dismissed) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  // Hand the display back to the TRS-80 emulator's coordinate system:
  // TRSCanvas does its own manual rotation of pixels into an unrotated
  // 480x640 native canvas, so LVGL must be at ROTATION_0 here (otherwise
  // trs_screen.init() reads the wrong horizontal/vertical resolution and
  // we get a double-rotated picture). The flush callback adapts based on
  // the current rotation and just blits straight through when it's 0.
  lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_0);

  trs_screen.init();
  trs_screen.push(new ScreenBuffer(MODE_TEXT_64x16));
  mem_init();
  z80_reset();

  // If the splash flow downloaded a CMD via the RetroStore browser, load
  // it over the freshly-reset Z80 memory and jump straight into it.
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

  xTaskCreatePinnedToCore(ui_task, "ui_task", 6000, NULL, 5, NULL, 1);

  while (1) {
    if (do_z80_reset) {
      z80_reset();
      do_z80_reset = false;
    }
    z80_run();
  }
}

// Set to 1 to bring back the lvgl_pump_task diagnostics — used to tell
// from serial whether a render hang is inside LVGL (we'd see "before"
// but never "after") and whether the pump loop is still iterating.
// Off by default since it's chatty.
#define LVGL_PUMP_DEBUG_LOGS 0

void lvgl_pump_task(void *arg)
{
  // Drives LVGL while the splash is up, and tears it down on this same task
  // so all LVGL calls stay on a single core. Exits once dismissed; ui_task
  // takes over driving lv_timer_handler from then on.
#if LVGL_PUMP_DEBUG_LOGS
  uint32_t iter = 0;
  uint32_t last_heartbeat_ms = 0;
#endif
  while (!splash_dismiss_requested) {
    splash_tick();
#if LVGL_PUMP_DEBUG_LOGS
    if (iter < 5 || (iter & 0xFFF) == 0) {
      ESP_LOGI(TAG, "lv_timer_handler before (iter=%lu)", (unsigned long) iter);
    }
#endif
    lv_timer_handler();
#if LVGL_PUMP_DEBUG_LOGS
    if (iter < 5 || (iter & 0xFFF) == 0) {
      ESP_LOGI(TAG, "lv_timer_handler after  (iter=%lu)", (unsigned long) iter);
    }
    iter++;
    uint32_t now = (uint32_t) (xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (now - last_heartbeat_ms >= 2000) {
      ESP_LOGI(TAG, "lvgl_pump alive: iter=%lu", (unsigned long) iter);
      last_heartbeat_ms = now;
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  splash_dismiss();
  splash_dismissed = true;
  vTaskDelete(NULL);
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
  // Initialize I2C (required by EXIO)
  I2C_Init();

  // Initialize EXIO (required by LCD)
  EXIO_Init();

  // Initialize LCD
  LCD_Init();

  // Initialize LVGL
  LVGL_Init();

  splash_init();

  // 8 KB stack: the LVGL render pipeline with widget transforms (rotation +
  // scale) recurses deep enough to overflow the typical 4 KB stack when
  // many transformed labels are present, which silently wedges the task.
  xTaskCreatePinnedToCore(lvgl_pump_task, "lvgl_pump", 8192, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(keyb_task, "keyb_task", 6000, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(heap_diag_task, "heap_diag", 3072, NULL, 1, NULL, 0);
  z80_task(NULL);
}
