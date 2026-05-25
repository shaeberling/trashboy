// Minimal GT911 reader. See Touch.h for the rationale.

#include "Touch.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "EXIO/TCA9554PWR.h"

static const char *TAG = "Touch";

#define GT911_I2C_PORT      I2C_NUM_0
#define GT911_I2C_ADDR_PRIMARY    0x5D     // selected when INT is low at RST release
#define GT911_I2C_ADDR_ALTERNATE  0x14     // selected when INT is high at RST release
#define GT911_INT_GPIO      GPIO_NUM_16
#define GT911_RST_EXIO      TCA9554_EXIO2

// Address detected during Touch_Init(). On the Waveshare 2.8B this comes
// out as 0x14 even when INT is driven low during the reset window; the
// board appears to hard-strap the alternate address.
static uint8_t s_gt911_addr = GT911_I2C_ADDR_PRIMARY;

#define GT911_NATIVE_X_MAX  480
#define GT911_NATIVE_Y_MAX  640

// GT911 register addresses (16-bit, big-endian on the wire).
#define GT911_REG_STATUS    0x814E   // bit7 = ready, bits3..0 = point count
#define GT911_REG_POINT1    0x8150   // 8 bytes per point: id,xL,xH,yL,yH,sizeL,sizeH,rsvd

#define I2C_TIMEOUT_MS      50

