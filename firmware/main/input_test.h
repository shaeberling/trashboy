// Input test screen for the GT911 touch panel and the MCP23017 button
// expander, reached via Settings -> Input Test. Shows an "Input Test"
// title, a small firework burst at every touch press, and a two-column
// grid of "BTN A0..A7" / "BTN B0..B7" labels that light up bright green
// while the corresponding MCP23017 pin is pulled low. Touches pulse the
// TCA9554 buzzer; button presses blip the speaker.
//
// It is an overlay on the menu UI (LVGL rotation 270): show/hide build and
// tear down the widgets, and LVGL timers do the rest. Both functions make
// LVGL calls, so they MUST run on the LVGL-owning task (display_task).
// The caller owns the exit condition -- every button is under test here,
// so there is no single "back" key (see run_input_test in main.cpp).

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Build the test screen on top of the active screen. `touch_ok` says
// whether Touch_Init() succeeded at boot; without touch only the button
// grid is live. The ~1.2 MB canvas is allocated from PSRAM here and freed
// again in input_test_hide(). The speaker blip needs init_sound() to have
// run (z80_task does that); until then button presses are just silent.
void input_test_show(bool touch_ok);

// Tear the test screen down again, uncovering the menu UI.
void input_test_hide(void);

#ifdef __cplusplus
}
#endif
