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

// Spawn the button task driven by the MCP23017's INTA pin. Configures
// the chip for interrupt-on-change across port A and the given ESP32
// GPIO for a falling-edge interrupt with internal pull-up. The task
// wakes on each edge, reads GPIOA (which clears the chip-side INT),
// and logs press/release for GPA0/GPA1.
bool mcp23017_start_button_task(int int_gpio);

#ifdef __cplusplus
}
#endif
