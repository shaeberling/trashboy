idf_component_register(
                        SRCS  "main.cpp"
                              "splash.c"
                              "wifi_manager.c"
                              "logoc.c"
                              "touch_test.c"
                              "EXIO/TCA9554PWR.c"
                              "LCD_Driver/ST7701S.c"
                              "LVGL_Driver/LVGL_Driver.c"
                              "I2C_Driver/I2C_Driver.c"
                              "Buzzer/Buzzer.c"
                              "Touch_Driver/Touch.c"
                              "MCP23017/mcp23017.c"

                        INCLUDE_DIRS "./EXIO"
                                     "./LCD_Driver"
                                     "./LVGL_Driver"
                                     "./I2C_Driver"
                                     "./Buzzer"
                                     "./Touch_Driver"
                                     "./MCP23017"
                                     "."
                        REQUIRES ptrs trs-lib retrostore-c-sdk esp_driver_spi esp_driver_ledc esp_lcd nvs_flash bt_keyboard driver bt esp_wifi esp_netif esp_event)
# esp_lcd_touch_gt911 is added automatically by the IDF Component Manager
# from main/idf_component.yml — it does not need to be listed in REQUIRES.
