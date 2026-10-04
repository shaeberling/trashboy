#pragma once

#include <stdint.h>

// Microseconds of the host's virtual clock, see freertos/task.h
int64_t esp_timer_get_time(void);
