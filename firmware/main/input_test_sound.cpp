// C shim over the C++ sound API (components/ptrs/sound.h) so the
// input-test screen (plain C) can play button-press feedback through
// the speaker instead of the TCA9554 buzzer.

#include "input_test_sound.h"

#include <math.h>

#include "sound.h"

// Blip shape: 880 Hz sine, 60 ms total, 5 ms attack / 20 ms release
// linear envelope so it starts and ends without clicks.
#define BEEP_FREQ_HZ     880.0f
#define BEEP_TOTAL_MS    60
#define BEEP_ATTACK_MS   5
#define BEEP_RELEASE_MS  20

// Peak amplitude around SIGNAL_CENTER. sound.cpp clamps SDM density to
// +/-25 (speaker over-excursion protection), so 25 is exactly full volume.
#define BEEP_AMPLITUDE   25.0f

extern "C" void input_sound_init(void)
{
  init_sound();
}

extern "C" void input_sound_beep(void)
{
  if (trsSamplesGenerator == NULL) {
    return;
  }

  const int rate = sdm_get_effective_sample_rate();
  const int total   = rate * BEEP_TOTAL_MS / 1000;    // ~1320 @ 22 kHz
  const int attack  = rate * BEEP_ATTACK_MS / 1000;
  const int release = rate * BEEP_RELEASE_MS / 1000;

  // Retrigger: drop whatever's left of a previous blip so a fresh press
  // always starts a fresh sound. (The ring holds ~93 ms at 22 kHz, so a
  // full blip fits; without the flush a rapid press series would just
  // fill the ring and drop samples.)
  trsSamplesGenerator->flush();

  const float w = 2.0f * (float) M_PI * BEEP_FREQ_HZ / (float) rate;
  for (int i = 0; i < total; i++) {
    float env = 1.0f;
    if (i < attack) {
      env = (float) i / (float) attack;
    } else if (i > total - release) {
      env = (float) (total - i) / (float) release;
    }
    const float s = sinf(w * (float) i) * env * BEEP_AMPLITUDE;
    trsSamplesGenerator->putSample((Uchar) (SIGNAL_CENTER + (int) s));
  }
}
