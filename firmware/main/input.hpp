// Single input hub for TrashBoy.
//
// Two producers feed it — the Bluetooth keyboard and the physical
// MCP23017 buttons — and two consumers read from it: the menus/Wi-Fi
// UI (as translated ASCII) and the TRS-80 emulator (as raw HID
// reports, via process_key). Everything that used to call
// bt_keyboard.wait_for_ascii_char / wait_for_low_event now goes
// through here.
//
// Why a hub and not a shared FIFO: a HID report describes the WHOLE
// keyboard state at an instant, and both consumers diff successive
// reports to derive presses and releases. If the two producers pushed
// their own reports into one queue, each would clobber the other's
// held-key state. So the hub keeps each source's latest report and
// emits their UNION whenever either changes.

#pragma once

#include "bt_keyboard.hpp"

// Create the queue + state. Call once, early in app_main(), before any
// producer can post (i.e. before the MCP poll task starts and before
// BT setup).
void input_init();

// Producer: latest full report from the Bluetooth keyboard. Registered
// as BTKeyboard's report sink so BTKeyboard stays a pure producer.
void input_post_bt(const BTKeyboard::KeyInfo &report);

#ifdef __cplusplus
extern "C" {
#endif
// Producer: latest MCP23017 port snapshot (pressed = bit 0). C-callable
// so it can be registered as the mcp23017 poll-task change callback.
void input_on_buttons(uint8_t port_a, uint8_t port_b);
#ifdef __cplusplus
}
#endif

// Consumer: raw merged HID report (for process_key, F5, splash dismiss).
bool input_wait_event(BTKeyboard::KeyInfo &inf, TickType_t timeout = portMAX_DELAY);

// Consumer: merged stream translated to ASCII, with key-repeat and
// caps-lock handling (for the menus / Wi-Fi text entry).
char input_wait_ascii(bool forever = true);
