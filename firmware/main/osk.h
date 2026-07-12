// On-screen touch keyboard (OSK) for the GAME UI mode.
//
// A transparent full-screen ARGB canvas overlaid on the TRS-80 emulator
// canvas: semi-transparent keys (A-Z, 0-9, one-shot Shift) drawn with the
// authentic TRS-80 Model III font, pre-rotated into the panel-native
// portrait frame the same way TRSCanvas pre-rotates the emulator screen
// (the display is at LV_DISPLAY_ROTATION_0 during games, so normal LVGL
// widgets would appear sideways). Touch is polled straight from the GT911
// (no LVGL indev); taps are injected into the shared input hub via
// input_post_osk(), so games receive them exactly like BT-keyboard keys.
//
// Toggled by board button B7 (intercepted in run_game_session). GAME mode
// only for now — the menu flows have BT/board input and no text fields
// that need it yet.

#pragma once

// Tell the OSK whether Touch_Init() succeeded at boot. Without touch the
// toggle is refused (logged) so a dead overlay can't eat the screen.
void osk_set_touch_available(bool available);

// Request show/hide. Safe from any task; applied on the next osk_tick().
void osk_request_toggle(void);

// Request hide + release of any latched key/shift state. Safe from any
// task. Called when a game session ends.
void osk_force_hide(void);

// Apply pending requests, poll touch, redraw. MUST be called from the
// LVGL-owning task (display_task) — every LVGL call stays on that task.
void osk_tick(void);
