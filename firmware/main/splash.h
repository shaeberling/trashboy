#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void splash_init(void);

// Request a status text change. Safe to call from any task; the change is
// applied during the next splash_tick() (which must run on the LVGL task).
// The string must remain valid until the splash is dismissed (use literals).
void splash_set_status(const char *text);
void splash_set_paired(void);

// Apply any pending status updates. Call from the LVGL-driving task.
void splash_tick(void);

// Tear down the splash widgets and restore the saved display rotation.
// Must be called from the LVGL-driving task.
void splash_dismiss(void);

#ifdef __cplusplus
}
#endif
