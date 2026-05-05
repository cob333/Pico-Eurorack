// Copyright 2026 Wenhao Yang
//
// Author: Wenhao Yang
// Contributor: Wenhao Yang
//
// Bootloader selector flash layout and shared calibration records for Pico-Eurorack.

#ifndef PICO_SELECTOR_CONFIG_H_
#define PICO_SELECTOR_CONFIG_H_

#include <stdint.h>

#define PICO_SELECTOR_MAGIC 0x50494253u
#define PICO_SELECTOR_VERSION 2u
#define PICO_SELECTOR_MAX_APPS 8u

#define PICO_SELECTOR_BOOT_BYTES (256u * 1024u)
#define PICO_SELECTOR_CONFIG_A_OFFSET (PICO_SELECTOR_BOOT_BYTES - (2u * 4096u))
#define PICO_SELECTOR_CONFIG_B_OFFSET (PICO_SELECTOR_BOOT_BYTES - 4096u)
#define PICO_SELECTOR_FIRST_APP_OFFSET PICO_SELECTOR_BOOT_BYTES

#define PICO_SELECTOR_DEFAULT_SLOT_BYTES (512u * 1024u)
#define PICO_SELECTOR_ARDUINO_VECTOR_OFFSET 0x3000u
#define PICO_SELECTOR_HOLD_SELECT_MS 700u
#define PICO_SELECTOR_HOLD_CONFIRM_MS 900u
#define PICO_SELECTOR_BOOT_GRACE_MS 600u

#define PICO_SELECTOR_FLAG_VALID 0x01u
#define PICO_SELECTOR_FLAG_RP2040 0x02u
#define PICO_SELECTOR_FLAG_RP2350 0x04u
#define PICO_SELECTOR_FLAG_PICOFX 0x08u

typedef struct {
  uint32_t flash_offset;
  uint32_t max_size;
  uint32_t flags;
  uint32_t app_id;
  uint32_t vector_offset;
} PicoSelectorAppSlot;

typedef struct {
  float cv1_counts_per_volt;
  float cv2_counts_per_volt;
  float cvout_counts_per_volt;
  float reserved[5];
} PicoSelectorCalibration;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t record_size;
  uint32_t sequence;
  uint8_t active_app;
  uint8_t app_count;
  uint16_t reserved0;
  PicoSelectorCalibration calibration;
  PicoSelectorAppSlot apps[PICO_SELECTOR_MAX_APPS];
  uint32_t crc32;
} PicoSelectorConfig;

#endif
