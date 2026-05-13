
#include "sound.h"
#include <driver/gptimer.h>
#include <driver/sdm.h>
#include <stdint.h>

#define SDM_OVERSAMPLE_RATE_HZ (10000000)
// Keep this on a divider-friendly value for APB timers.
#define SDM_TIMER_RESOLUTION_HZ (2000000)

#define UNDERRUN_HOLD_SAMPLES 88


static std::atomic<uint32_t> s_effective_sample_rate_hz {SDM_SAMPLING_FREQ};

int sdm_get_effective_sample_rate()
{
  return (int) s_effective_sample_rate_hz.load(std::memory_order_relaxed);
}

uint32_t sdm_get_ring_fill()
{
  if (trsSamplesGenerator == NULL) {
    return 0;
  }

  return trsSamplesGenerator->getFillLevel();
}

static inline uint32_t ringFillLevel(uint32_t read_idx, uint32_t write_idx)
{
  return (write_idx >= read_idx)
    ? (write_idx - read_idx)
    : (SOUND_RING_SIZE - (read_idx - write_idx));
}

bool TRSSamplesGenerator::putSample(Uchar sample) {
  uint32_t write_idx = sound_ring_write_idx.load(std::memory_order_relaxed);
  uint32_t next_write_idx = write_idx + 1;
  if (next_write_idx >= SOUND_RING_SIZE) {
    next_write_idx = 0;
  }

  uint32_t read_idx = sound_ring_read_idx.load(std::memory_order_acquire);
  if (next_write_idx == read_idx) {
    // Buffer full: drop newest sample to preserve waveform continuity.
    return false;
  }

  sound_ring[write_idx] = sample;
  sound_ring_write_idx.store(next_write_idx, std::memory_order_release);
  return true;
}

int TRSSamplesGenerator::getSample() {
  return getSampleFromISR();
}

void TRSSamplesGenerator::flush() {
  // Discard all pending samples by advancing the read index to the write index.
  uint32_t write_idx = sound_ring_write_idx.load(std::memory_order_acquire);
  sound_ring_read_idx.store(write_idx, std::memory_order_release);
}

uint32_t TRSSamplesGenerator::getFillLevel() const {
  const uint32_t read_idx = sound_ring_read_idx.load(std::memory_order_acquire);
  const uint32_t write_idx = sound_ring_write_idx.load(std::memory_order_acquire);
  return ringFillLevel(read_idx, write_idx);
}

int TRSSamplesGenerator::getSampleFromISR() {
  int sample = SIGNAL_CENTER;
  int volume = 100;
  static int last_signed_sample = 0;
  static uint32_t underrun_samples = 0;

  uint32_t read_idx = sound_ring_read_idx.load(std::memory_order_relaxed);
  uint32_t write_idx = sound_ring_write_idx.load(std::memory_order_acquire);
  if (read_idx != write_idx) {
    sample = sound_ring[read_idx];
    read_idx++;
    if (read_idx >= SOUND_RING_SIZE) {
      read_idx = 0;
    }
    sound_ring_read_idx.store(read_idx, std::memory_order_release);

    // Convert unsigned 8-bit sample to signed audio around center.
    last_signed_sample = (sample - SIGNAL_CENTER) * volume / 127;
    underrun_samples = 0;
  } else {
    // Brief underruns: hold last sample. After threshold, fade to neutral.
    if (underrun_samples < UNDERRUN_HOLD_SAMPLES) {
      underrun_samples++;
      // Hold last_signed_sample as-is
    } else {
      // Extended underrun: fade toward center to reduce pops
      underrun_samples++;
      last_signed_sample = (last_signed_sample * 63) / 64;
    }
  }

  return last_signed_sample;
}

TRSSamplesGenerator* trsSamplesGenerator = NULL;

static volatile bool s_sdm_tx_enabled = true;
static sdm_channel_handle_t s_sdm_channel = NULL;
static gptimer_handle_t s_sdm_timer = NULL;
static bool s_sdm_channel_enabled = false;

