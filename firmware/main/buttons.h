// Names for TrashBoy's physical buttons, so code and docs can say
// BTN_R_ACTION_UPPER instead of "B5".
//
// Every button is one MCP23017 pin. Port A is the left half of the device,
// port B the right half, and the two halves mirror each other:
//
//        left (port A)                    right (port B)
//    A0  shoulder (back)              B0  shoulder (back)
//    A1  D-pad up                     B1  D-pad up
//    A2  D-pad right                  B2  D-pad right
//    A3  D-pad down                   B3  D-pad down
//    A4  D-pad left                   B4  D-pad left
//    A5  upper action button          B5  upper action button
//    A6  lower action button          B6  lower action button
//    A7  menu / home (centre)         B7  on-screen keyboard (centre)
//
// The shoulder buttons are on the carrier but not wired up yet. A7 and B7
// are system buttons (see main.cpp): don't hand them to a game.
//
// Per-game suggestions for what each button should send are in
// GAME_KEYS.md, section 7.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Button id = pin number within the 16-bit (port B << 8 | port A) word:
// 0-7 are A0-A7, 8-15 are B0-B7.
typedef enum {
  BTN_L_SHOULDER     = 0,   // A0 (not wired yet)
  BTN_L_DPAD_UP      = 1,   // A1
  BTN_L_DPAD_RIGHT   = 2,   // A2
  BTN_L_DPAD_DOWN    = 3,   // A3
  BTN_L_DPAD_LEFT    = 4,   // A4
  BTN_L_ACTION_UPPER = 5,   // A5
  BTN_L_ACTION_LOWER = 6,   // A6
  BTN_MENU           = 7,   // A7: leaves the running game

  BTN_R_SHOULDER     = 8,   // B0 (not wired yet)
  BTN_R_DPAD_UP      = 9,   // B1
  BTN_R_DPAD_RIGHT   = 10,  // B2
  BTN_R_DPAD_DOWN    = 11,  // B3
  BTN_R_DPAD_LEFT    = 12,  // B4
  BTN_R_ACTION_UPPER = 13,  // B5
  BTN_R_ACTION_LOWER = 14,  // B6
  BTN_OSK            = 15,  // B7: toggles the on-screen keyboard

  BTN_COUNT          = 16,
} button_t;

// Fold a raw port snapshot (a pressed pin reads 0) into a mask with bit n
// set while button id n is held down.
static inline uint16_t buttons_pressed(uint8_t port_a, uint8_t port_b) {
  return (uint16_t) ((uint8_t) ~port_a | ((uint16_t) (uint8_t) ~port_b << 8));
}

static inline bool button_is_down(uint16_t pressed, button_t button) {
  return ((pressed >> button) & 1u) != 0;
}

#ifdef __cplusplus
}
#endif
