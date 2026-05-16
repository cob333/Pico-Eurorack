// Copyright 2026 Wenhao Yang
//
// Author: Wenhao Yang
// Contributor: Wenhao Yang
//
// Read-only Pico-Eurorack bootloader config access for slot-loaded apps.

#ifndef PICO_BOOT_CONFIG_H_
#define PICO_BOOT_CONFIG_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define PICO_BOOT_CONFIG_MAGIC 0x50494253u
#define PICO_BOOT_CONFIG_VERSION 4u
#define PICO_BOOT_CONFIG_MAX_APPS 8u
#define PICO_BOOT_XIP_BASE 0x10000000u
#define PICO_BOOT_BYTES (256u * 1024u)
#define PICO_BOOT_CONFIG_A_OFFSET (PICO_BOOT_BYTES - (2u * 4096u))
#define PICO_BOOT_CONFIG_B_OFFSET (PICO_BOOT_BYTES - 4096u)

typedef struct {
  float cv1_counts_per_volt;
  float cv2_counts_per_volt;
  float cvout_counts_per_volt;
  float cv1_zero_counts;
  float cv2_zero_counts;
  float cvout_zero_counts;
  float reserved[2];
} PicoBootCalibration;

typedef struct {
  uint32_t flash_offset;
  uint32_t max_size;
  uint32_t flags;
  uint32_t app_id;
  uint32_t vector_offset;
} PicoBootAppSlot;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t record_size;
  uint32_t sequence;
  uint8_t active_app;
  uint8_t app_count;
  uint16_t reserved0;
  uint32_t cpu_hz;
  PicoBootCalibration calibration;
  PicoBootAppSlot apps[PICO_BOOT_CONFIG_MAX_APPS];
  uint32_t crc32;
} PicoBootConfig;

static inline uint32_t PicoBootFnv1a32(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    hash ^= p[i];
    hash *= 16777619u;
  }
  return hash;
}

static inline uint32_t PicoBootConfigCrc(const PicoBootConfig *cfg) {
  return PicoBootFnv1a32(cfg, offsetof(PicoBootConfig, crc32));
}

static inline int PicoBootConfigValid(const PicoBootConfig *cfg) {
  if (cfg->magic != PICO_BOOT_CONFIG_MAGIC) return 0;
  if (cfg->version != PICO_BOOT_CONFIG_VERSION) return 0;
  if (cfg->record_size != sizeof(PicoBootConfig)) return 0;
  if (cfg->app_count == 0 || cfg->app_count > PICO_BOOT_CONFIG_MAX_APPS) return 0;
  if (cfg->active_app >= cfg->app_count) return 0;
  if (cfg->cpu_hz < 48000000u || cfg->cpu_hz > 300000000u) return 0;
  if (cfg->crc32 != PicoBootConfigCrc(cfg)) return 0;
  return 1;
}

static inline const PicoBootConfig *PicoBootConfigAt(uint32_t flash_offset) {
  return (const PicoBootConfig *)(PICO_BOOT_XIP_BASE + flash_offset);
}

static inline void PicoBootCalibrationDefaults(PicoBootCalibration *cal) {
  memset(cal, 0, sizeof(*cal));
  cal->cv1_counts_per_volt = 582.52f;
  cal->cv2_counts_per_volt = 582.52f;
  cal->cvout_counts_per_volt = 6553.4f;
  cal->cv1_zero_counts = 4095.0f;
  cal->cv2_zero_counts = 4095.0f;
  cal->cvout_zero_counts = 0.0f;
}

static inline int PicoBootLoadConfig(PicoBootConfig *out) {
  const PicoBootConfig *a = PicoBootConfigAt(PICO_BOOT_CONFIG_A_OFFSET);
  const PicoBootConfig *b = PicoBootConfigAt(PICO_BOOT_CONFIG_B_OFFSET);
  const int av = PicoBootConfigValid(a);
  const int bv = PicoBootConfigValid(b);

  if (av && (!bv || a->sequence >= b->sequence)) {
    memcpy(out, a, sizeof(*out));
    return 1;
  }
  if (bv) {
    memcpy(out, b, sizeof(*out));
    return 1;
  }
  memset(out, 0, sizeof(*out));
  out->cpu_hz = 250000000u;
  PicoBootCalibrationDefaults(&out->calibration);
  return 0;
}

static inline int PicoBootLoadCalibration(PicoBootCalibration *out) {
  PicoBootConfig cfg;
  const int loaded = PicoBootLoadConfig(&cfg);
  *out = cfg.calibration;
  return loaded;
}

#endif
