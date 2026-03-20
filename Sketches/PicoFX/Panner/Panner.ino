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
Spreader example for 2HPico DSP hardware
Wenhao Yang  Mar 2026

Top Jack - Audio input

Middle jack - Right Audio out (solder the DAC jumper on the back of the PCB)

Bottom Jack - Left Audio out

Top pot - Width

Second pot - Rate

Third pot - Delay
  full CCW  = triangle LFO
  full CW   = square LFO

Fourth pot - Output level

*/

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

#define SAMPLERATE 96000  // lightweight processing, runs comfortably on RP2350

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S i2s(INPUT_PULLUP); // both input and output

float samplerate = SAMPLERATE;

constexpr float SPREADER_MIN_RATE_HZ = 0.05f;
constexpr float SPREADER_MAX_RATE_HZ = 100.0f;
constexpr float SPREADER_DEFAULT_WIDTH = 0.75f;
constexpr float SPREADER_DEFAULT_RATE_HZ = 1.2f;
constexpr float SPREADER_DEFAULT_DELAY = 0.25f;
constexpr float SPREADER_DEFAULT_LEVEL = 0.9f;
constexpr float SPREADER_PARAM_SMOOTH = 0.0015f;
constexpr uint8_t SPREADER_CONTROL_POLL_DIVIDER = 32;
constexpr float SPREADER_VU_ATTACK = 0.02f;
constexpr float SPREADER_VU_DECAY = 0.0008f;

class ShapeMorphLfo
{
public:
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    phase_ = 0.0f;
    phase_inc_ = 0.0f;
    SetRateHz(SPREADER_DEFAULT_RATE_HZ);
  }

  void SetRateHz(float hz) {
    if (hz < SPREADER_MIN_RATE_HZ) hz = SPREADER_MIN_RATE_HZ;
    if (hz > SPREADER_MAX_RATE_HZ) hz = SPREADER_MAX_RATE_HZ;
    phase_inc_ = hz / sample_rate_;
  }

  float Process(float shape) {
    phase_ += phase_inc_;

    while (phase_ >= 1.0f) phase_ -= 1.0f;

    if (shape < 0.0f) shape = 0.0f;
    if (shape > 1.0f) shape = 1.0f;

    float triangle = 1.0f - 4.0f * fabsf(phase_ - 0.5f);
    float square = (phase_ < 0.5f) ? 1.0f : -1.0f;

    return triangle + (square - triangle) * shape;
  }

private:
  float sample_rate_;
  float phase_;
  float phase_inc_;
};

struct SharedSettings {
  volatile uint32_t revision;
  volatile float width;
  volatile float rate_hz;
  volatile float delay;
  volatile float level;
};

struct SharedLedState {
  volatile uint32_t revision;
  volatile float vu;
  volatile float pan;
};

SharedSettings settings = {0, SPREADER_DEFAULT_WIDTH, SPREADER_DEFAULT_RATE_HZ, SPREADER_DEFAULT_DELAY, SPREADER_DEFAULT_LEVEL};
SharedLedState ledstate = {0, 0.0f, 0.0f};

ShapeMorphLfo spreadlfo;

#define NUMUISTATES 1
enum UIstates {SPREADER};
uint8_t UIstate = SPREADER;

bool button = 0;
uint32_t buttontimer, parameterupdate;

float MapRateHz(uint16_t value) {
  float normalized = mapf(value, 0, AD_RANGE - 1, 0.0f, 1.0f);
  return SPREADER_MIN_RATE_HZ * expf(logf(SPREADER_MAX_RATE_HZ / SPREADER_MIN_RATE_HZ) * normalized);
}

void PublishSettings(float width, float rate_hz, float delay, float level) {
  ++settings.revision; // odd means update in progress
  settings.width = width;
  settings.rate_hz = rate_hz;
  settings.delay = delay;
  settings.level = level;
  ++settings.revision; // even means stable snapshot
}

void PublishLedState(float vu, float pan) {
  ++ledstate.revision; // odd means update in progress
  ledstate.vu = vu;
  ledstate.pan = pan;
  ++ledstate.revision; // even means stable snapshot
}

uint32_t ScaleLedColor(uint32_t color, float brightness) {
  if (brightness < 0.0f) brightness = 0.0f;
  if (brightness > 1.0f) brightness = 1.0f;

  uint8_t r = (uint8_t)((((color >> 16) & 0x1f) * brightness) + 0.5f);
  uint8_t g = (uint8_t)((((color >> 8) & 0x1f) * brightness) + 0.5f);
  uint8_t b = (uint8_t)((((color >> 0) & 0x1f) * brightness) + 0.5f);

  return RGB(r, g, b);
}

