// Minimal LVGL bring-up screen for the new GT911 touch panel. Selected at
// build time via CONFIG_TRASHBOY_TOUCH_TEST_MODE (see Kconfig.projbuild).
// Shows a "Touch Test" label and drops a coloured circle at every press
// position. Runs forever.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialises the touch controller, registers it as an LVGL pointer
// input device, builds the "Touch Test" screen, starts the LVGL pump
// task, and never returns. Caller must have already run I2C_Init(),
// EXIO_Init(), LCD_Init() and LVGL_Init().
void touch_test_run(void);

#ifdef __cplusplus
}
#endif
