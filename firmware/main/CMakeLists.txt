# One firmware per board (menuconfig -> Trashboy -> Board). The boards share
# the components (emulator core, trs-io, ...) and nothing in this directory:
# the Watcher build is watcher/ alone. CONFIG_* is not known yet when CMake
# first scans the components for their requirements, so REQUIRES and
# INCLUDE_DIRS below are the same for both.
if(CONFIG_TRASHBOY_BOARD_SENSECAP_WATCHER)
    set(srcs "watcher/watcher_main.cpp"
             "watcher/watcher_screen.cpp"
             "watcher/watcher_board.c")
    # The one game this board runs, see watcher/watcher_main.cpp. Not in the
    # repository; scripts/watcher.sh build downloads it.
    set(embed "watcher/breakdown.cmd")
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${embed}")
        message(FATAL_ERROR "main/${embed} is missing: build with scripts/watcher.sh build, "
                            "or fetch it with scripts/fetch_retrostore_cmd.py")
    endif()
else()
    set(srcs "main.cpp"
             "input.cpp"
             "osk.cpp"
             "games_cache.cpp"
             "battery.cpp"
             "splash.c"
             "wifi_manager.c"
             "logoc.c"
             "input_test.c"
             "input_test_sound.cpp"
             "EXIO/TCA9554PWR.c"
             "LCD_Driver/ST7701S.c"
             "LVGL_Driver/LVGL_Driver.c"
             "I2C_Driver/I2C_Driver.c"
             "Buzzer/Buzzer.c"
             "Touch_Driver/Touch.c"
             "MCP23017/mcp23017.c")
    set(embed "")
endif()

idf_component_register(
                        SRCS  ${srcs}
                        EMBED_FILES ${embed}

                        INCLUDE_DIRS "./EXIO"
                                     "./LCD_Driver"
                                     "./LVGL_Driver"
                                     "./I2C_Driver"
                                     "./Buzzer"
                                     "./Touch_Driver"
                                     "./MCP23017"
                                     "./watcher"
                                     "."
                        REQUIRES ptrs trs-lib retrostore-c-sdk esp_driver_spi esp_driver_ledc esp_lcd nvs_flash bt_keyboard driver bt esp_wifi esp_netif esp_event fatfs esp_adc
                                 esp_driver_gpio esp_driver_i2c esp_driver_pcnt esp_timer)
# esp_lcd_touch_gt911 is added automatically by the IDF Component Manager
# from main/idf_component.yml — it does not need to be listed in REQUIRES.
