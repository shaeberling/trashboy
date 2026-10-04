#pragma once

#include "freertos/FreeRTOS.h"

// Nobody waits on the host: the delay advances the clock that
// esp_timer_get_time() returns (host.cpp), so the emulator runs as fast as
// the host can.
void vTaskDelay(TickType_t ticks);
