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

// Read the GPIOB port (8 bits). Caller-side: pressed = bit == 0.
esp_err_t mcp23017_read_b(uint8_t *out);

// Return the most recent port A/B snapshot cached by the button task.
// Both pointers may be NULL. Safe to call from any task; the underlying
// stores are 8-bit aligned so reads/writes are atomic on this MCU.
void mcp23017_get_ports(uint8_t *port_a, uint8_t *port_b);

// Called from the poll task whenever the port state changes, with the
// fresh port A/B snapshots (pressed = bit 0). Runs in the poll task's
// context. May be NULL.
typedef void (*mcp23017_change_cb_t)(uint8_t port_a, uint8_t port_b);

// Spawn the button poll task. It reads ports A and B over I2C on a fixed
// cadence, caches the result for mcp23017_get_ports(), logs press/release
// edges, and (if on_change != NULL) invokes it on every change. Polling
// (rather than the chip's INTA/INTB pins) is deliberate: this board has
// no free, exposed, non-strapping GPIO to receive an interrupt. Returns
// true on successful task creation.
bool mcp23017_start_button_task(mcp23017_change_cb_t on_change);

#ifdef __cplusplus
}
#endif
