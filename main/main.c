#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "TCA9554PWR.h"
#include "I2C_Driver.h"
#include "ST7701S.h"
#include "LVGL_Driver.h"

void app_main(void)
{
    // Initialize I2C (required by EXIO)
    I2C_Init();
    
    // Initialize EXIO (required by LCD)
    EXIO_Init();
    
    // Initialize LCD
    LCD_Init();
    
    // Initialize LVGL
    LVGL_Init();
    
    // Create a label with "Hello World" text centered on the screen
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello World");
    lv_obj_center(label);
    
    // Main LVGL loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
