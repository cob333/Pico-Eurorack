#pragma once

#include <cstdint>

#include "hardware/pio.h"

namespace audio_bootloader {

struct RgbColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

class Ws2812Driver {
 public:
  void Init(PIO pio, uint32_t sm, uint32_t pin);
  void Set(const RgbColor& color);
  void Clear();

 private:
  PIO pio_ = pio0;
  uint32_t sm_ = 0;
  bool initialized_ = false;
};

RgbColor ScaleColor(const RgbColor& color, float brightness);

}  // namespace audio_bootloader
