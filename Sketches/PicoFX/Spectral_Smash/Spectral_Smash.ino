// Copyright 2026 Rich Heslip
//
// Author: Rich Heslip
// Contributor: Wenhao Yang
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
/*
Spectral Freeze Tear for 2HPico DSP hardware
RP2350 required. 22050 Hz keeps the FFT workload reasonable; 250 MHz is recommended.

Top Jack - Audio input

Middle Jack - Right audio out (solder the DAC jumper on the back of the PCB)

Bottom Jack - Left audio out

Button:
  - short press toggles freeze
  - hold for 0.8s while frozen to recapture the current spectrum

Pot 1 - Warp / Tear
Pot 2 - Blur
Pot 3 - Time Smear
Pot 4 - Wet / Dry Mix

LED:
  - Red: live spectral processing
  - Aqua: frozen spectrum
  - White flash: recaptured frozen frame

The effect repeats short FFT frames with overlap-add synthesis. In live mode the
current frame is continuously reanalysed; in freeze mode the held spectrum can be
warped, blurred, and smeared over time for evolving "torn" textures. The left and
right outputs share the same captured spectrum, but each side applies a slightly
different warp/blur/smear contour for stereo width.
*/

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <string.h>

#include "pico/multicore.h"

//#define DEBUG
//#define MONITOR_CPU1

#define SAMPLERATE 44100

constexpr uint32_t CONTROL_UPDATE_MS = 5;
constexpr uint32_t BUTTON_HOLD_MS = 800;
constexpr uint32_t FLASH_MS = 120;
constexpr float OUTPUT_GAIN = 0.9f;

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S i2s(INPUT_PULLUP); // both input and output

struct SharedSettings {
  volatile uint32_t revision;
  volatile float warp;
  volatile float blur;
  volatile float smear;
  volatile float mix;
  volatile uint8_t freeze_enabled;
  volatile uint32_t capture_generation;
};

struct SharedMeter {
  volatile uint8_t vu;
};

SharedSettings settings = {
  0,
  0.5f,
  0.0f,
  0.0f,
  0.7f,
  0,
  0
};

SharedMeter meter = {LED_BRIGHT_0_25};

