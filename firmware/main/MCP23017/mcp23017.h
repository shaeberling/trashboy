#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCP23017_I2C_ADDR 0x21  // A0=VDD, A1=A2=GND (avoids TCA9554 at 0x20)

// Probe the chip on the I2C bus. Returns ESP_OK if the device ACKs and
// reports a sane IODIRA value (0xFF at power-on). Logs the outcome.
esp_err_t mcp23017_probe(void);

// Configure all GPA pins as inputs with internal pull-ups enabled.
// Buttons on GPA0/GPA1 read LOW when pressed.
esp_err_t mcp23017_init(void);

// Read the GPIOA port (8 bits). Caller-side: pressed = bit == 0.
esp_err_t mcp23017_read_a(uint8_t *out);

// Spawn the periodic button-poll task (500 ms cadence). Logs press/release
// edges for GPA0/GPA1. Returns true on successful task creation.
bool mcp23017_start_button_task(void);

#ifdef __cplusplus
}
#endif
