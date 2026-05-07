// Copyright 2026 Wenhao Yang
//
// Author: Wenhao Yang
// Contributor: Wenhao Yang
//
// CV input and loopback CV output calibration for the bootloader.

#include "calibration.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "i2s_lsbj.pio.h"

#define PIN_CV1 26u
#define PIN_CV2 27u
#define PIN_I2S_BCLK 12u
#define PIN_I2S_DATA 14u
#define DAC_MAX_COUNTS 32767
#define DAC_SAMPLE_RATE 22050u
#define CV_CAL_POINTS 6u
#define CV_SAMPLE_COUNT 256u
#define CV_COUNTS_PER_VOLT_MIN 100.0f
#define CV_COUNTS_PER_VOLT_MAX 1200.0f
#define CVOUT_COUNTS_PER_VOLT_MIN 4000.0f
#define CVOUT_COUNTS_PER_VOLT_MAX 9000.0f

static PIO dac_pio = pio1;
static uint dac_sm = 0;
static bool dac_ready = false;

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8);
}

static void wait_for_button_press(const PicoBootCalibrationUi *ui) {
  while (!ui->button_pressed()) {
    sleep_ms(5);
  }
  sleep_ms(30);
}

static void adc_init_inputs(void) {
  adc_init();
  adc_gpio_init(PIN_CV1);
  adc_gpio_init(PIN_CV2);
}

static uint16_t adc_read_average(uint8_t adc_input) {
  uint32_t total = 0;
  adc_select_input(adc_input);
  sleep_us(20);
  for (uint16_t i = 0; i < CV_SAMPLE_COUNT; ++i) {
    total += adc_read();
    sleep_us(80);
  }
  return (uint16_t)((total + (CV_SAMPLE_COUNT / 2u)) / CV_SAMPLE_COUNT);
}

static bool valid_range(float value, float min_value, float max_value) {
  return value >= min_value && value <= max_value;
}

static int16_t dac_clamp_counts(int32_t value) {
  if (value > DAC_MAX_COUNTS) return DAC_MAX_COUNTS;
  if (value < -DAC_MAX_COUNTS) return -DAC_MAX_COUNTS;
  return (int16_t)value;
}

static int16_t dac_counts_for_voltage(float volts, float counts_per_volt) {
  const float counts = volts * counts_per_volt;
  return dac_clamp_counts((int32_t)(counts >= 0.0f ? counts + 0.5f : counts - 0.5f));
}

static void dac_init(void) {
  if (dac_ready) return;
  const uint offset = pio_add_program(dac_pio, &i2s_lsbj_out_program);
  i2s_lsbj_out_program_init(dac_pio, dac_sm, offset, PIN_I2S_DATA, PIN_I2S_BCLK, 16);

  const float bit_clk = (float)DAC_SAMPLE_RATE * 16.0f * 2.0f * 2.0f;
  pio_sm_set_clkdiv(dac_pio, dac_sm, (float)clock_get_hz(clk_sys) / bit_clk);
  pio_sm_set_enabled(dac_pio, dac_sm, true);
  dac_ready = true;
}

static void dac_write_stereo(int16_t left, int16_t right) {
  if (!dac_ready) return;
  const uint32_t frame = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;
  pio_sm_put_blocking(dac_pio, dac_sm, frame);
}

static void dac_hold(int16_t sample, uint32_t ms) {
  const uint32_t frames = ((uint32_t)DAC_SAMPLE_RATE * ms) / 1000u;
  for (uint32_t i = 0; i < frames; ++i) {
    dac_write_stereo(sample, sample);
  }
}

static float fit_counts_per_volt(const uint16_t raw[CV_CAL_POINTS], float zero_counts) {
  float weighted_counts = 0.0f;
  float weighted_volts = 0.0f;
  for (uint8_t i = 1; i < CV_CAL_POINTS; ++i) {
    const float volts = (float)i;
    weighted_counts += volts * (zero_counts - (float)raw[i]);
    weighted_volts += volts * volts;
  }
  if (weighted_volts <= 0.0f) return 0.0f;
  return weighted_counts / weighted_volts;
}

static float measured_voltage_from_raw(uint16_t raw, float zero_counts, float counts_per_volt) {
  if (counts_per_volt <= 0.0f) return 0.0f;
  return (zero_counts - (float)raw) / counts_per_volt;
}

static bool wait_for_calibration_press_with_dac(const PicoBootCalibrationUi *ui, int16_t sample) {
  while (!ui->button_pressed()) {
    dac_hold(sample, 8);
  }
  dac_hold(sample, 30);
  return ui->wait_button_release(2000);
}

