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
Bitcrush for 2HPico DSP hardware
Aligned with the current PicoFX style and optimized for RP2350.

Top jack - Audio input

Middle jack - Mix CV input (solder the CV jumper on the back of the 2HPico DSP PCB)

Bottom jack - Audio out

Button - switch parameter page

Page 1 parameters - GREEN LED

Top pot - Resolution (1 bit to 16 bit)

Second pot - DownSampling (1x to 20x)

Third pot - Mix

Fourth pot - Output level

RP2350 optimizations:
- core 0 handles button, pots, and Mix CV sampling
- core 1 only performs integer bit reduction, sample-hold downsampling, and fixed-point wet/dry mixing
- control settings are published with a revisioned shared struct so the audio path stays lock-free
*/

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>

#include "pico/multicore.h"

//#define DEBUG
//#define MONITOR_CPU1

#define SAMPLERATE 96000  // lightweight integer DSP, runs comfortably on RP2350

constexpr uint32_t BITCRUSH_CONTROL_UPDATE_MS = 5;
constexpr uint8_t BITCRUSH_AUDIO_CONTROL_DIVIDER = 32;
constexpr uint8_t BITCRUSH_DEFAULT_BITS = 12;
constexpr uint8_t BITCRUSH_DEFAULT_DOWNSAMPLE = 4;
constexpr uint16_t BITCRUSH_Q16_MAX = 65535u;
constexpr uint16_t BITCRUSH_DEFAULT_MIX_Q16 = BITCRUSH_Q16_MAX / 2u;
constexpr uint16_t BITCRUSH_DEFAULT_LEVEL_Q16 = (BITCRUSH_Q16_MAX * 9u) / 10u;
constexpr uint8_t BITCRUSH_LED_FLOOR = LED_BRIGHT_0_25;
constexpr uint8_t BITCRUSH_SMOOTH_SHIFT = 4;

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S i2s(INPUT_PULLUP); // both input and output

struct SharedSettings {
  volatile uint32_t revision;
  volatile uint8_t bit_depth;
  volatile uint8_t downsample_factor;
  volatile uint16_t mix_q16;
  volatile uint16_t level_q16;
};

struct SharedMeter {
  volatile uint8_t vu;
};

SharedSettings settings = {
  0,
  BITCRUSH_DEFAULT_BITS,
  BITCRUSH_DEFAULT_DOWNSAMPLE,
  BITCRUSH_DEFAULT_MIX_Q16,
  BITCRUSH_DEFAULT_LEVEL_Q16
};

SharedMeter meter = {BITCRUSH_LED_FLOOR};

uint8_t panel_bit_depth = BITCRUSH_DEFAULT_BITS;
uint8_t panel_downsample = BITCRUSH_DEFAULT_DOWNSAMPLE;
uint16_t panel_mix_q16 = BITCRUSH_DEFAULT_MIX_Q16;
uint16_t panel_level_q16 = BITCRUSH_DEFAULT_LEVEL_Q16;

#define NUMUISTATES 1
enum UIstates {BITCRUSH};
uint8_t UIstate = BITCRUSH;

bool button = 0;
uint32_t buttontimer = 0;
uint32_t controlupdate = 0;

static inline uint16_t PotToQ16(uint16_t value) {
  return (uint32_t)value * BITCRUSH_Q16_MAX / (AD_RANGE - 1);
}

static inline uint8_t PotToBitDepth(uint16_t value) {
  return 1u + ((uint32_t)value * 16u) / AD_RANGE;
}

static inline uint8_t PotToDownsampleFactor(uint16_t value) {
  return 1u + ((uint32_t)value * 20u) / AD_RANGE;
}

static inline uint16_t CvToMixQ16(uint16_t value) {
  uint16_t inverted = (AD_RANGE - 1u) - value; // the middle jack CV path is inverted on this hardware
  return (uint32_t)inverted * BITCRUSH_Q16_MAX / (AD_RANGE - 1u);
}

static inline uint16_t ClampQ16Sum(uint16_t a, uint16_t b) {
  uint32_t sum = (uint32_t)a + b;
  if (sum > BITCRUSH_Q16_MAX) return BITCRUSH_Q16_MAX;
  return (uint16_t)sum;
}

static inline int32_t CrushSample(int32_t sample, uint8_t bit_depth) {
  int32_t sample16 = sample >> 16;
  uint8_t shift = 16u - bit_depth;

  if (shift > 0u) sample16 = (sample16 >> shift) << shift;

  return sample16 << 16;
}

void PublishSettings(uint8_t bit_depth, uint8_t downsample_factor, uint16_t mix_q16, uint16_t level_q16) {
  ++settings.revision; // odd means update in progress
  settings.bit_depth = bit_depth;
  settings.downsample_factor = downsample_factor;
  settings.mix_q16 = mix_q16;
  settings.level_q16 = level_q16;
  ++settings.revision; // even means stable snapshot
}

void setup() {
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println("starting setup");
#endif

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT); // hi = CPU busy
#endif

  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

  LEDS.begin();
  LEDS.setPixelColor(0, GREEN);
  LEDS.show();

  analogReadResolution(AD_BITS);

  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK); // Note: LRCLK = BCLK + 1
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();

  PublishSettings(panel_bit_depth, panel_downsample, panel_mix_q16, panel_level_q16);

