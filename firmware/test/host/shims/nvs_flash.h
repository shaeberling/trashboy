// Host stand-in for NVS: a table in memory (host.cpp).
#pragma once

#include <stdint.h>
#include <assert.h>

typedef int esp_err_t;
typedef uint32_t nvs_handle;
typedef nvs_handle nvs_handle_t;

#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERROR_CHECK(x) do { esp_err_t err_ = (x); assert(err_ == ESP_OK); (void) err_; } while (0)

typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode;

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_open(const char* name, nvs_open_mode mode, nvs_handle* handle);
esp_err_t nvs_get_u8(nvs_handle handle, const char* key, uint8_t* value);
esp_err_t nvs_set_u8(nvs_handle handle, const char* key, uint8_t value);
esp_err_t nvs_get_i8(nvs_handle handle, const char* key, int8_t* value);
esp_err_t nvs_set_i8(nvs_handle handle, const char* key, int8_t value);
esp_err_t nvs_commit(nvs_handle handle);
esp_err_t nvs_erase_all(nvs_handle handle);
