// What the emulator core (components/ptrs: z80.cpp, trs.cpp, trs_memory.cpp,
// trs-keyboard.cpp) needs from the rest of the firmware, for a host: a
// screen that draws nothing, the Model III's I/O ports without TRS-IO, a
// clock that never waits, and NVS in memory.

#include "host.h"

#include "trs_screen.h"
#include "io.h"
#include "settings.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include <string.h>
#include <map>
#include <string>

//----------------------------------------------------------------
// The clock. vTaskDelay() is how trs.cpp paces the Z80 against real time.

static int64_t clock_us = 0;

int64_t esp_timer_get_time(void)
{
  return clock_us;
}

void vTaskDelay(TickType_t ticks)
{
  clock_us += (int64_t) ticks * portTICK_PERIOD_MS * 1000;
}

//----------------------------------------------------------------
// trs_screen.h. Like the firmware's screen, this one keeps the characters:
// mem_write() hands them over and mem_read() asks for them. Nothing is
// drawn.

TRSScreen trs_screen;

// The Model 4's 80x24 is the largest
static uint8_t characters[80 * 24];

TRSScreen::TRSScreen() {}
void TRSScreen::setMode(uint8_t mode) {}
void TRSScreen::setInverse(int flag) {}
void TRSScreen::refresh() {}

void TRSScreen::drawChar(uint16_t pos, uint8_t character)
{
  if (pos < sizeof(characters)) {
    characters[pos] = character;
  }
}

bool TRSScreen::getChar(uint16_t pos, uint8_t& character)
{
  if (pos < sizeof(characters)) {
    character = characters[pos];
    return true;
  }
  return false;
}

//----------------------------------------------------------------
// io.h: the ports of io.cpp that the ROM uses, with TRS-IO and FreHD
// absent. Keep the values the same as there.

static uint8_t port_0xe0 = 0b11110011;
static uint8_t port_0xec = 0xff;

void z80_out(uint8_t address, uint8_t data, tstate_t z80_state_t_count)
{
  if (address == 0xec) {
    port_0xec = data;
  }
}

uint8_t z80_in(uint8_t address, tstate_t z80_state_t_count)
{
  switch (address) {
  case 0xec:
    return port_0xec;
  case 0xe0:
    // Bit 2 is 0: the timer interrupt
    return port_0xe0;
  }
  return 0xff;
}

//----------------------------------------------------------------
// settings.h and NVS

// (settings.cpp, which has this, also sets up the screen's settings)
nvs_handle SettingsBase::storage;

static std::map<std::string, int> nvs;

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { nvs.clear(); return ESP_OK; }
esp_err_t nvs_open(const char* name, nvs_open_mode mode, nvs_handle* handle) { *handle = 1; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle handle) { return ESP_OK; }
esp_err_t nvs_erase_all(nvs_handle handle) { nvs.clear(); return ESP_OK; }

static esp_err_t nvs_get(const char* key, int* value)
{
  auto entry = nvs.find(key);
  if (entry == nvs.end()) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  *value = entry->second;
  return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle handle, const char* key, uint8_t* value)
{
  int v;
  esp_err_t err = nvs_get(key, &v);
  if (err == ESP_OK) {
    *value = v;
  }
  return err;
}

esp_err_t nvs_get_i8(nvs_handle handle, const char* key, int8_t* value)
{
  int v;
  esp_err_t err = nvs_get(key, &v);
  if (err == ESP_OK) {
    *value = v;
  }
  return err;
}

esp_err_t nvs_set_u8(nvs_handle handle, const char* key, uint8_t value) { nvs[key] = value; return ESP_OK; }
esp_err_t nvs_set_i8(nvs_handle handle, const char* key, int8_t value) { nvs[key] = value; return ESP_OK; }

//----------------------------------------------------------------

void host_clear_screen()
{
  memset(characters, 0, sizeof(characters));
}

void host_init()
{
  SettingsBase::init();
  // The ROM of a new device: none is stored
  settingsROM.init();
}
