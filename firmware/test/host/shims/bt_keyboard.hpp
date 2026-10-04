// The part of components/bt_keyboard/src/bt_keyboard.hpp that the emulator
// core uses: a keyboard report. The real header needs the Bluetooth stack.
// Keep KeyInfo the same as there.
#pragma once

#include <stdint.h>

class BTKeyboard {
public:
  enum class KeyModifier : uint8_t {
    L_CTRL  = 0x01,
    L_SHIFT = 0x02,
    L_ALT   = 0x04,
    L_META  = 0x08,
    R_CTRL  = 0x10,
    R_SHIFT = 0x20,
    R_ALT   = 0x40,
    R_META  = 0x80
  };

  static const uint8_t MAX_KEY_DATA_SIZE = 20;
  struct KeyInfo {
    uint8_t     size;
    uint8_t     keys[MAX_KEY_DATA_SIZE];
    KeyModifier modifier;
  };
};
