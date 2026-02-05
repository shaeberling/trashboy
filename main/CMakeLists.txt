idf_component_register(
                        SRCS  "main.c" 
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
                        )
