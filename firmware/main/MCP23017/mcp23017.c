#include "mcp23017.h"

#include "I2C_Driver.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "mcp23017";

// Register map with BANK=0 (power-on default).
#define REG_IODIRA   0x00  // 1 = input, 0 = output
#define REG_IODIRB   0x01
#define REG_GPINTENA 0x04  // 1 = interrupt-on-change enabled for this pin
#define REG_GPINTENB 0x05
#define REG_INTCONA  0x08  // 0 = compare against previous, 1 = compare DEFVAL
#define REG_INTCONB  0x09
#define REG_IOCON    0x0A  // global config
#define REG_GPPUA    0x0C  // 1 = 100k pull-up enabled
#define REG_GPPUB    0x0D
#define REG_GPIOA    0x12  // read port-A pin state (also clears INT)
#define REG_GPIOB    0x13

static SemaphoreHandle_t s_int_sem;
static int s_int_gpio = -1;

// Latest snapshot of port A / B pin state, cached by the button task
// after each successful read. Consumers (e.g. input-test UI) can poll
// this without doing their own I2C traffic. Aligned uint8_t stores are
// atomic on the S3 — no lock needed.
static volatile uint8_t s_last_a = 0xFF;
static volatile uint8_t s_last_b = 0xFF;

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

// IOCON=0x40 → BANK=0, MIRROR=1 (INTA/INTB OR'd together so one wire from
// INTA to the ESP32 covers events on both ports), SEQOP=0, ODR=0
// (push-pull), INTPOL=0 (active-low). INTCONx=0x00 → interrupt on any
// change. GPINTENx=0xFF → enable interrupt-on-change on every pin of
// both ports.
static esp_err_t mcp23017_configure_interrupts(void) {
  struct { uint8_t reg; uint8_t val; const char *name; } writes[] = {
    { REG_IOCON,    0x40, "IOCON"    },
    { REG_INTCONA,  0x00, "INTCONA"  },
    { REG_INTCONB,  0x00, "INTCONB"  },
    { REG_GPINTENA, 0xFF, "GPINTENA" },
    { REG_GPINTENB, 0xFF, "GPINTENB" },
  };
  for (size_t i = 0; i < sizeof(writes) / sizeof(writes[0]); i++) {
    esp_err_t err = I2C_Write(MCP23017_I2C_ADDR, writes[i].reg, &writes[i].val, 1);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "%s write failed: %s", writes[i].name, esp_err_to_name(err));
      return err;
    }
  }
  return ESP_OK;
}

static void IRAM_ATTR mcp23017_int_isr(void *arg) {
  (void) arg;
  BaseType_t hpw = pdFALSE;
  xSemaphoreGiveFromISR(s_int_sem, &hpw);
  if (hpw == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

static void button_task(void *arg) {
  (void) arg;
  uint8_t prev_a = 0xFF, prev_b = 0xFF;
  // Establish baseline and clear any latched chip-side INT from boot.
  mcp23017_read_a(&prev_a);
  mcp23017_read_b(&prev_b);
  s_last_a = prev_a;
  s_last_b = prev_b;
  for (;;) {
    if (xSemaphoreTake(s_int_sem, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    // Let contact bounce settle so we don't log a flurry of transitions
    // for a single press.
    vTaskDelay(pdMS_TO_TICKS(10));
    // Drain loop: if a new change happens between our read and the chip
    // releasing INT, no fresh NEGEDGE will fire on the ESP32 side (INT
    // was already low), and we'd be stuck waiting forever. Keep reading
    // until the chip's INT pin actually returns high.
    int iters = 0;
    do {
      uint8_t cur_a = 0xFF, cur_b = 0xFF;
      esp_err_t err_a = mcp23017_read_a(&cur_a);
      esp_err_t err_b = mcp23017_read_b(&cur_b);
      if (err_a != ESP_OK || err_b != ESP_OK) {
        ESP_LOGW(TAG, "read GPIOA/B failed: %s / %s",
                 esp_err_to_name(err_a), esp_err_to_name(err_b));
        break;
      }
      uint8_t changed_a = cur_a ^ prev_a;
      uint8_t changed_b = cur_b ^ prev_b;
      for (int i = 0; i < 8; i++) {
        uint8_t mask = 1u << i;
        if (changed_a & mask) {
          ESP_LOGI(TAG, "BTN A%d %s", i, (cur_a & mask) ? "released" : "pressed");
        }
        if (changed_b & mask) {
          ESP_LOGI(TAG, "BTN B%d %s", i, (cur_b & mask) ? "released" : "pressed");
        }
      }
      prev_a = cur_a;
      prev_b = cur_b;
      s_last_a = cur_a;
      s_last_b = cur_b;
      if (++iters > 16) {
        ESP_LOGW(TAG, "INT (GPIO%d) stuck low after 16 reads — wiring fault?",
                 s_int_gpio);
        break;
      }
    } while (gpio_get_level((gpio_num_t) s_int_gpio) == 0);
  }
}

bool mcp23017_start_button_task(int int_gpio) {
  if (mcp23017_configure_interrupts() != ESP_OK) {
    return false;
  }
  s_int_gpio = int_gpio;
  s_int_sem = xSemaphoreCreateBinary();
  if (!s_int_sem) {
    ESP_LOGE(TAG, "semaphore create failed");
    return false;
  }
  gpio_config_t cfg = {
    .pin_bit_mask = 1ULL << int_gpio,
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE,
  };
  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "gpio_config(GPIO%d) failed: %s", int_gpio, esp_err_to_name(err));
    return false;
  }
  // ISR service is process-global; ignore ALREADY_INSTALLED if anything
  // else (e.g. touch driver) has already installed it.
  err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
    return false;
  }
  err = gpio_isr_handler_add((gpio_num_t) int_gpio, mcp23017_int_isr, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "gpio_isr_handler_add(GPIO%d) failed: %s",
             int_gpio, esp_err_to_name(err));
    return false;
  }
  BaseType_t ok = xTaskCreatePinnedToCore(button_task, "mcp_btn",
                                          3072, NULL, 4, NULL, 0);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "button_task create failed");
    return false;
  }
  ESP_LOGI(TAG, "INTA wired to GPIO%d", int_gpio);
  return true;
}
