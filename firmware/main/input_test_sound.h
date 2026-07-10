// C-callable shim over the C++ SDM sound API in components/ptrs/sound.h,
// for the input-test screen's button feedback. See input_test_sound.cpp.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the SDM audio channel on GPIO4 (calls ptrs init_sound()).
// Only call this on the input-test path — the emulator path calls
// init_sound() itself and the SDM channel must not be created twice.
void input_sound_init(void);

// Play a short (~60 ms) soft sine blip on the speaker. Retriggering
// while a blip is still playing restarts it.
void input_sound_beep(void);

#ifdef __cplusplus
}
#endif
