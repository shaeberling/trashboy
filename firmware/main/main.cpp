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
          bt_keyboard.devices_scan(); // Required to discover new keyboards and for pairing
                                      // Default duration is 5 seconds
          vTaskDelay(pdMS_TO_TICKS(5000));
        }
      }
    }
    
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(5));
      BTKeyboard::KeyInfo inf;

      bt_keyboard.wait_for_low_event(inf);
      if (inf.size == 4 && inf.keys[2] == 2 /* F5 */) {
        z80_pause();
        configure_pocket_trs();
        z80_resume();
      } else if (inf.size > 4 && inf.keys[0] == 5 && inf.keys[1] == 0x4c /* Ctrl+Alt+Del */) {
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
  init_wifi();
  init_trs_lib();

  trs_screen.init();
  trs_screen.push(new ScreenBuffer(MODE_TEXT_64x16));
  mem_init();
  z80_reset();

  xTaskCreatePinnedToCore(ui_task, "ui_task", 6000, NULL, 5, NULL, 1);

  while (1) {
    if (do_z80_reset) {
      z80_reset();
      do_z80_reset = false;
    }
    z80_run();
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
    
  xTaskCreatePinnedToCore(keyb_task, "keyb_task", 6000, NULL, 5, NULL, 0);
  z80_task(NULL);
}
