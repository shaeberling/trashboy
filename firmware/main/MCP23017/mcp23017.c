#include "mcp23017.h"

#include "I2C_Driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp23017";

// Register map with BANK=0 (power-on default).
#define REG_IODIRA 0x00  // 1 = input, 0 = output
#define REG_IODIRB 0x01
#define REG_GPPUA  0x0C  // 1 = 100k pull-up enabled
#define REG_GPPUB  0x0D
#define REG_GPIOA  0x12  // read port-A pin state
#define REG_GPIOB  0x13

// Poll cadence. Also acts as the debounce window: a contact bounce that
// settles within one interval never shows up as more than one edge.
#define POLL_INTERVAL_MS 20

// Latest snapshot of port A / B pin state, cached by the poll task after
// each successful read. Consumers (e.g. input-test UI) can read this
// without doing their own I2C traffic. Aligned uint8_t stores are atomic
// on the S3 — no lock needed.
static volatile uint8_t s_last_a = 0xFF;
static volatile uint8_t s_last_b = 0xFF;

static mcp23017_change_cb_t s_on_change = NULL;

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
  uint8_t v = 0xFF;
  esp_err_t err;
  const struct { uint8_t reg; const char *name; } writes[] = {
    { REG_IODIRA, "IODIRA" },
    { REG_IODIRB, "IODIRB" },
    { REG_GPPUA,  "GPPUA"  },
    { REG_GPPUB,  "GPPUB"  },
  };
  for (size_t i = 0; i < sizeof(writes) / sizeof(writes[0]); i++) {
    err = I2C_Write(MCP23017_I2C_ADDR, writes[i].reg, &v, 1);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "%s write failed: %s", writes[i].name, esp_err_to_name(err));
      return err;
    }
  }
  ESP_LOGI(TAG, "configured: ports A+B inputs + pull-ups");
  return ESP_OK;
}

esp_err_t mcp23017_read_a(uint8_t *out) {
  return I2C_Read(MCP23017_I2C_ADDR, REG_GPIOA, out, 1);
}

esp_err_t mcp23017_read_b(uint8_t *out) {
  return I2C_Read(MCP23017_I2C_ADDR, REG_GPIOB, out, 1);
}

void mcp23017_get_ports(uint8_t *port_a, uint8_t *port_b) {
  if (port_a) *port_a = s_last_a;
  if (port_b) *port_b = s_last_b;
}

// Poll both ports on a fixed cadence and log press/release edges. We poll
// rather than use the chip's INTA/INTB pins because on this board (Waveshare
// ESP32-S3-Touch-LCD-2.8B) there is no free, exposed, non-strapping GPIO to
// receive the interrupt: 33-37 are consumed by the octal PSRAM, 16 is the
// GT911 touch INT, and 4 drives the emulator's SDM audio. Polling over the
// shared I2C bus costs no GPIO at all.
static void button_poll_task(void *arg) {
  (void) arg;
  uint8_t prev_a = 0xFF, prev_b = 0xFF;
  // Establish baseline before reporting edges.
  mcp23017_read_a(&prev_a);
  mcp23017_read_b(&prev_b);
  s_last_a = prev_a;
  s_last_b = prev_b;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

    uint8_t cur_a = 0xFF, cur_b = 0xFF;
    esp_err_t err_a = mcp23017_read_a(&cur_a);
    esp_err_t err_b = mcp23017_read_b(&cur_b);
    if (err_a != ESP_OK || err_b != ESP_OK) {
      ESP_LOGW(TAG, "read GPIOA/B failed: %s / %s",
               esp_err_to_name(err_a), esp_err_to_name(err_b));
      continue;
    }

    uint8_t changed_a = cur_a ^ prev_a;
    uint8_t changed_b = cur_b ^ prev_b;
    if (changed_a || changed_b) {
      for (int i = 0; i < 8; i++) {
        uint8_t mask = 1u << i;
        if (changed_a & mask) {
          ESP_LOGI(TAG, "BTN A%d %s", i, (cur_a & mask) ? "released" : "pressed");
        }
        if (changed_b & mask) {
          ESP_LOGI(TAG, "BTN B%d %s", i, (cur_b & mask) ? "released" : "pressed");
        }
      }
      if (s_on_change) {
        s_on_change(cur_a, cur_b);
      }
    }

    prev_a = cur_a;
    prev_b = cur_b;
    s_last_a = cur_a;
    s_last_b = cur_b;
  }
}

bool mcp23017_start_button_task(mcp23017_change_cb_t on_change) {
  s_on_change = on_change;
  BaseType_t ok = xTaskCreatePinnedToCore(button_poll_task, "mcp_btn",
                                          3072, NULL, 4, NULL, 0);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "button_poll_task create failed");
    return false;
  }
  ESP_LOGI(TAG, "polling ports A+B every %d ms", POLL_INTERVAL_MS);
  return true;
}
