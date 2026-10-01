// C-callable shim over the C++ SDM sound API in components/ptrs/sound.h,
// for the input-test screen's button feedback. See input_test_sound.cpp.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Play a short (~60 ms) soft sine blip on the speaker. Retriggering
// while a blip is still playing restarts it. Silent no-op until the
// emulator's init_sound() (z80_task) has brought up the SDM channel.
void input_sound_beep(void);

#ifdef __cplusplus
}
#endif
