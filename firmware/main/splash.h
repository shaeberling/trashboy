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

// Optional bottom-left hint (used for the password entry value or the
// "ESC: back" hint). Pass NULL or "" to hide. Lifetime same as
// splash_set_status.
void splash_set_subtext(const char *text);

// Optional bottom-RIGHT hint, displayed at the same y as the left subtext
// but right-aligned. Pass NULL or "" to hide.
void splash_set_subtext_right(const char *text);

// Bottom status bar (white box, black lettering, Wi-Fi symbol prefix).
// Shows system status — currently the Wi-Fi state. The string must remain
// valid until the next call (use literals or a stable buffer).
void splash_set_statusbar(const char *text);

// Right-aligned text in the same bar (the device's IP address while Wi-Fi
// is connected). Pass "" to clear. Same lifetime rule as above.
void splash_set_statusbar_right(const char *text);

// Green progress bar below the status line: hollow (green border) at 0,
// filling green as `percent` grows. Pass 0..100 to show/update, -1 to hide.
void splash_set_progress(int percent);

// Apply any pending status updates. Call from the LVGL-driving task.
void splash_tick(void);

// Show a vertical list of selectable items below the status. The list will
// replace any previously shown list. `items` are not retained — strings are
// copied into the splash's internal buffer.
void splash_show_list(const char * const *items, int count, int selected);

// Same, in a much larger font: for the main menu, whose few short entries
// should be readable at arm's length. At most 4 rows fit; longer lists and
// longer texts belong in splash_show_list.
void splash_show_list_large(const char * const *items, int count, int selected);

// Update which row is highlighted in the currently shown list.
void splash_set_list_selection(int selected);

// Remove the list widgets.
void splash_hide_list(void);

// Switch the layout into "compact" mode: logo shrinks to 50% and slides to
// the top, and the status / list / subtext positions move up to give more
// vertical room for the Wi-Fi list. Idempotent.
void splash_set_compact(void);

// Tear down the splash widgets and restore the saved display rotation.
// Must be called from the LVGL-driving task.
void splash_dismiss(void);

#ifdef __cplusplus
}
#endif
