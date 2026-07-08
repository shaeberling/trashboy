// Minimal LVGL bring-up screen for the GT911 touch panel and the
// MCP23017 button expander. Selected at build time via
// CONFIG_TRASHBOY_INPUT_TEST_MODE (see Kconfig.projbuild). Shows an
// "Input Test" title, a small firework burst at every touch press, and
// a two-column grid of "BTN A0..A7" / "BTN B0..B7" labels that light up
// bright green while the corresponding MCP23017 pin is pulled low.
// Runs forever.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialises the touch controller, registers it as an LVGL pointer
// input device, builds the "Input Test" screen, starts the LVGL pump
// task, and never returns. Caller must have already run I2C_Init(),
// EXIO_Init(), LCD_Init(), LVGL_Init(), and (for the button grid to
// respond) mcp23017_init() + mcp23017_start_button_task().
void input_test_run(void);

#ifdef __cplusplus
}
#endif