#ifdef DEBUG
  Serial.println("finished setup");
#endif
}

void loop() {
  uint32_t now = millis();

// select UI page - only one page in this sketch
  if (!digitalRead(BUTTON1)) {
    if (((now - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      ++UIstate;
      if (UIstate >= NUMUISTATES) UIstate = BITCRUSH;
      lockpots();
    }
  }
  else {
    buttontimer = now;
    button = 0;
  }

  if ((now - controlupdate) >= BITCRUSH_CONTROL_UPDATE_MS) {
    controlupdate = now;
    samplepots();

    switch (UIstate) {
      case BITCRUSH:
        if (!potlock[0]) panel_bit_depth = PotToBitDepth(pot[0]);
        if (!potlock[1]) panel_downsample = PotToDownsampleFactor(pot[1]);
        if (!potlock[2]) panel_mix_q16 = PotToQ16(pot[2]);
        if (!potlock[3]) panel_level_q16 = PotToQ16(pot[3]);
        break;
      default:
        break;
    }

    PublishSettings(
      panel_bit_depth,
      panel_downsample,
      ClampQ16Sum(panel_mix_q16, CvToMixQ16(sampleCV2())),
      panel_level_q16
    );

    uint8_t ledvu = meter.vu;
    if (ledvu < BITCRUSH_LED_FLOOR) ledvu = BITCRUSH_LED_FLOOR;
    LEDS.setPixelColor(0, RGB(0, ledvu, 0));
    LEDS.show();
  }
}

// second core setup
// second core is dedicated to sample processing
void setup1() {
  delay(1000); // wait for main core to start up peripherals
}

// process audio samples
void loop1() {
  static uint8_t control_divider = 0;
  static uint8_t bit_depth = BITCRUSH_DEFAULT_BITS;
  static uint8_t downsample_factor = BITCRUSH_DEFAULT_DOWNSAMPLE;
  static uint8_t downsample_counter = 0;
  static uint32_t applied_revision = 0xffffffffu;
  static int32_t held_sample = 0;
  static int32_t target_mix_q16 = BITCRUSH_DEFAULT_MIX_Q16;
  static int32_t current_mix_q16 = BITCRUSH_DEFAULT_MIX_Q16;
  static int32_t target_level_q16 = BITCRUSH_DEFAULT_LEVEL_Q16;
  static int32_t current_level_q16 = BITCRUSH_DEFAULT_LEVEL_Q16;
  static uint8_t vu = BITCRUSH_LED_FLOOR;

  int32_t left;
  int32_t right;
  int32_t mixed;
  int32_t output;

  left = i2s.read();   // input is mono but we still have to read both channels
  right = i2s.read();  // second slot is discarded

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // hi = CPU busy
#endif

  if (++control_divider >= BITCRUSH_AUDIO_CONTROL_DIVIDER) {
    control_divider = 0;

    uint32_t revision;
    uint8_t new_bit_depth;
    uint8_t new_downsample_factor;
    uint16_t new_mix_q16;
    uint16_t new_level_q16;

    do {
      revision = settings.revision;
      while (revision & 1u) revision = settings.revision;
      new_bit_depth = settings.bit_depth;
      new_downsample_factor = settings.downsample_factor;
      new_mix_q16 = settings.mix_q16;
      new_level_q16 = settings.level_q16;
    } while (revision != settings.revision);

    if (revision != applied_revision) {
      bit_depth = new_bit_depth;
      downsample_factor = new_downsample_factor;
      target_mix_q16 = new_mix_q16;
      target_level_q16 = new_level_q16;

      if (downsample_counter >= downsample_factor) downsample_counter = 0;

      applied_revision = revision;
    }

    current_mix_q16 += (target_mix_q16 - current_mix_q16) >> BITCRUSH_SMOOTH_SHIFT;
    current_level_q16 += (target_level_q16 - current_level_q16) >> BITCRUSH_SMOOTH_SHIFT;
  }

  if (downsample_counter == 0u) {
    held_sample = CrushSample(left, bit_depth);
    downsample_counter = downsample_factor;
  }
  --downsample_counter;

  mixed = left + (int32_t)((((int64_t)held_sample - left) * current_mix_q16) >> 16);
  output = (int32_t)(((int64_t)mixed * current_level_q16) >> 16);

  {
    uint32_t abs_output = (output < 0) ? (uint32_t)(-((int64_t)output)) : (uint32_t)output;
    uint8_t target_vu = abs_output >> 26;

    if (target_vu > LED_BRIGHT_1) target_vu = LED_BRIGHT_1;

    if (target_vu > vu) vu += (uint8_t)(((uint16_t)(target_vu - vu) + 1u) >> 1);
    else if (target_vu < vu) vu -= (uint8_t)(((uint16_t)(vu - target_vu) + 7u) >> 3);

    if (!control_divider) meter.vu = vu;
  }

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0); // low - CPU not busy
#endif

  i2s.write(output);
  i2s.write(output);
}
