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
Sidechain ducking example for 2HPico DSP hardware
W Yang / R Heslip  Mar 2026

Lightweight trigger ducking sidechain for RP2350:
- no DaisySP dependency
- trigger edge detection runs on the audio core
- no per-sample powf()/expf() in the audio path
- mono output mirrored to both DAC channels

Top Jack - Audio input

Middle jack - Trigger input (solder the CV jumper on the back of the 2HPico DSP PCB)

Bottom Jack - Audio out

First Parameter Page - RED

Top pot - Attack

Second pot - Decay

Third pot - Knee
  full CCW  = logarithmic response
  noon      = linear response
  full CW   = exponential response

Fourth pot - Output level

*/

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

//#define SAMPLERATE 44100
#define SAMPLERATE 96000  // very light processing, runs comfortably on RP2350

#define SIDECHAIN_TRIGGER_IN AIN1

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S i2s(INPUT_PULLUP); // both input and output

float samplerate = SAMPLERATE;

constexpr float SIDECHAIN_MIN_ATTACK_MS = 1.0f;
constexpr float SIDECHAIN_MAX_ATTACK_MS = 250.0f;
constexpr float SIDECHAIN_MIN_DECAY_MS = 10.0f;
constexpr float SIDECHAIN_MAX_DECAY_MS = 2000.0f;
constexpr float SIDECHAIN_MIN_GAIN = 0.10f; // about -20 dB at peak ducking
constexpr uint8_t TRIGGER_POLL_DIVIDER = 16;
constexpr float LED_METER_ATTACK = 0.2f;
constexpr float LED_METER_RELEASE = 0.01f;

class TriggerDucker
{
public:
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    stage_ = IDLE;
    progress_ = 1.0f;
    current_duck_ = 0.0f;
    segment_start_ = 0.0f;
    segment_end_ = 0.0f;
    attack_inc_ = 1.0f;
    decay_inc_ = 1.0f;
    curve_ = 0.5f;
    depth_ = 1.0f - SIDECHAIN_MIN_GAIN;
    SetAttackMs(20.0f);
    SetDecayMs(250.0f);
    SetCurve(0.5f);
  }

  void SetAttackMs(float ms) {
    attack_inc_ = 1.0f / TimeToSamples(ms);
  }

  void SetDecayMs(float ms) {
    decay_inc_ = 1.0f / TimeToSamples(ms);
  }

  void SetCurve(float curve) {
    if (curve < 0.0f) curve = 0.0f;
    if (curve > 1.0f) curve = 1.0f;
    curve_ = curve;
  }

  void Trigger() {
    StartSegment(current_duck_, 1.0f, attack_inc_, ATTACK);
  }

  float Process(float in) {
    AdvanceEnvelope();
    float gain = 1.0f - current_duck_ * depth_;
    return in * gain;
  }

  float GetDuckAmount() const {
    return current_duck_;
  }

private:
  enum EnvelopeStage : uint8_t {
    IDLE = 0,
    ATTACK,
    DECAY
  };

  float TimeToSamples(float ms) const {
    if (ms < 0.1f) ms = 0.1f;
    float samples = ms * 0.001f * sample_rate_;
    if (samples < 1.0f) samples = 1.0f;
    return samples;
  }

  float Shape(float x) const {
    float logshape = x * (2.0f - x); // fast start, slow finish
    float expshape = x * x;          // slow start, fast finish

    if (curve_ < 0.5f) {
      float morph = curve_ * 2.0f;
      return logshape + (x - logshape) * morph;
    }

    float morph = (curve_ - 0.5f) * 2.0f;
    return x + (expshape - x) * morph;
  }

  void StartSegment(float start, float end, float increment, EnvelopeStage stage) {
    segment_start_ = start;
    segment_end_ = end;
    progress_ = 0.0f;
    increment_ = increment;
    stage_ = stage;
    current_duck_ = start;
  }

  void AdvanceEnvelope() {
    if (stage_ == IDLE) return;

    progress_ += increment_;

    if (progress_ >= 1.0f) {
      current_duck_ = segment_end_;

      if (stage_ == ATTACK) {
        StartSegment(current_duck_, 0.0f, decay_inc_, DECAY);
      }
      else {
        stage_ = IDLE;
        current_duck_ = 0.0f;
      }
      return;
    }

    float shaped = Shape(progress_);
    current_duck_ = segment_start_ + (segment_end_ - segment_start_) * shaped;
  }

  float sample_rate_;
  float attack_inc_;
  float decay_inc_;
  float increment_;
  float progress_;
  float segment_start_;
  float segment_end_;
  float current_duck_;
  float curve_;
  float depth_;
  EnvelopeStage stage_;
};

struct SharedSettings {
  volatile uint32_t revision;
  volatile float attack_ms;
  volatile float decay_ms;
  volatile float knee;
  volatile float level;
};

struct SharedMeter {
  volatile uint8_t output_level;
  volatile uint8_t duck_amount;
};

SharedSettings settings = {0, 20.0f, 250.0f, 0.5f, 0.85f};
SharedMeter meter = {0, 0};

