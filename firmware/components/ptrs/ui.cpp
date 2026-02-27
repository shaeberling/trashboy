
#include "ui.h"
//#include "calibrate.h"
#include "storage.h"
#include "trs_screen.h"
#include <freertos/task.h>
#include "bt_keyboard.hpp"

extern "C" {
  #include <trs-lib.h>
  #include "settings.h"
}

extern BTKeyboard bt_keyboard;

#define MENU_CONFIGURE 0
#define MENU_CALIBRATE 1
#define MENU_STATUS 2
#define MENU_RESET 3
#define MENU_HELP 4
#define MENU_EXIT 5

static menu_item_t main_menu_items[] = {
  {MENU_CONFIGURE, "Configure"},
  //{MENU_CALIBRATE, "Calibrate Screen"},
  {MENU_STATUS, "Status"},
  {MENU_RESET, "Reset Settings"},
  {MENU_HELP, "Help"},
  {MENU_EXIT, "Exit"}
};

MENU(main_menu, "TrashBoy");

static ScreenBuffer* screenBuffer;


static void screen_update(uint8_t* from, uint8_t* to)
{
  screenBuffer->update(from, to);
}

static char get_next_key()
{
  uint8_t ch = bt_keyboard.wait_for_ascii_char(true);

  switch(ch) {
  case 0x1b:
    return KEY_BREAK;
  case 0x98:
    return KEY_UP;
  case 0x97:
    return KEY_DOWN;
  case 0x95:
    return KEY_RIGHT;
  case 0x96:
    return KEY_LEFT;
  default:
    break;
  }
  return ch;
}

void configure_pocket_trs()
{
  bool show_from_left = false;
  bool exit = false;
  uint8_t mode = trs_screen.getMode();

  ScreenBuffer* orig = trs_screen.getTop();

  ScreenBuffer* backgroundBuffer = new ScreenBuffer(mode);
  trs_screen.push(backgroundBuffer);

  screenBuffer = new ScreenBuffer(mode);
  trs_screen.push(screenBuffer);

  set_screen(screenBuffer->getBuffer(), backgroundBuffer->getBuffer(),
	     screenBuffer->getWidth(), screenBuffer->getHeight());

  while (!exit) {
    uint8_t action = menu(&main_menu, show_from_left, true);
    switch (action) {
    case MENU_CONFIGURE:
      configure();
      break;
#if 0
    case MENU_CALIBRATE:
      calibrate();
      break;
#endif
    case MENU_STATUS:
      status();
      break;
    case MENU_RESET:
      bt_keyboard.remove_all_bonded_devices();
      vTaskDelay(pdMS_TO_TICKS(100));
      SettingsBase::reset();
      storage_erase();
      esp_restart();
      break;
    case MENU_HELP:
      help();
      break;
    case MENU_EXIT:
    case MENU_ABORT:
      exit = true;
      break;
    }
    show_from_left = true;
  }

  // Copy original screen content of the TRS emulation
  // to the background buffer
  backgroundBuffer->copyBufferFrom(orig);
  screen_show(true);
  trs_screen.pop();
  trs_screen.pop();
}

void init_trs_lib()
{
  set_screen_callback(screen_update);
  set_keyboard_callback(get_next_key);
}