#include "mcp23017.h"

#include "I2C_Driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp23017";

// Register map with BANK=0 (power-on default).
#define REG_IODIRA 0x00  // 1 = input, 0 = output
#define REG_GPPUA  0x0C  // 1 = 100k pull-up enabled
#define REG_GPIOA  0x12  // read = port-A pin state

esp_err_t mcp23017_probe(void) {
  uint8_t iodira = 0;
  esp_err_t err = I2C_Read(MCP23017_I2C_ADDR, REG_IODIRA, &iodira, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "probe failed: no response at 0x%02X (%s)",
             MCP23017_I2C_ADDR, esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(TAG, "found at 0x%02X — IODIRA=0x%02X (expect 0xFF at power-on)",
           MCP23017_I2C_ADDR, iodira);
  return ESP_OK;
}

esp_err_t mcp23017_init(void) {
  uint8_t v;
  v = 0xFF;
  esp_err_t err = I2C_Write(MCP23017_I2C_ADDR, REG_IODIRA, &v, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "IODIRA write failed: %s", esp_err_to_name(err));
    return err;
  }
  v = 0xFF;
  err = I2C_Write(MCP23017_I2C_ADDR, REG_GPPUA, &v, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GPPUA write failed: %s", esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(TAG, "configured: port A inputs + pull-ups");
  return ESP_OK;
}

esp_err_t mcp23017_read_a(uint8_t *out) {
  return I2C_Read(MCP23017_I2C_ADDR, REG_GPIOA, out, 1);
}

static void button_poll_task(void *arg) {
  (void) arg;
  uint8_t prev = 0xFF;  // all released at boot (pull-ups → 1)
  for (;;) {
    uint8_t cur = 0xFF;
    esp_err_t err = mcp23017_read_a(&cur);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "read GPIOA failed: %s", esp_err_to_name(err));
    } else if (cur != prev) {
      uint8_t changed = cur ^ prev;
      if (changed & 0x01) {
        ESP_LOGI(TAG, "BTN1 %s", (cur & 0x01) ? "released" : "pressed");
      }
      if (changed & 0x02) {
        ESP_LOGI(TAG, "BTN2 %s", (cur & 0x02) ? "released" : "pressed");
      }
      prev = cur;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

bool mcp23017_start_button_task(void) {
  BaseType_t ok = xTaskCreatePinnedToCore(button_poll_task, "mcp_btn",
                                          3072, NULL, 4, NULL, 0);
  return ok == pdPASS;
}
