// See watcher_board.h.
//
// Pins, the expander's bit assignments and the bring-up order follow Seeed's
// sensecap-watcher BSP (SenseCAP-Watcher-Firmware) and the Watcher board file
// of muse-gadget-sdk (esp32/components/muse/boards/board_sensecap_watcher.c,
// Apache-2.0), which is where the notes on the SPD2010's quirks come from.

#include "watcher_board.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_spd2010.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "watcher";

#define LCD_HOST SPI3_HOST
#define LCD_PCLK GPIO_NUM_7
#define LCD_D0 GPIO_NUM_9
#define LCD_D1 GPIO_NUM_1
#define LCD_D2 GPIO_NUM_14
#define LCD_D3 GPIO_NUM_13
#define LCD_CS GPIO_NUM_45
#define LCD_BL GPIO_NUM_8
#define LCD_PCLK_HZ (40 * 1000 * 1000)
// Largest single SPI transfer; bigger blocks are sent as several, back to
// back. Eight full-width rows.
#define LCD_CHUNK_BYTES (WATCHER_LCD_RES * 8 * 2)

#define TP_SDA GPIO_NUM_39     // the panel's touch controller, unused here
#define TP_SCL GPIO_NUM_38

#define I2C_SDA GPIO_NUM_47
#define I2C_SCL GPIO_NUM_48

#define KNOB_A GPIO_NUM_41
#define KNOB_B GPIO_NUM_42

// PCA9535 at 0x21: port 0 in the low byte, port 1 in the high byte.
#define EXP_ADDR 0x21
#define EXP_REG_INPUT 0x00
#define EXP_REG_OUTPUT 0x02
#define EXP_REG_CONFIG 0x06        // 1 = input
#define EXP_INPUTS 0x20FF          // port 0 and BAT_DET (13)
#define EXP_WHEEL BIT(3)           // low while pressed
#define EXP_PWR_LCD BIT(9)
#define EXP_PWR_SYSTEM BIT(10)     // holds the board on from battery

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_exp;
static uint16_t s_exp_out;
static pcnt_unit_handle_t s_knob;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_sent;   // given when a block of pixels has gone out

static esp_err_t exp_write(uint8_t reg, uint16_t v)
{
    const uint8_t buf[] = { reg, v & 0xff, v >> 8 };
    return i2c_master_transmit(s_exp, buf, sizeof(buf), 50);
}

static esp_err_t exp_read(uint16_t *v)
{
    const uint8_t reg = EXP_REG_INPUT;
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_exp, &reg, 1, buf, sizeof(buf), 50), TAG, "expander read");
    *v = buf[0] | buf[1] << 8;
    return ESP_OK;
}

static esp_err_t exp_set(uint16_t mask, bool on)
{
    s_exp_out = on ? s_exp_out | mask : s_exp_out & ~mask;
    return exp_write(EXP_REG_OUTPUT, s_exp_out);
}

static esp_err_t knob_init(void)
{
    const pcnt_unit_config_t unit_cfg = { .low_limit = -100, .high_limit = 100 };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_cfg, &s_knob), TAG, "pcnt");
    const pcnt_glitch_filter_config_t filter = { .max_glitch_ns = 1000 };
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_knob, &filter), TAG, "pcnt filter");
    // Full quadrature: count both edges of both lines. A leading B counts down,
    // a leading A up — "left" and "right" in Seeed's firmware.
    pcnt_channel_handle_t a, b;
    const pcnt_chan_config_t a_cfg = { .edge_gpio_num = KNOB_A, .level_gpio_num = KNOB_B };
    const pcnt_chan_config_t b_cfg = { .edge_gpio_num = KNOB_B, .level_gpio_num = KNOB_A };
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_knob, &a_cfg, &a), TAG, "pcnt a");
    ESP_RETURN_ON_ERROR(pcnt_new_channel(s_knob, &b_cfg, &b), TAG, "pcnt b");
    pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    gpio_pullup_en(KNOB_A);
    gpio_pullup_en(KNOB_B);
    ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_knob), TAG, "pcnt enable");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_knob), TAG, "pcnt clear");
    return pcnt_unit_start(s_knob);
}

