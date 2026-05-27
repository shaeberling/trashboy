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
#define REG_GPINTENA 0x04  // 1 = interrupt-on-change enabled for this pin
#define REG_INTCONA  0x08  // 0 = compare against previous, 1 = compare DEFVAL
#define REG_IOCON    0x0A  // global config
#define REG_GPPUA    0x0C  // 1 = 100k pull-up enabled
#define REG_GPIOA    0x12  // read port-A pin state (also clears INT)

static SemaphoreHandle_t s_int_sem;
static int s_int_gpio = -1;

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

// IOCON=0x00 → BANK=0, MIRROR=0, SEQOP=0, ODR=0 (push-pull), INTPOL=0
// (active-low INTA). INTCONA=0x00 → interrupt on any change. GPINTENA=0xFF
// → enable interrupt-on-change on all port-A pins.
static esp_err_t mcp23017_configure_interrupts(void) {
  uint8_t v;
  v = 0x00;
  esp_err_t err = I2C_Write(MCP23017_I2C_ADDR, REG_IOCON, &v, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "IOCON write failed: %s", esp_err_to_name(err));
    return err;
  }
  v = 0x00;
  err = I2C_Write(MCP23017_I2C_ADDR, REG_INTCONA, &v, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "INTCONA write failed: %s", esp_err_to_name(err));
    return err;
  }
  v = 0xFF;
  err = I2C_Write(MCP23017_I2C_ADDR, REG_GPINTENA, &v, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GPINTENA write failed: %s", esp_err_to_name(err));
    return err;
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
  uint8_t prev = 0xFF;
  // Establish baseline and clear any latched chip-side INT from boot.
  mcp23017_read_a(&prev);
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
      uint8_t cur = 0xFF;
      esp_err_t err = mcp23017_read_a(&cur);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "read GPIOA in IRQ path failed: %s", esp_err_to_name(err));
        break;
      }
      uint8_t changed = cur ^ prev;
      if (changed & 0x01) {
        ESP_LOGI(TAG, "BTN1 %s", (cur & 0x01) ? "released" : "pressed");
      }
      if (changed & 0x02) {
        ESP_LOGI(TAG, "BTN2 %s", (cur & 0x02) ? "released" : "pressed");
      }
      prev = cur;
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