static bool calibrate_cv_inputs(PicoBootConfig *cfg, const PicoBootCalibrationUi *ui) {
  static const uint32_t colors[CV_CAL_POINTS] = {
      0x000018u, 0x001818u, 0x001800u, 0x181800u, 0x100018u, 0x181818u,
  };
  uint16_t cv1_raw[CV_CAL_POINTS];
  uint16_t cv2_raw[CV_CAL_POINTS];

  ui->led_blink(rgb(0, 18, 18), 2, 90, 90);
  for (uint8_t point = 0; point < CV_CAL_POINTS; ++point) {
    ui->led_set(colors[point]);
    ui->wait_button_release(2000);
    wait_for_button_press(ui);
    cv1_raw[point] = adc_read_average(0);
    cv2_raw[point] = adc_read_average(1);
    ui->led_blink(colors[point], 1, 60, 40);
    ui->wait_button_release(2000);
  }

  const float cv1_zero = (float)cv1_raw[0];
  const float cv2_zero = (float)cv2_raw[0];
  const float cv1_cpv = fit_counts_per_volt(cv1_raw, cv1_zero);
  const float cv2_cpv = fit_counts_per_volt(cv2_raw, cv2_zero);

  if (!valid_range(cv1_cpv, CV_COUNTS_PER_VOLT_MIN, CV_COUNTS_PER_VOLT_MAX) ||
      !valid_range(cv2_cpv, CV_COUNTS_PER_VOLT_MIN, CV_COUNTS_PER_VOLT_MAX)) {
    ui->led_blink(rgb(18, 0, 18), 6, 120, 120);
    return false;
  }

  cfg->calibration.cv1_counts_per_volt = cv1_cpv;
  cfg->calibration.cv2_counts_per_volt = cv2_cpv;
  cfg->calibration.cv1_zero_counts = cv1_zero;
  cfg->calibration.cv2_zero_counts = cv2_zero;
  ui->led_blink(rgb(0, 24, 0), 3, 120, 80);
  return true;
}

static bool calibrate_cv_output(PicoBootConfig *cfg, const PicoBootCalibrationUi *ui) {
  static const uint32_t colors[CV_CAL_POINTS] = {
      0x000018u, 0x001818u, 0x001800u, 0x181800u, 0x100018u, 0x181818u,
  };
  float estimated_sum = 0.0f;
  uint8_t estimated_count = 0;
  float drive_cpv = cfg->calibration.cvout_counts_per_volt;

  if (!valid_range(drive_cpv, CVOUT_COUNTS_PER_VOLT_MIN, CVOUT_COUNTS_PER_VOLT_MAX)) {
    drive_cpv = PICO_BOOT_DEFAULT_CVOUT_COUNTS_PER_VOLT;
  }

  ui->led_blink(rgb(24, 24, 24), 2, 80, 80);
  ui->led_set(rgb(24, 24, 24));
  dac_hold(0, 300);
  wait_for_calibration_press_with_dac(ui, 0);

  for (uint8_t point = 0; point < CV_CAL_POINTS; ++point) {
    const float volts = (float)point;
    const int16_t out = dac_counts_for_voltage(volts, drive_cpv);
    ui->led_set(colors[point]);
    dac_hold(out, 500);
    const uint16_t raw = adc_read_average(0);
    dac_hold(out, 80);

    if (point > 0) {
      const float measured = measured_voltage_from_raw(
          raw, cfg->calibration.cv1_zero_counts, cfg->calibration.cv1_counts_per_volt);
      if (measured > 0.2f && measured < 5.8f) {
        estimated_sum += (float)out / measured;
        estimated_count++;
      }
    }
  }

  dac_hold(0, 250);
  if (estimated_count == 0) {
    ui->led_blink(rgb(18, 0, 18), 6, 120, 120);
    return false;
  }

  const float cvout_cpv = estimated_sum / (float)estimated_count;
  if (!valid_range(cvout_cpv, CVOUT_COUNTS_PER_VOLT_MIN, CVOUT_COUNTS_PER_VOLT_MAX)) {
    ui->led_blink(rgb(18, 0, 18), 6, 120, 120);
    return false;
  }

  cfg->calibration.cvout_counts_per_volt = cvout_cpv;
  cfg->calibration.cvout_zero_counts = 0.0f;
  return true;
}

bool pico_boot_calibration_run(PicoBootConfig *cfg, const PicoBootCalibrationUi *ui) {
  adc_init_inputs();
  dac_init();
  dac_hold(0, 100);

  if (!calibrate_cv_inputs(cfg, ui)) return false;
  if (!calibrate_cv_output(cfg, ui)) return false;

  pico_boot_config_save(cfg);
  ui->led_blink(rgb(0, 24, 0), 3, 160, 100);
  dac_hold(0, 200);
  return true;
}