TriggerDucker ducker;

#define NUMUISTATES 1
enum UIstates {SIDECHAIN};
uint8_t UIstate = SIDECHAIN;

bool button = 0;
uint32_t buttontimer, parameterupdate;

float MapResponseTime(uint16_t value, float min_ms, float max_ms) {
  float normalized = mapf(value, 0, AD_RANGE - 1, 0.0f, 1.0f);
  return min_ms * expf(logf(max_ms / min_ms) * normalized);
}

void PublishSettings(float attack_ms, float decay_ms, float knee, float level) {
  ++settings.revision; // odd means update in progress
  settings.attack_ms = attack_ms;
  settings.decay_ms = decay_ms;
  settings.knee = knee;
  settings.level = level;
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

  pinMode(BUTTON1, INPUT_PULLUP); // button in
  pinMode(MUXCTL, OUTPUT);        // analog switch mux
  pinMode(SIDECHAIN_TRIGGER_IN, INPUT_PULLUP);

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

  PublishSettings(settings.attack_ms, settings.decay_ms, settings.knee, settings.level);

#ifdef DEBUG
  Serial.println("finished setup");
#endif
}


void loop() {
  uint32_t now = millis();

  if (!digitalRead(BUTTON1)) {
    if (((now - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      ++UIstate;
      if (UIstate >= NUMUISTATES) UIstate = SIDECHAIN;
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
      case SIDECHAIN: {
        float attack_ms = settings.attack_ms;
        float decay_ms = settings.decay_ms;
        float knee = settings.knee;
        float level = settings.level;

        if (!potlock[0]) attack_ms = MapResponseTime(pot[0], SIDECHAIN_MIN_ATTACK_MS, SIDECHAIN_MAX_ATTACK_MS);
        if (!potlock[1]) decay_ms = MapResponseTime(pot[1], SIDECHAIN_MIN_DECAY_MS, SIDECHAIN_MAX_DECAY_MS);
        if (!potlock[2]) knee = mapf(pot[2], 0, AD_RANGE - 1, 0.0f, 1.0f);
        if (!potlock[3]) level = mapf(pot[3], 0, AD_RANGE - 1, 0.0f, 1.0f);

        PublishSettings(attack_ms, decay_ms, knee, level);
        break;
      }
      default:
        break;
    }
  }

  uint8_t level_led = meter.output_level;
  uint8_t duck_led = meter.duck_amount;
  uint8_t red = duck_led;
  uint8_t green = level_led > duck_led ? (uint8_t)(level_led - duck_led) : 0;
  uint8_t blue = 0;

  LEDS.setPixelColor(0, RGB(red, green, blue));
  LEDS.show();
}

// second core setup
// second core is dedicated to sample processing
void setup1() {
  delay(1000); // wait for main core to start up peripherals
  ducker.Init(samplerate);
}

// process audio samples
void loop1() {
  static uint8_t trigger_divider = 0;
  static bool trigger_state = false;
  static uint32_t applied_revision = 0xffffffffu;
  static float output_level = 0.85f;
  static float level_env = 0.0f;

  float sig, abs_sig, duck_amount;
  int32_t left, right;

  left = i2s.read();   // input is mono but we still have to read both channels
  right = i2s.read();  // second slot is discarded

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // hi = CPU busy
#endif

  if (++trigger_divider >= TRIGGER_POLL_DIVIDER) {
    trigger_divider = 0;

    uint32_t revision;
    float attack_ms, decay_ms, knee, level;

    do {
      revision = settings.revision;
      while (revision & 1u) revision = settings.revision;
      attack_ms = settings.attack_ms;
      decay_ms = settings.decay_ms;
      knee = settings.knee;
      level = settings.level;
    } while (revision != settings.revision);

    if (revision != applied_revision) {
      ducker.SetAttackMs(attack_ms);
      ducker.SetDecayMs(decay_ms);
      ducker.SetCurve(knee);
      applied_revision = revision;
    }

    output_level += (level - output_level) * 0.1f;

    bool raw_trigger = !digitalRead(SIDECHAIN_TRIGGER_IN); // middle jack CV path is treated as active-low here
    if (raw_trigger && !trigger_state) {
      ducker.Trigger();
    }
    trigger_state = raw_trigger;
  }

  sig = left * DIV_16;
  sig = ducker.Process(sig) * output_level;
  duck_amount = ducker.GetDuckAmount();

  abs_sig = fabsf(sig);
  if (abs_sig > level_env) {
    level_env += (abs_sig - level_env) * LED_METER_ATTACK;
  }
  else {
    level_env += (abs_sig - level_env) * LED_METER_RELEASE;
  }

  if (level_env > 1.0f) level_env = 1.0f;
  meter.output_level = (uint8_t)(level_env * LED_BRIGHT_1);
  meter.duck_amount = (uint8_t)(duck_amount * LED_BRIGHT_1);

  left = (int32_t)(sig * MULT_16);
  right = (int32_t)(sig * MULT_16);

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0); // low - CPU not busy
#endif

  i2s.write(left);
  i2s.write(right);
}
