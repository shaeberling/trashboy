#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// On-demand battery voltage reading. GPIO 4 is both the SDM audio output
// and the board's battery-voltage divider tap, so a reading briefly takes
// the pin away from the sound driver: call only from a menu, never while
// a game is producing sound. Blocks ~100 ms.
//
// Returns false if the ADC could not be set up. `mv` is the battery voltage
// in millivolts, `percent` a rough state of charge (0..100) from a resting
// Li-ion discharge curve — only meaningful with USB unplugged, since the
// charger holds the cell at its charge voltage.
bool battery_read(int *mv, int *percent);

#ifdef __cplusplus
}
#endif
