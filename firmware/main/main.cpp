#include <stdio.h>
#include <assert.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

extern "C" {
#include "TCA9554PWR.h"
#include "I2C_Driver.h"
#include "ST7701S.h"
#include "LVGL_Driver.h"
}
#include "trs_screen.h"
#include "trs.h"



void z80_thread(void *arg)
{
  z80_reset();

  while (1) {
    z80_run();
  }
}

void ui_task(void *arg)
{
  trs_screen.init();
  trs_screen.push(new ScreenBuffer(MODE_TEXT_64x16));

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5));
    trs_screen.render();
    lv_timer_handler();
  }
}

extern "C" void app_main(void)
{
    // Initialize I2C (required by EXIO)
    I2C_Init();
    
    // Initialize EXIO (required by LCD)
    EXIO_Init();
    
    // Initialize LCD
    LCD_Init();
    
    // Initialize LVGL
    LVGL_Init();
    
    xTaskCreatePinnedToCore(ui_task, "ui_task", 6000, NULL, 5, NULL, 1);

    /*while(1)*/ vTaskDelay(pdMS_TO_TICKS(1000));
    z80_thread(NULL);
}