esp_err_t watcher_board_init(void)
{
    // Hold the LCD and touch lines low until the LCD rail is up, as Seeed's BSP does.
    const gpio_config_t lcd_pins = {
        .pin_bit_mask = BIT64(TP_SDA) | BIT64(TP_SCL) | BIT64(LCD_PCLK) | BIT64(LCD_D0) | BIT64(LCD_D1) |
                        BIT64(LCD_D2) | BIT64(LCD_D3) | BIT64(LCD_CS) | BIT64(LCD_BL),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&lcd_pins), TAG, "lcd pins");
    for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
        if (lcd_pins.pin_bit_mask & BIT64(pin)) {
            gpio_set_level(pin, 0);
        }
    }

    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c), TAG, "i2c");
    const i2c_device_config_t exp_cfg = { .device_address = EXP_ADDR, .scl_speed_hz = 400000 };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &exp_cfg, &s_exp), TAG, "expander");

    // Outputs start low, then the system rail, then the LCD's (Seeed's order).
    // Every other rail (camera, audio amplifier, SD, Grove, battery ADC) stays off.
    ESP_RETURN_ON_ERROR(exp_write(EXP_REG_OUTPUT, 0), TAG, "expander outputs");
    ESP_RETURN_ON_ERROR(exp_write(EXP_REG_CONFIG, EXP_INPUTS), TAG, "expander config");
    uint16_t in;
    ESP_RETURN_ON_ERROR(exp_read(&in), TAG, "expander inputs");
    ESP_LOGI(TAG, "expander inputs 0x%04x", in);
    ESP_RETURN_ON_ERROR(exp_set(EXP_PWR_SYSTEM, true), TAG, "system rail");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(exp_set(EXP_PWR_LCD, true), TAG, "lcd rail");
    vTaskDelay(pdMS_TO_TICKS(50));

    // The touch controller's bus is not used: stop driving it.
    gpio_reset_pin(TP_SDA);
    gpio_reset_pin(TP_SCL);

    return knob_init();
}

int watcher_knob_read(void)
{
    int n = 0;
    if (pcnt_unit_get_count(s_knob, &n) == ESP_OK && n) {
        pcnt_unit_clear_count(s_knob);
    }
    return n;
}

bool watcher_button_pressed(void)
{
    uint16_t in;
    return exp_read(&in) == ESP_OK && !(in & EXP_WHEEL);
}

static bool IRAM_ATTR on_pixels_sent(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_sent, &woken);
    return woken == pdTRUE;
}

esp_err_t watcher_lcd_init(void)
{
    s_sent = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_sent, ESP_ERR_NO_MEM, TAG, "semaphore");

    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t bl_ch = {
        .gpio_num = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&bl_timer), TAG, "backlight timer");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&bl_ch), TAG, "backlight channel");

    const spi_bus_config_t bus =
        SPD2010_PANEL_BUS_QSPI_CONFIG(LCD_PCLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_CHUNK_BYTES);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");
    esp_lcd_panel_io_spi_config_t io_cfg = SPD2010_PANEL_IO_QSPI_CONFIG(LCD_CS, on_pixels_sent, NULL);
    io_cfg.pclk_hz = LCD_PCLK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io), TAG, "panel io");

    spd2010_vendor_config_t vendor_cfg = { .flags.use_qspi_interface = 1 };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_spd2010(s_io, &panel_cfg, &s_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

void watcher_lcd_backlight(int percent)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t watcher_lcd_draw(int x, int y, int w, int h, const void *pixels)
{
    // The transfer is queued and finishes in the background; the panel driver
    // reads `pixels` until then.
    xSemaphoreTake(s_sent, 0);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, pixels), TAG, "draw");
    return xSemaphoreTake(s_sent, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t watcher_lcd_clear(void)
{
    const int rows = LCD_CHUNK_BYTES / (WATCHER_LCD_RES * 2);
    void *black = heap_caps_calloc(1, LCD_CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(black, ESP_ERR_NO_MEM, TAG, "clear buffer");
    esp_err_t err = ESP_OK;
    for (int y = 0; y < WATCHER_LCD_RES && err == ESP_OK; y += rows) {
        const int h = WATCHER_LCD_RES - y < rows ? WATCHER_LCD_RES - y : rows;
        err = watcher_lcd_draw(0, y, WATCHER_LCD_RES, h, black);
    }
    heap_caps_free(black);
    return err;
}
