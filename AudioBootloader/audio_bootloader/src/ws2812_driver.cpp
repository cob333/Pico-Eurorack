#include "ws2812_driver.h"

#include <algorithm>

#include "hardware/clocks.h"
#include "ws2812.pio.h"

namespace audio_bootloader {

namespace {

void PutPixel(PIO pio, uint sm, uint32_t pixel_grb) {
  pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

}  // namespace

void Ws2812Driver::Init(PIO pio, uint32_t sm, uint32_t pin) {
  pio_ = pio;
  sm_ = sm;
  const uint offset = pio_add_program(pio_, &ws2812_program);
  pio_sm_config config = ws2812_program_get_default_config(offset);
  sm_config_set_sideset_pins(&config, pin);
  sm_config_set_out_shift(&config, false, true, 24);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
  const int cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
  const float divider = static_cast<float>(clock_get_hz(clk_sys)) / (800000.0f * static_cast<float>(cycles_per_bit));
  sm_config_set_clkdiv(&config, divider);

  pio_gpio_init(pio_, pin);
  pio_sm_set_consecutive_pindirs(pio_, sm_, pin, 1, true);
  pio_sm_init(pio_, sm_, offset, &config);
  pio_sm_set_enabled(pio_, sm_, true);
  initialized_ = true;
  Clear();
}

void Ws2812Driver::Set(const RgbColor& color) {
  if (!initialized_) {
    return;
  }
  const uint32_t grb =
      (static_cast<uint32_t>(color.g) << 16) |
      (static_cast<uint32_t>(color.r) << 8) |
      static_cast<uint32_t>(color.b);
  PutPixel(pio_, sm_, grb);
}

void Ws2812Driver::Clear() {
  Set({0, 0, 0});
}

RgbColor ScaleColor(const RgbColor& color, float brightness) {
  const float clamped = std::clamp(brightness, 0.0f, 1.0f);
  return {
      static_cast<uint8_t>(color.r * clamped),
      static_cast<uint8_t>(color.g * clamped),
      static_cast<uint8_t>(color.b * clamped),
  };
}

}  // namespace audio_bootloader
