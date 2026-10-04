#pragma once

// Call once, before the emulator core is used
void host_init();

// Blank the screen. The ROM does that itself when it starts, but not at
// once: until then a test would still see what the previous one left.
void host_clear_screen();
