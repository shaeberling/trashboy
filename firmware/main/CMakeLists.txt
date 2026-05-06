idf_component_register(
                        SRCS  "main.cpp"
                              "splash.c"
                              "logo90c.c"
                              "EXIO/TCA9554PWR.c"
                              "LCD_Driver/ST7701S.c"
                              "LVGL_Driver/LVGL_Driver.c"
                              "I2C_Driver/I2C_Driver.c"
                              "Buzzer/Buzzer.c"

                        INCLUDE_DIRS "./EXIO"
                                     "./LCD_Driver"
                                     "./LVGL_Driver"
                                     "./I2C_Driver"
                                     "./Buzzer"
                                     "."
                        REQUIRES ptrs trs-lib esp_driver_spi esp_lcd nvs_flash bt_keyboard driver bt)
