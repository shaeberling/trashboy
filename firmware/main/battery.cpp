#include "battery.h"

#include <driver/gpio.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "sdkconfig.h"
#include "sound.h"

static const char *TAG = "battery";

// The pin's RC network (audio reconstruction filter) was last driven by the
// SDM at mid-rail; let it discharge onto the divider's level before sampling.
static const int SETTLE_MS = 60;
static const int SAMPLES = 32;

// Resting single-cell Li-ion/LiPo voltage -> state of charge. Coarse, but
// good enough for a "how much is left" indication.
static const struct { int mv; int pct; } kCurve[] = {
  {4200, 100}, {4100, 90}, {4000, 80}, {3920, 70}, {3850, 60}, {3800, 50},
  {3750, 40},  {3700, 30}, {3650, 20}, {3550, 10}, {3400, 5},  {3200, 0},
};

static int mv_to_percent(int mv) {
  const int n = sizeof(kCurve) / sizeof(kCurve[0]);
  if (mv >= kCurve[0].mv) return 100;
  for (int i = 1; i < n; i++) {
    if (mv >= kCurve[i].mv) {
      int dv = kCurve[i - 1].mv - kCurve[i].mv;
      int dp = kCurve[i - 1].pct - kCurve[i].pct;
      return kCurve[i].pct + (mv - kCurve[i].mv) * dp / dv;
    }
  }
  return 0;
}

bool battery_read(int *mv, int *percent) {
  adc_unit_t unit;
  adc_channel_t channel;
  if (adc_oneshot_io_to_channel(SDM_AUDIO_PIN, &unit, &channel) != ESP_OK) {
    ESP_LOGE(TAG, "GPIO %d has no ADC channel", (int) SDM_AUDIO_PIN);
    return false;
  }

  sound_release_pin();

  bool ok = false;
  adc_oneshot_unit_handle_t adc = NULL;
  adc_cali_handle_t cali = NULL;
  adc_oneshot_unit_init_cfg_t unit_cfg = {};
  unit_cfg.unit_id = unit;
  adc_oneshot_chan_cfg_t chan_cfg = {};
  chan_cfg.atten = ADC_ATTEN_DB_12;  // full ~0-3.1 V range: safe for any divider
  chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  adc_cali_curve_fitting_config_t cali_cfg = {};
  cali_cfg.unit_id = unit;
  cali_cfg.chan = channel;
  cali_cfg.atten = ADC_ATTEN_DB_12;
  cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;

  int raw_sum = 0;
  int pin_mv_sum = 0;
  if (adc_oneshot_new_unit(&unit_cfg, &adc) != ESP_OK ||
      adc_oneshot_config_channel(adc, channel, &chan_cfg) != ESP_OK ||
      adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) != ESP_OK) {
    ESP_LOGE(TAG, "ADC setup failed");
  } else {
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    ok = true;
    for (int i = 0; i < SAMPLES && ok; i++) {
      int raw = 0, pin_mv = 0;
      ok = adc_oneshot_read(adc, channel, &raw) == ESP_OK &&
           adc_cali_raw_to_voltage(cali, raw, &pin_mv) == ESP_OK;
      raw_sum += raw;
      pin_mv_sum += pin_mv;
    }
  }

  if (cali != NULL) adc_cali_delete_scheme_curve_fitting(cali);
  if (adc != NULL) adc_oneshot_del_unit(adc);
  sound_reclaim_pin();

  if (!ok) return false;
  int pin_mv = pin_mv_sum / SAMPLES;
  *mv = pin_mv * CONFIG_TRASHBOY_BATTERY_DIVIDER_X1000 / 1000;
  *percent = mv_to_percent(*mv);
  // Logged so the divider factor can be calibrated against a multimeter.
  ESP_LOGI(TAG, "raw=%d pin=%d mV -> battery=%d mV (%d%%), factor=%d/1000",
           raw_sum / SAMPLES, pin_mv, *mv, *percent,
           CONFIG_TRASHBOY_BATTERY_DIVIDER_X1000);
  return true;
}
