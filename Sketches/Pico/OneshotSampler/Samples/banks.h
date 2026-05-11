#pragma once
#include <stdint.h>

struct sample_t {
  const int16_t * samplearray;
  uint32_t samplesize;
  uint32_t sampleindex;
  uint8_t MIDINOTE;
  uint8_t play_volume;
  char sname[20];
};

struct sample_bank_t {
  sample_t *samples;
  uint16_t count;
  const char *name;
};

#include "bank_tr_606.h"
#include "bank_tr_808.h"
#include "bank_tr_909.h"
#include "bank_temp.h"

static sample_bank_t sample_banks[] = {
  {bank_tr_606_samples, bank_tr_606_count, bank_tr_606_name},
  {bank_tr_808_samples, bank_tr_808_count, bank_tr_808_name},
  {bank_tr_909_samples, bank_tr_909_count, bank_tr_909_name},
  {bank_temp_samples, bank_temp_count, bank_temp_name},
};

#define NUM_BANKS ((uint16_t)(sizeof(sample_banks) / sizeof(sample_bank_t)))