static inline float ClampUnit(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static inline float ClampAudio(float value) {
  if (value > 1.0f) return 1.0f;
  if (value < -1.0f) return -1.0f;
  return value;
}

static inline uint32_t ScaleColorByLevel(uint32_t color, uint8_t level) {
  uint8_t r = (color >> 16) & 0x1f;
  uint8_t g = (color >> 8) & 0x1f;
  uint8_t b = color & 0x1f;
  r = (uint8_t)((r * level) / 31);
  g = (uint8_t)((g * level) / 31);
  b = (uint8_t)((b * level) / 31);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

class SpectralFreezeTearEngine {
 public:
  static constexpr size_t kNumChannels = 2;
  static constexpr size_t kFftSize = 128;
  static constexpr size_t kHopSize = kFftSize / 2;
  static constexpr size_t kNumBins = (kFftSize / 2) + 1;
  static constexpr size_t kOlaSize = 1024;

  void Init() {
    memset(analysis_ring_, 0, sizeof(analysis_ring_));
    memset(fft_input_, 0, sizeof(fft_input_));
    memset(fft_real_, 0, sizeof(fft_real_));
    memset(fft_imag_, 0, sizeof(fft_imag_));
    memset(output_ring_, 0, sizeof(output_ring_));
    memset(dry_delay_, 0, sizeof(dry_delay_));
    memset(live_real_, 0, sizeof(live_real_));
    memset(live_imag_, 0, sizeof(live_imag_));
    memset(frozen_real_, 0, sizeof(frozen_real_));
    memset(frozen_imag_, 0, sizeof(frozen_imag_));
    memset(work_real_, 0, sizeof(work_real_));
    memset(work_imag_, 0, sizeof(work_imag_));
    memset(temp_real_, 0, sizeof(temp_real_));
    memset(temp_imag_, 0, sizeof(temp_imag_));
    memset(prev_real_, 0, sizeof(prev_real_));
    memset(prev_imag_, 0, sizeof(prev_imag_));

    analysis_write_ = 0;
    hop_counter_ = 0;
    output_read_ = 0;
    dry_index_ = 0;
    capture_pending_ = false;
    freeze_enabled_ = false;
    frozen_valid_ = false;
    for (size_t channel = 0; channel < kNumChannels; ++channel) previous_valid_[channel] = false;
    dc_x1_ = 0.0f;
    dc_y1_ = 0.0f;
    vu_ = LED_BRIGHT_0_25;

    for (size_t i = 0; i < kFftSize; ++i) {
      float phase = (2.0f * PI * (float)i) / (float)(kFftSize - 1);
      float hann = 0.5f - 0.5f * cosf(phase);
      window_[i] = sqrtf(hann);
    }

    for (size_t i = 0; i < (kFftSize / 2); ++i) {
      float phase = (2.0f * PI * (float)i) / (float)kFftSize;
      twiddle_cos_[i] = cosf(phase);
      twiddle_sin_[i] = sinf(phase);
    }
  }

  void SetWarp(float warp) { warp_ = ClampUnit(warp); }
  void SetBlur(float blur) { blur_ = ClampUnit(blur); }
  void SetSmear(float smear) { smear_ = ClampUnit(smear); }
  void SetMix(float mix) { mix_ = ClampUnit(mix); }
  void SetFreezeEnabled(bool enabled) { freeze_enabled_ = enabled; }
  void RequestCapture() { capture_pending_ = true; }
  uint8_t vu() const { return vu_; }

  void Process(float input, float* out_left, float* out_right) {
    float wet_left = output_ring_[0][output_read_];
    float wet_right = output_ring_[1][output_read_];
    output_ring_[0][output_read_] = 0.0f;
    output_ring_[1][output_read_] = 0.0f;

    float dry = dry_delay_[dry_index_];
    dry_delay_[dry_index_] = input;
    dry_index_ = (dry_index_ + 1) % kFftSize;

    output_read_ = (output_read_ + 1) % kOlaSize;

    analysis_ring_[analysis_write_] = DcBlock(input);
    analysis_write_ = (analysis_write_ + 1) % kFftSize;

    ++hop_counter_;
    if (hop_counter_ >= kHopSize) {
      hop_counter_ = 0;
      ProcessFrame();
    }

    float left = ClampAudio((((1.0f - mix_) * dry) + (mix_ * wet_left)) * OUTPUT_GAIN);
    float right = ClampAudio((((1.0f - mix_) * dry) + (mix_ * wet_right)) * OUTPUT_GAIN);

    float level = fmaxf(fabsf(left), fabsf(right)) * 48.0f;
    if (level < (float)LED_BRIGHT_0_25) level = (float)LED_BRIGHT_0_25;
    if (level > 31.0f) level = 31.0f;
    vu_ = (uint8_t)level;

    *out_left = left;
    *out_right = right;
  }

 private:
  static inline float MirrorIndex(float index, float max_index) {
    if (max_index <= 0.0f) return 0.0f;
    while (index < 0.0f || index > max_index) {
      if (index < 0.0f) index = -index;
      if (index > max_index) index = max_index - (index - max_index);
    }
    return index;
  }

  void SwapValues(float* a, float* b) {
    float tmp = *a;
    *a = *b;
    *b = tmp;
  }

  void Transform(bool inverse) {
    size_t j = 0;
    for (size_t i = 0; i < kFftSize; ++i) {
      if (i < j) {
        SwapValues(&fft_real_[i], &fft_real_[j]);
        SwapValues(&fft_imag_[i], &fft_imag_[j]);
      }

      size_t bit = kFftSize >> 1;
      while (bit && (j & bit)) {
        j ^= bit;
        bit >>= 1;
      }
      j |= bit;
    }

    for (size_t len = 2; len <= kFftSize; len <<= 1) {
      size_t half = len >> 1;
      size_t step = kFftSize / len;
      for (size_t base = 0; base < kFftSize; base += len) {
        for (size_t k = 0; k < half; ++k) {
          size_t twiddle = k * step;
          float wr = twiddle_cos_[twiddle];
          float wi = inverse ? twiddle_sin_[twiddle] : -twiddle_sin_[twiddle];

          size_t even = base + k;
          size_t odd = even + half;

          float tr = wr * fft_real_[odd] - wi * fft_imag_[odd];
          float ti = wr * fft_imag_[odd] + wi * fft_real_[odd];

          float ur = fft_real_[even];
          float ui = fft_imag_[even];

          fft_real_[even] = ur + tr;
          fft_imag_[even] = ui + ti;
          fft_real_[odd] = ur - tr;
          fft_imag_[odd] = ui - ti;
        }
      }
    }

    if (inverse) {
      float scale = 1.0f / (float)kFftSize;
      for (size_t i = 0; i < kFftSize; ++i) {
        fft_real_[i] *= scale;
        fft_imag_[i] *= scale;
      }
    }
  }

  void BuildUniqueSpectrum(float* real, float* imag) {
    for (size_t i = 0; i < kNumBins; ++i) {
      real[i] = fft_real_[i];
      imag[i] = fft_imag_[i];
    }
    imag[0] = 0.0f;
    imag[kNumBins - 1] = 0.0f;
  }

  void BuildHermitianSpectrum(const float* real, const float* imag) {
    fft_real_[0] = real[0];
    fft_imag_[0] = 0.0f;
    fft_real_[kFftSize / 2] = real[kNumBins - 1];
    fft_imag_[kFftSize / 2] = 0.0f;

    for (size_t bin = 1; bin < kNumBins - 1; ++bin) {
      fft_real_[bin] = real[bin];
      fft_imag_[bin] = imag[bin];
      fft_real_[kFftSize - bin] = real[bin];
      fft_imag_[kFftSize - bin] = -imag[bin];
    }
  }

  void SampleSpectrum(
      const float* src_real,
      const float* src_imag,
      float index,
      float* out_real,
      float* out_imag) {
    float max_index = (float)(kNumBins - 1);
    index = MirrorIndex(index, max_index);

    int lower = (int)index;
    int upper = lower + 1;
    if (upper >= (int)kNumBins) upper = (int)kNumBins - 1;
    float frac = index - (float)lower;

    *out_real = src_real[lower] + (src_real[upper] - src_real[lower]) * frac;
    *out_imag = src_imag[lower] + (src_imag[upper] - src_imag[lower]) * frac;
  }

  void WarpSpectrum(
      const float* src_real,
      const float* src_imag,
      float* dst_real,
      float* dst_imag,
      float warp) {
    float signed_warp = (warp - 0.5f) * 2.0f;
    float ratio = powf(2.0f, signed_warp * 3.0f);
    float tear = powf(fabsf(signed_warp), 0.8f);
    float max_index = (float)(kNumBins - 1);

    dst_real[0] = src_real[0];
    dst_imag[0] = 0.0f;
    dst_real[kNumBins - 1] = src_real[kNumBins - 1];
    dst_imag[kNumBins - 1] = 0.0f;

    for (size_t bin = 1; bin < kNumBins - 1; ++bin) {
      float normalized = (float)bin / max_index;
      float source_index = (float)bin / ratio;
      if (tear > 0.0005f) {
        float phase = normalized * (6.2831853f * (1.0f + tear * 5.0f));
        source_index += sinf(phase) * tear * (14.0f + 18.0f * normalized);
        source_index += cosf(phase * 0.5f + signed_warp * 2.4f) * tear * 6.0f * (1.0f - normalized);
      }
      SampleSpectrum(src_real, src_imag, source_index, &dst_real[bin], &dst_imag[bin]);
    }
  }

  void BlurSpectrum(float* real, float* imag, float* temp_real, float* temp_imag, float blur) {
    if (blur <= 0.0005f) return;

    float shaped_blur = powf(blur, 0.75f);
    float coefficient = 0.35f + shaped_blur * 1.15f;
    int passes = 1 + (int)(shaped_blur * 8.0f);

    for (int pass = 0; pass < passes; ++pass) {
      temp_real[0] = real[0];
      temp_imag[0] = 0.0f;
      temp_real[kNumBins - 1] = real[kNumBins - 1];
      temp_imag[kNumBins - 1] = 0.0f;

      float side = 0.25f * coefficient;
      float center = 1.0f - (0.5f * coefficient);

      for (size_t bin = 1; bin < kNumBins - 1; ++bin) {
        temp_real[bin] =
            real[bin] * center +
            (real[bin - 1] + real[bin + 1]) * side;
        temp_imag[bin] =
            imag[bin] * center +
            (imag[bin - 1] + imag[bin + 1]) * side;
      }

      memcpy(real, temp_real, kNumBins * sizeof(float));
      memcpy(imag, temp_imag, kNumBins * sizeof(float));
    }
  }

  void ApplySmear(
      size_t channel,
      float* real,
      float* imag,
      bool reset_previous,
      float smear) {
    float hold = powf(smear, 1.25f) * 0.9994f;
    float fresh = 1.0f - hold;

    if (reset_previous || !previous_valid_[channel] || hold <= 0.0001f) {
      memcpy(prev_real_[channel], real, kNumBins * sizeof(float));
      memcpy(prev_imag_[channel], imag, kNumBins * sizeof(float));
      previous_valid_[channel] = true;
      return;
    }

    for (size_t bin = 0; bin < kNumBins; ++bin) {
      prev_real_[channel][bin] = prev_real_[channel][bin] * hold + real[bin] * fresh;
      prev_imag_[channel][bin] = prev_imag_[channel][bin] * hold + imag[bin] * fresh;
    }

    memcpy(real, prev_real_[channel], kNumBins * sizeof(float));
    memcpy(imag, prev_imag_[channel], kNumBins * sizeof(float));
    imag[0] = 0.0f;
    imag[kNumBins - 1] = 0.0f;
  }

  void GetStereoParams(size_t channel, float* warp, float* blur, float* smear) {
    float direction = channel == 0 ? -1.0f : 1.0f;

    constexpr float kWarpSpread = 0.08f;
    constexpr float kBlurSpread = 0.06f;
    constexpr float kSmearSpread = 0.07f;

    *warp = ClampUnit(warp_ + direction * kWarpSpread);
    *blur = ClampUnit(blur_ + direction * kBlurSpread);
    *smear = ClampUnit(smear_ - direction * kSmearSpread);
  }

  void RenderChannel(size_t channel, const float* src_real, const float* src_imag, bool reset_previous) {
    float channel_warp, channel_blur, channel_smear;
    GetStereoParams(channel, &channel_warp, &channel_blur, &channel_smear);

    WarpSpectrum(
        src_real,
        src_imag,
        work_real_[channel],
        work_imag_[channel],
        channel_warp);
    BlurSpectrum(
        work_real_[channel],
        work_imag_[channel],
        temp_real_[channel],
        temp_imag_[channel],
        channel_blur);
    ApplySmear(
        channel,
        work_real_[channel],
        work_imag_[channel],
        reset_previous,
        channel_smear);

    BuildHermitianSpectrum(work_real_[channel], work_imag_[channel]);
    Transform(true);

    size_t start = (output_read_ + kFftSize - 1) % kOlaSize;
    for (size_t i = 0; i < kFftSize; ++i) {
      size_t position = (start + i) % kOlaSize;
      output_ring_[channel][position] += fft_real_[i] * window_[i];
    }
  }

  void ProcessFrame() {
    for (size_t i = 0; i < kFftSize; ++i) {
      size_t index = (analysis_write_ + i) % kFftSize;
      fft_input_[i] = analysis_ring_[index] * window_[i];
      fft_real_[i] = fft_input_[i];
      fft_imag_[i] = 0.0f;
    }

    Transform(false);
    BuildUniqueSpectrum(live_real_, live_imag_);

    bool reset_previous = false;
    if (capture_pending_ || (freeze_enabled_ && !frozen_valid_)) {
      memcpy(frozen_real_, live_real_, sizeof(live_real_));
      memcpy(frozen_imag_, live_imag_, sizeof(live_imag_));
      frozen_imag_[0] = 0.0f;
      frozen_imag_[kNumBins - 1] = 0.0f;
      capture_pending_ = false;
      frozen_valid_ = true;
      reset_previous = true;
    }

    const float* src_real = (freeze_enabled_ && frozen_valid_) ? frozen_real_ : live_real_;
    const float* src_imag = (freeze_enabled_ && frozen_valid_) ? frozen_imag_ : live_imag_;

    for (size_t channel = 0; channel < kNumChannels; ++channel) {
      RenderChannel(channel, src_real, src_imag, reset_previous);
    }
  }

  float DcBlock(float input) {
    float output = input - dc_x1_ + 0.995f * dc_y1_;
    dc_x1_ = input;
    dc_y1_ = output;
    return output;
  }

  float window_[kFftSize];
  float twiddle_cos_[kFftSize / 2];
  float twiddle_sin_[kFftSize / 2];
  float analysis_ring_[kFftSize];
  float fft_input_[kFftSize];
  float fft_real_[kFftSize];
  float fft_imag_[kFftSize];
  float output_ring_[kNumChannels][kOlaSize];
  float dry_delay_[kFftSize];

  float live_real_[kNumBins];
  float live_imag_[kNumBins];
  float frozen_real_[kNumBins];
  float frozen_imag_[kNumBins];
  float work_real_[kNumChannels][kNumBins];
  float work_imag_[kNumChannels][kNumBins];
  float temp_real_[kNumChannels][kNumBins];
  float temp_imag_[kNumChannels][kNumBins];
  float prev_real_[kNumChannels][kNumBins];
  float prev_imag_[kNumChannels][kNumBins];

  size_t analysis_write_ = 0;
  size_t hop_counter_ = 0;
  size_t output_read_ = 0;
  size_t dry_index_ = 0;

  float warp_ = 0.5f;
  float blur_ = 0.0f;
  float smear_ = 0.0f;
  float mix_ = 0.7f;
  float dc_x1_ = 0.0f;
  float dc_y1_ = 0.0f;

  bool capture_pending_ = false;
  bool freeze_enabled_ = false;
  bool frozen_valid_ = false;
  bool previous_valid_[kNumChannels];

  uint8_t vu_ = LED_BRIGHT_0_25;
};

SpectralFreezeTearEngine engine;

bool freeze_enabled = false;
bool button_down = false;
bool long_press_handled = false;
uint32_t buttontimer = 0;
uint32_t buttonpress = 0;
uint32_t controlupdate = 0;
uint32_t flash_until = 0;

static inline float PotToUnit(uint16_t value) {
  return (float)value / (float)(AD_RANGE - 1);
}

void PublishSettings() {
  ++settings.revision;
}

void TriggerFreezeCapture() {
  ++settings.capture_generation;
  PublishSettings();
  flash_until = millis() + FLASH_MS;
}

void ToggleFreeze() {
  freeze_enabled = !freeze_enabled;
  settings.freeze_enabled = freeze_enabled ? 1 : 0;
  if (freeze_enabled) TriggerFreezeCapture();
  else PublishSettings();
}

void UpdateControls() {
  if ((millis() - controlupdate) < CONTROL_UPDATE_MS) return;
  controlupdate = millis();

  samplepots();

  settings.warp = PotToUnit(pot[0]);
  settings.blur = PotToUnit(pot[1]);
  settings.smear = PotToUnit(pot[2]);
  settings.mix = PotToUnit(pot[3]);
  PublishSettings();
}

void UpdateButton() {
  bool pressed = !digitalRead(BUTTON1);
  uint32_t now = millis();

  if (pressed) {
    if (((now - buttontimer) > DEBOUNCE) && !button_down) {
      button_down = true;
      long_press_handled = false;
      buttonpress = now;
    }

    if (button_down && freeze_enabled && !long_press_handled && ((now - buttonpress) > BUTTON_HOLD_MS)) {
      long_press_handled = true;
      TriggerFreezeCapture();
    }
  } else {
    if (button_down) {
      if (!long_press_handled) ToggleFreeze();
      buttontimer = now;
      button_down = false;
    } else {
      buttontimer = now;
    }
  }
}

void UpdateLed() {
  uint32_t color;
  if (millis() < flash_until) color = WHITE;
  else color = freeze_enabled ? AQUA : RED;

  LEDS.setPixelColor(0, ScaleColorByLevel(color, meter.vu));
  LEDS.show();
}

void setup() {
  Serial.begin(115200);

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

  LEDS.begin();
  LEDS.setPixelColor(0, RED);
  LEDS.show();

  analogReadResolution(AD_BITS);
  samplepots();

  settings.warp = PotToUnit(pot[0]);
  settings.blur = PotToUnit(pot[1]);
  settings.smear = PotToUnit(pot[2]);
  settings.mix = PotToUnit(pot[3]);
  settings.freeze_enabled = 0;
  PublishSettings();

  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK);
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();
}

void loop() {
  UpdateButton();
  UpdateControls();
  UpdateLed();
}

void setup1() {
  delay(1000);
  engine.Init();
  engine.SetWarp(settings.warp);
  engine.SetBlur(settings.blur);
  engine.SetSmear(settings.smear);
  engine.SetMix(settings.mix);
}

void loop1() {
  static uint32_t last_revision = 0;
  static uint32_t last_capture_generation = 0;
  int32_t left, right;
  float out_left, out_right;

  left = i2s.read();
  right = i2s.read();

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1);
#endif

  uint32_t revision = settings.revision;
  if (revision != last_revision) {
    engine.SetWarp(settings.warp);
    engine.SetBlur(settings.blur);
    engine.SetSmear(settings.smear);
    engine.SetMix(settings.mix);
    engine.SetFreezeEnabled(settings.freeze_enabled != 0);
    last_revision = revision;
  }

  if (settings.capture_generation != last_capture_generation) {
    engine.RequestCapture();
    last_capture_generation = settings.capture_generation;
  }

  float input = left * DIV_16;
  engine.Process(input, &out_left, &out_right);

  meter.vu = engine.vu();

  left = (int32_t)(out_left * MULT_16);
  right = (int32_t)(out_right * MULT_16);

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0);
#endif

  i2s.write(left);
  i2s.write(right);
}