void setup() {
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println("starting setup");
#endif

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT); // hi = CPU busy
#endif

  pinMode(BUTTON1, INPUT_PULLUP); // button in
  pinMode(MUXCTL, OUTPUT);        // analog switch mux

  LEDS.begin();
  LEDS.setPixelColor(0, RED);
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

  PublishSettings(settings.width, settings.rate_hz, settings.delay, settings.level);

#ifdef DEBUG
  Serial.println("finished setup");
#endif
}


void loop() {
  uint32_t now = millis();
  uint32_t ledrevision;
  float ledvu, ledpan;

// select UI page - only one page in this sketch
  if (!digitalRead(BUTTON1)) {
    if (((now - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      ++UIstate;
      if (UIstate >= NUMUISTATES) UIstate = SPREADER;
      lockpots();
    }
  }
  else {
    buttontimer = now;
    button = 0;
  }

  if ((now - parameterupdate) > PARAMETERUPDATE) {
    parameterupdate = now;
    samplepots();

    switch (UIstate) {
      case SPREADER: {
        float width = settings.width;
        float rate_hz = settings.rate_hz;
        float delay = settings.delay;
        float level = settings.level;

        if (!potlock[0]) width = mapf(pot[0], 0, AD_RANGE - 1, 0.0f, 1.0f);
        if (!potlock[1]) rate_hz = MapRateHz(pot[1]);
        if (!potlock[2]) delay = mapf(pot[2], 0, AD_RANGE - 1, 0.0f, 1.0f);
        if (!potlock[3]) level = mapf(pot[3], 0, AD_RANGE - 1, 0.0f, 1.0f);

        PublishSettings(width, rate_hz, delay, level);
        break;
      }
      default:
        break;
    }
  }

  do {
    ledrevision = ledstate.revision;
    while (ledrevision & 1u) ledrevision = ledstate.revision;
    ledvu = ledstate.vu;
    ledpan = ledstate.pan;
  } while (ledrevision != ledstate.revision);

  float brightness = sqrtf(fminf(1.0f, ledvu * 4.0f));
  LEDS.setPixelColor(0, ScaleLedColor((ledpan <= 0.0f) ? AQUA : ORANGE, brightness));
  LEDS.show();
}

// second core setup
// second core is dedicated to sample processing
void setup1() {
  delay(1000); // wait for main core to start up peripherals
  spreadlfo.Init(samplerate);
}

// process audio samples
void loop1() {
  static uint8_t control_divider = 0;
  static uint32_t applied_revision = 0xffffffffu;
  static float width = SPREADER_DEFAULT_WIDTH;
  static float target_width = SPREADER_DEFAULT_WIDTH;
  static float lfoshape = SPREADER_DEFAULT_DELAY;
  static float target_lfoshape = SPREADER_DEFAULT_DELAY;
  static float output_level = SPREADER_DEFAULT_LEVEL;
  static float target_output_level = SPREADER_DEFAULT_LEVEL;

  float sig;
  float pan;
  float base_gain;
  float side_gain;
  float outL;
  float outR;
  float levelabs;
  static float vu = 0.0f;
  int32_t left, right;

  left = i2s.read();   // input is mono but we still have to read both channels
  right = i2s.read();  // second slot is discarded

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // hi = CPU busy
#endif

  if (++control_divider >= SPREADER_CONTROL_POLL_DIVIDER) {
    control_divider = 0;

    uint32_t revision;
    float new_width, rate_hz, new_delay, new_level;

    do {
      revision = settings.revision;
      while (revision & 1u) revision = settings.revision;
      new_width = settings.width;
      rate_hz = settings.rate_hz;
      new_delay = settings.delay;
      new_level = settings.level;
    } while (revision != settings.revision);

    if (revision != applied_revision) {
      spreadlfo.SetRateHz(rate_hz);
      target_width = new_width;
      target_lfoshape = new_delay;
      target_output_level = new_level;
      applied_revision = revision;
    }
  }

  width += (target_width - width) * SPREADER_PARAM_SMOOTH;
  lfoshape += (target_lfoshape - lfoshape) * SPREADER_PARAM_SMOOTH;
  output_level += (target_output_level - output_level) * SPREADER_PARAM_SMOOTH;

  sig = left * DIV_16;
  pan = spreadlfo.Process(lfoshape) * width;
  levelabs = fabsf(sig);

  if (levelabs > vu) vu += (levelabs - vu) * SPREADER_VU_ATTACK;
  else vu += (levelabs - vu) * SPREADER_VU_DECAY;

  base_gain = 1.0f - width * 0.5f;
  side_gain = pan * 0.5f;

  outL = sig * (base_gain - side_gain) * output_level;
  outR = sig * (base_gain + side_gain) * output_level;

  if (!control_divider) {
    PublishLedState(vu, pan);
  }

  left = (int32_t)(outL * MULT_16);
  right = (int32_t)(outR * MULT_16);

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0); // low - CPU not busy
#endif

  i2s.write(left);  // left output -> bottom jack
  i2s.write(right); // right output -> middle jack
}
