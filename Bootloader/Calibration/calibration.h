// Copyright 2026 Wenhao Yang
//
// Author: Wenhao Yang
// Contributor: Wenhao Yang
//
// Bootloader global calibration workflow for Pico-Eurorack.

#ifndef PICO_BOOT_CALIBRATION_H_
#define PICO_BOOT_CALIBRATION_H_

#include <stdbool.h>
#include <stdint.h>

#include "../boot_config.h"

typedef struct {
  void (*led_set)(uint32_t color);
  void (*led_blink)(uint32_t color, uint8_t times, uint32_t on_ms, uint32_t off_ms);
  bool (*button_pressed)(void);
  bool (*wait_button_release)(uint32_t timeout_ms);
} PicoBootCalibrationUi;

bool pico_boot_calibration_run(PicoBootConfig *cfg, const PicoBootCalibrationUi *ui);

#endif