static esp_err_t gt911_read(uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t reg_be[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_write_read_device(
        GT911_I2C_PORT, s_gt911_addr,
        reg_be, sizeof(reg_be),
        buf, len,
        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t gt911_write_u8(uint16_t reg, uint8_t value)
{
    uint8_t buf[3] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
        value,
    };
    return i2c_master_write_to_device(
        GT911_I2C_PORT, s_gt911_addr,
        buf, sizeof(buf),
        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

// Probe the GT911 by reading its product id register (0x8140..0x8143,
// ASCII "911"). Returns ESP_OK if the chip ACKed and the id is valid.
static esp_err_t gt911_probe(uint8_t addr)
{
    uint8_t reg_be[2] = { 0x81, 0x40 };
    uint8_t prod[4] = {0};
    esp_err_t err = i2c_master_write_read_device(
        GT911_I2C_PORT, addr,
        reg_be, sizeof(reg_be),
        prod, sizeof(prod),
        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) return err;
    if (prod[0] != '9' || prod[1] != '1' || prod[2] != '1') {
        ESP_LOGW(TAG, "GT911 probe @ 0x%02X: bogus id %02X %02X %02X %02X",
                 addr, prod[0], prod[1], prod[2], prod[3]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t Touch_Init(void)
{
    // GT911 latches its I2C slave address from the INT line level at the
    // moment RST is released:
    //     INT low  at RST release -> addr 0x5D
    //     INT high at RST release -> addr 0x14
    // We need 0x5D, so drive INT low as an OUTPUT during the reset window,
    // then re-flop it to a floating INPUT for the IRQ line afterwards.
    gpio_config_t int_out_cfg = {
        .pin_bit_mask = 1ULL << GT911_INT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&int_out_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config(INT,out): %s", esp_err_to_name(err));
        return err;
    }
    gpio_set_level(GT911_INT_GPIO, 0);   // INT low -> address 0x5D

    // Pulse TP_RST via TCA9554 EXIO 2, keeping INT low across the
    // RST-release edge so the chip latches the correct address.
    ESP_LOGI(TAG, "GT911 reset via TCA9554 EXIO%d, INT held low for 0x5D",
             GT911_RST_EXIO);
    Mode_EXIO(GT911_RST_EXIO, 0);            // 0 = output mode
    Set_EXIO(GT911_RST_EXIO, false);         // drive RST low
    vTaskDelay(pdMS_TO_TICKS(10));
    Set_EXIO(GT911_RST_EXIO, true);          // release RST -- address latched
    vTaskDelay(pdMS_TO_TICKS(5));            // datasheet: hold INT low >=5ms
                                             //            after RST release

    // Flip INT to a floating input so the GT911 can drive it as IRQ.
    gpio_config_t int_in_cfg = {
        .pin_bit_mask = 1ULL << GT911_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&int_in_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config(INT,in): %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));           // GT911 firmware boot (>=50 ms)

    // Probe both possible addresses. The Waveshare 2.8B comes out at
    // 0x14 even with INT held low at reset, so we accept either.
    s_gt911_addr = GT911_I2C_ADDR_PRIMARY;
    err = gt911_probe(s_gt911_addr);
    if (err != ESP_OK) {
        s_gt911_addr = GT911_I2C_ADDR_ALTERNATE;
        err = gt911_probe(s_gt911_addr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GT911 not responding at 0x5D or 0x14: %s",
                     esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "GT911 responding at 0x%02X (INT=GPIO%d, RST=EXIO%d, "
                  "native %dx%d)",
             s_gt911_addr, GT911_INT_GPIO, GT911_RST_EXIO,
             GT911_NATIVE_X_MAX, GT911_NATIVE_Y_MAX);
    return ESP_OK;
}

bool Touch_Read(int *x_landscape, int *y_landscape)
{
    uint8_t status = 0;
    if (gt911_read(GT911_REG_STATUS, &status, 1) != ESP_OK) {
        return false;
    }
    const bool ready = (status & 0x80) != 0;
    const uint8_t count = status & 0x0F;
    if (!ready || count == 0) {
        // If the buffer is "ready" but reports 0 points, the GT911 still
        // wants the status byte cleared so the next frame can land.
        if (ready) {
            (void) gt911_write_u8(GT911_REG_STATUS, 0);
        }
        return false;
    }

    // Read the first point only: 8 bytes starting at 0x8150.
    uint8_t pt[8];
    if (gt911_read(GT911_REG_POINT1, pt, sizeof(pt)) != ESP_OK) {
        (void) gt911_write_u8(GT911_REG_STATUS, 0);
        return false;
    }
    // Acknowledge the frame regardless of what we do with the data.
    (void) gt911_write_u8(GT911_REG_STATUS, 0);

    // GT911 point 0 layout starting at register 0x8150:
    //   pt[0]=x_low, pt[1]=x_high, pt[2]=y_low, pt[3]=y_high,
    //   pt[4..5]=size, pt[6]=reserved, pt[7]=track id of point 1.
    const uint16_t native_x = (uint16_t)(pt[0] | ((uint16_t) pt[1] << 8));
    const uint16_t native_y = (uint16_t)(pt[2] | ((uint16_t) pt[3] << 8));

    // LVGL 9 unconditionally applies its display-rotation transform to
    // whatever we put in lv_indev_data_t.point (see lv_indev.c around
    // line 636). For LV_DISPLAY_ROTATION_270 with hor_res=480, ver_res=640
    // (the values passed to lv_display_create in LVGL_Driver.c), the net
    // transform is (x, y) -> (y, hor_res - 1 - x).
    //
    // To make LVGL hit-test land where the user touched, we therefore
    // need to feed it coordinates in the panel-native portrait frame --
    // mirrored, because the GT911 on this panel reports both axes
    // inverted relative to what LVGL's rotation expects. Specifically:
    //     input_x = (NATIVE_X_MAX - 1) - raw_x   (range 0..479)
    //     input_y = (NATIVE_Y_MAX - 1) - raw_y   (range 0..639)
    // Verified against calibration:
    //     raw (41, 618) -> (438, 21) -> LVGL (21, 41)     [top-left]
    //     raw (479, 11) -> (0, 628)  -> LVGL (628, 479)   [bottom-right]
    //     raw (256, 314)-> (223, 325)-> LVGL (325, 256)   [center]
    int ix = (GT911_NATIVE_X_MAX - 1) - (int) native_x;
    int iy = (GT911_NATIVE_Y_MAX - 1) - (int) native_y;

    // Clamp to the native portrait bounds; a noisy touch can briefly
    // report outside the panel.
    if (ix < 0) ix = 0;
    if (ix >= GT911_NATIVE_X_MAX) ix = GT911_NATIVE_X_MAX - 1;
    if (iy < 0) iy = 0;
    if (iy >= GT911_NATIVE_Y_MAX) iy = GT911_NATIVE_Y_MAX - 1;

    ESP_LOGD(TAG, "raw=(%u,%u) -> lvgl-input=(%d,%d)",
             native_x, native_y, ix, iy);

    if (x_landscape) *x_landscape = ix;
    if (y_landscape) *y_landscape = iy;
    return true;
}
