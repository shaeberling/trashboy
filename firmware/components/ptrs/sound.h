
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdint>


//#define NOISE_FLOOR 1024
//#define SIGNAL_CENTER 2048
#define NOISE_FLOOR 64
#define SIGNAL_CENTER 127

#define SDM_SAMPLING_FREQ 22000
#define RING_BUFFER_SIZE 6000
#define DMA_BUFFER_SIZE 512

#define SPEED_500     0
#define SPEED_1500    1
#define SPEED_250     2

#define SDM_AUDIO_PIN 16

typedef uint8_t Uchar;
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint16_t Ushort;
typedef uint32_t Uint32;


#define SOUND_RING_SIZE 2048

class TRSSamplesGenerator {
private:
  Uint8 sound_ring[SOUND_RING_SIZE];
  std::atomic<uint32_t> sound_ring_read_idx {0};
  std::atomic<uint32_t> sound_ring_write_idx {0};

public:
  TRSSamplesGenerator() = default;

  bool putSample(Uchar sample);
  int getSample();
  int getSampleFromISR();
  uint32_t getFillLevel() const;
  void flush();
};

extern TRSSamplesGenerator* trsSamplesGenerator;

void init_sound();
void sdm_set_motor_state(bool motor_on);
uint8_t getSample();
int sdm_get_effective_sample_rate();
uint32_t sdm_get_ring_fill();