void sdm_set_motor_state(bool motor_on) {
  // Sound output is active when cassette motor is OFF.
  s_sdm_tx_enabled = !motor_on;

  if (s_sdm_channel == NULL) {
    return;
  }

  // Flush stale samples on every motor transition to avoid pops.
  if (trsSamplesGenerator != NULL) {
    trsSamplesGenerator->flush();
  }

  if (!motor_on) {
    if (!s_sdm_channel_enabled) {
      ESP_ERROR_CHECK(sdm_channel_enable(s_sdm_channel));
      s_sdm_channel_enabled = true;
    }
    ESP_ERROR_CHECK(sdm_channel_set_pulse_density(s_sdm_channel, 0));
  } else if (s_sdm_channel_enabled) {
    // Zero the output before disabling to avoid leaving a DC level on the pin.
    ESP_ERROR_CHECK(sdm_channel_set_pulse_density(s_sdm_channel, 0));
    ESP_ERROR_CHECK(sdm_channel_disable(s_sdm_channel));
    s_sdm_channel_enabled = false;
  }
}

uint8_t getSample() {
  return 0;
}

// SDM density is clamped to 20% of full scale (±25 out of ±127) to protect
// the AST-01508MR-R speaker from over-excursion.
static const int SDM_AMPLITUDE_LIMIT = 25;

static int8_t buildSdmDensitySample()
{
  int sample = trsSamplesGenerator->getSampleFromISR();

  if (sample > SDM_AMPLITUDE_LIMIT) {
    sample = SDM_AMPLITUDE_LIMIT;
  } else if (sample < -SDM_AMPLITUDE_LIMIT) {
    sample = -SDM_AMPLITUDE_LIMIT;
  }

  return (int8_t) sample;
}

static bool IRAM_ATTR sdmTimerCallback(gptimer_handle_t timer,
                                      const gptimer_alarm_event_data_t *edata,
                                      void *user_ctx)
{
  if (!s_sdm_tx_enabled || !s_sdm_channel_enabled || s_sdm_channel == NULL || trsSamplesGenerator == NULL) {
    return false;
  }

  // No hard mute - always output the sample
  int8_t density = buildSdmDensitySample();
  sdm_channel_set_pulse_density(s_sdm_channel, density);
  return false;
}

static void initSdmTimer()
{
  gptimer_config_t timer_cfg = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = SDM_TIMER_RESOLUTION_HZ,
  };
  ESP_ERROR_CHECK(gptimer_new_timer(&timer_cfg, &s_sdm_timer));

  uint32_t real_resolution_hz = 0;
  ESP_ERROR_CHECK(gptimer_get_resolution(s_sdm_timer, &real_resolution_hz));

  uint32_t alarm_count = (real_resolution_hz + (SDM_SAMPLING_FREQ / 2)) / SDM_SAMPLING_FREQ;
  if (alarm_count == 0) {
    alarm_count = 1;
  }
  uint32_t effective_sample_rate_hz = real_resolution_hz / alarm_count;
  s_effective_sample_rate_hz.store(effective_sample_rate_hz, std::memory_order_relaxed);

  gptimer_alarm_config_t alarm_cfg = {
    .alarm_count = alarm_count,
    .reload_count = 0,
    .flags = {
      .auto_reload_on_alarm = true,
    },
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(s_sdm_timer, &alarm_cfg));

  gptimer_event_callbacks_t cbs = {
    .on_alarm = sdmTimerCallback,
  };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_sdm_timer, &cbs, NULL));
  ESP_ERROR_CHECK(gptimer_set_raw_count(s_sdm_timer, 0));
  ESP_ERROR_CHECK(gptimer_enable(s_sdm_timer));
  ESP_ERROR_CHECK(gptimer_start(s_sdm_timer));
}

void init_sound()
{
  sdm_config_t sdm_config = {
    .gpio_num = SDM_AUDIO_PIN,
    .clk_src = SDM_CLK_SRC_DEFAULT,
    .sample_rate_hz = SDM_OVERSAMPLE_RATE_HZ,
  };

  ESP_ERROR_CHECK(sdm_new_channel(&sdm_config, &s_sdm_channel));
  s_sdm_channel_enabled = false;

  trsSamplesGenerator = new TRSSamplesGenerator();

  sdm_set_motor_state(false);
  initSdmTimer();
}
