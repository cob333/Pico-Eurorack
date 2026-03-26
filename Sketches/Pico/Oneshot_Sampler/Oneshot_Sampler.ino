// Copyright 2026 Rich Heslip
//
// Author: Rich Heslip
// Contributor: OpenAI Codex
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
Oneshot sampler for the 2HPico Eurorack module
Aligned with the Grids_Sampler sketch style and RP2350 dual-core layout.

Top jack - trigger input - triggers on +ve edge
Middle jack - Volt/Octave CV input
Bottom jack - audio out

Button - switch parameter page

Page 1 parameters - GREEN LED
Pot 1 - Level
Pot 2 - Tone (lowpass brightness)
Pot 3 - Sample select
Pot 4 - Pitch

Page 2 parameters - AQUA LED
Pot 1 - Attack
Pot 2 - Decay (full CW = hold until sample end)
Pot 3 - Sample randomness
Pot 4 - Reverse (left = forward, right = reverse)

RP2350 optimizations:
- core 0 handles UI, trigger and CV sampling, and trigger setup
- core 1 only does linear interpolation, envelope, one-pole lowpass, and DAC writes
- control-rate LUTs avoid repeated exp/pow work in the realtime path
*/

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include "pico/multicore.h"

//#define DEBUG
//#define MONITOR_CPU1

#define SAMPLERATE 44100
#define CONTROL_UPDATE_MS 5
#define CVIN_VOLT 580.6f

#define PITCH_RANGE_SEMITONES 24.0f
#define ATTACK_MAX_SEC 0.30f
#define DECAY_MIN_SEC 0.01f
#define DECAY_MAX_SEC 3.00f
#define DECAY_HOLD_THRESHOLD 0.985f

#define TONE_MIN_HZ 250.0f
#define TONE_MAX_HZ 14000.0f

#define LUT_SIZE 256
#define SAMPLE_GAIN (1.0f / 32768.0f)

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);

#include "samples.h"
#define NUM_SAMPLES ((uint16_t)(sizeof(sample) / sizeof(sample_t)))

enum UIstates {PAGE_GREEN, PAGE_AQUA};
enum EnvState {ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN};

uint8_t UIstate = PAGE_GREEN;
bool button = 0;
bool triggered = 0;
uint32_t buttontimer = 0;
uint32_t trigtimer = 0;
uint32_t parameterupdate = 0;

float attack_lut[LUT_SIZE];
float decay_lut[LUT_SIZE];
float tone_coeff_lut[LUT_SIZE];

int16_t manual_sample = 0;
int16_t last_trigger_sample = 0;
float pitch_pot_semitones = 0.0f;
float voct_semitones = 0.0f;
float attack_time = 0.0f;
float decay_time = 0.40f;
float sample_randomness = 0.0f;
bool decay_hold = 0;
bool reverse_playback = 0;

volatile float live_gain = 1.0f;
volatile float live_tone_coeff = 1.0f;

const int16_t * volatile pending_sample_ptr = 0;
volatile uint32_t pending_sample_size = 0;
volatile float pending_increment = 1.0f;
volatile float pending_attack_inc = 1.0f;
volatile float pending_decay_dec = 0.0f;
volatile bool pending_hold = 1;
volatile bool pending_reverse = 0;
volatile uint32_t pending_trigger_serial = 0;

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline float adc_to_norm(uint16_t adc_val) {
  if (adc_val >= AD_RANGE) adc_val = AD_RANGE - 1;
  return (float)adc_val / (float)(AD_RANGE - 1);
}

static inline uint16_t adc_to_lut_index(uint16_t adc_val) {
  uint32_t idx = ((uint32_t)adc_val * (LUT_SIZE - 1)) / AD_RANGE;
  if (idx >= LUT_SIZE) idx = LUT_SIZE - 1;
  return (uint16_t)idx;
}

static inline float semitones_to_ratio(float semitones) {
  float ratio = exp2f(semitones / 12.0f);
  if (ratio < 0.125f) ratio = 0.125f;
  if (ratio > 8.0f) ratio = 8.0f;
  return ratio;
}

static void set_page_led(void) {
  if (UIstate == PAGE_GREEN) LEDS.setPixelColor(0, GREEN);
  else LEDS.setPixelColor(0, AQUA);
  LEDS.show();
}

static void init_lookup_tables(void) {
  const float tone_ratio = TONE_MAX_HZ / TONE_MIN_HZ;
  const float two_pi = 6.28318530717958647692f;

  for (uint16_t i = 0; i < LUT_SIZE; ++i) {
    float x = (float)i / (float)(LUT_SIZE - 1);
    float attack_shape = x * x;
    float tone_hz = TONE_MIN_HZ * powf(tone_ratio, x);
    float coeff = 1.0f - expf(-(two_pi * tone_hz) / (float)SAMPLERATE);

    attack_lut[i] = ATTACK_MAX_SEC * attack_shape;
    decay_lut[i] = DECAY_MIN_SEC * powf(DECAY_MAX_SEC / DECAY_MIN_SEC, x);
    tone_coeff_lut[i] = clamp01(coeff);
  }
}

static inline float read_voct_semitones(void) {
  float cv = (float)(AD_RANGE - sampleCV2());
  return (cv / CVIN_VOLT) * 12.0f;
}

static uint16_t choose_trigger_sample(void) {
  uint16_t chosen = (uint16_t)manual_sample;
  if ((NUM_SAMPLES <= 1) || (sample_randomness <= 0.0f)) return chosen;

  bool choose_random = (sample_randomness >= 0.999f);
  if (!choose_random) {
    uint16_t threshold = (uint16_t)(sample_randomness * 1000.0f);
    choose_random = ((uint16_t)random(0, 1000)) < threshold;
  }
  if (!choose_random) return chosen;

  if (NUM_SAMPLES == 2) {
    return (last_trigger_sample == 0) ? 1 : 0;
  }

  do {
    chosen = (uint16_t)random(0, NUM_SAMPLES);
  } while (chosen == (uint16_t)last_trigger_sample);

  return chosen;
}

static void trigger_voice(void) {
  uint16_t chosen = choose_trigger_sample();
  float semitones = pitch_pot_semitones + voct_semitones;
  float increment = semitones_to_ratio(semitones);
  float attack_inc = 1.0f;
  float decay_dec = 0.0f;
  const sample_t *selected_sample = &sample[chosen];

  if (selected_sample->samplesize < 2) return;

  if (attack_time > 0.0005f) {
    attack_inc = 1.0f / (attack_time * (float)SAMPLERATE);
    if (attack_inc > 1.0f) attack_inc = 1.0f;
  }

  if (!decay_hold) {
    decay_dec = 1.0f / (decay_time * (float)SAMPLERATE);
    if (decay_dec > 1.0f) decay_dec = 1.0f;
  }

  pending_sample_ptr = selected_sample->samplearray;
  pending_sample_size = selected_sample->samplesize;
  pending_increment = increment;
  pending_attack_inc = attack_inc;
  pending_decay_dec = decay_dec;
  pending_hold = decay_hold;
  pending_reverse = reverse_playback;
  pending_trigger_serial++;

  last_trigger_sample = (int16_t)chosen;
}

static void update_page_one(void) {
  if (!potlock[0]) live_gain = adc_to_norm(pot[0]) * SAMPLE_GAIN;
  if (!potlock[1]) live_tone_coeff = tone_coeff_lut[adc_to_lut_index(pot[1])];
  if (!potlock[2]) manual_sample = map(pot[2], 0, AD_RANGE - 1, 0, NUM_SAMPLES - 1);
  if (!potlock[3]) pitch_pot_semitones = mapf(pot[3], 0, AD_RANGE - 1, -PITCH_RANGE_SEMITONES, PITCH_RANGE_SEMITONES);
}

static void update_page_two(void) {
  if (!potlock[0]) attack_time = attack_lut[adc_to_lut_index(pot[0])];
  if (!potlock[1]) {
    float decay_norm = adc_to_norm(pot[1]);
    decay_hold = decay_norm >= DECAY_HOLD_THRESHOLD;
    if (!decay_hold) decay_time = decay_lut[adc_to_lut_index(pot[1])];
  }
  if (!potlock[2]) sample_randomness = adc_to_norm(pot[2]);
  if (!potlock[3]) reverse_playback = pot[3] >= (AD_RANGE / 2);
}

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  pinMode(TRIGGER, INPUT_PULLUP);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

  LEDS.begin();
  set_page_led();

  analogReadResolution(AD_BITS);
  samplepots();

  init_lookup_tables();
  randomSeed((uint32_t)sampleCV2() ^ micros());

  live_gain = adc_to_norm(pot[0]) * SAMPLE_GAIN;
  live_tone_coeff = tone_coeff_lut[adc_to_lut_index(pot[1])];
  manual_sample = map(pot[2], 0, AD_RANGE - 1, 0, NUM_SAMPLES - 1);
  pitch_pot_semitones = mapf(pot[3], 0, AD_RANGE - 1, -PITCH_RANGE_SEMITONES, PITCH_RANGE_SEMITONES);
  voct_semitones = read_voct_semitones();

  DAC.setBCLK(BCLK);
  DAC.setDATA(I2S_DATA);
  DAC.setBitsPerSample(16);
  DAC.setBuffers(1, 128, 0);
  DAC.setLSBJFormat();
  DAC.begin(SAMPLERATE);
}

void loop() {
  if (!digitalRead(BUTTON1)) {
    if (((millis() - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      UIstate = (UIstate == PAGE_GREEN) ? PAGE_AQUA : PAGE_GREEN;
      lockpots();
      set_page_led();
    }
  }
  else {
    buttontimer = millis();
    button = 0;
  }

  if ((millis() - parameterupdate) >= CONTROL_UPDATE_MS) {
    parameterupdate = millis();
    samplepots();
    voct_semitones = read_voct_semitones();

    switch (UIstate) {
      case PAGE_GREEN:
        update_page_one();
        break;
      case PAGE_AQUA:
        update_page_two();
        break;
      default:
        break;
    }
  }

  if (!digitalRead(TRIGGER)) {
    if (((micros() - trigtimer) > TRIG_DEBOUNCE) && !triggered) {
      triggered = 1;
      trigger_voice();
    }
  }
  else {
    trigtimer = micros();
    triggered = 0;
  }
}

void setup1() {
  delay(1000);
}

void loop1() {
  static const int16_t *active_sample = 0;
  static uint32_t active_size = 0;
  static float phase = 0.0f;
  static float increment = 1.0f;
  static float env = 0.0f;
  static float attack_inc = 1.0f;
  static float decay_dec = 0.0f;
  static float filter_state = 0.0f;
  static bool hold = 1;
  static bool reverse = 0;
  static uint8_t env_state = ENV_IDLE;
  static uint32_t last_trigger_serial = 0;

  uint32_t trigger_serial = pending_trigger_serial;
  int16_t out = 0;

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0);
#endif

  if (trigger_serial != last_trigger_serial) {
    last_trigger_serial = trigger_serial;
    active_sample = pending_sample_ptr;
    active_size = pending_sample_size;
    increment = pending_increment;
    attack_inc = pending_attack_inc;
    decay_dec = pending_decay_dec;
    hold = pending_hold;
    reverse = pending_reverse;
    filter_state = 0.0f;

    if ((active_sample != 0) && (active_size > 1)) {
      phase = reverse ? (float)(active_size - 1) : 0.0f;
      if (attack_inc >= 1.0f) {
        env = 1.0f;
        env_state = hold ? ENV_SUSTAIN : ENV_DECAY;
      }
      else {
        env = 0.0f;
        env_state = ENV_ATTACK;
      }
    }
    else {
      env_state = ENV_IDLE;
      env = 0.0f;
    }
  }

  if ((env_state != ENV_IDLE) && (active_sample != 0) && (active_size > 1)) {
    float s = 0.0f;

    if (!reverse) {
      if (phase >= (float)(active_size - 1)) {
        env_state = ENV_IDLE;
      }
      else {
        uint32_t idx = (uint32_t)phase;
        float frac = phase - (float)idx;
        float a = (float)active_sample[idx];
        float b = (float)active_sample[idx + 1];
        s = a + (b - a) * frac;
        phase += increment;
      }
    }
    else {
      if (phase <= 0.0f) {
        env_state = ENV_IDLE;
      }
      else {
        uint32_t idx = (uint32_t)phase;
        float frac = phase - (float)idx;
        float a = (float)active_sample[idx];
        float b = (idx > 0) ? (float)active_sample[idx - 1] : a;
        s = a + (b - a) * frac;
        phase -= increment;
      }
    }

    if (env_state != ENV_IDLE) {
      switch (env_state) {
        case ENV_ATTACK:
          env += attack_inc;
          if (env >= 1.0f) {
            env = 1.0f;
            env_state = hold ? ENV_SUSTAIN : ENV_DECAY;
          }
          break;

        case ENV_DECAY:
          env -= decay_dec;
          if (env <= 0.0f) {
            env = 0.0f;
            env_state = ENV_IDLE;
          }
          break;

        default:
          break;
      }
    }

    if (env_state != ENV_IDLE) {
      float coeff = live_tone_coeff;
      float gain = live_gain;
      float output;

      filter_state += (s - filter_state) * coeff;
      output = filter_state * env * gain;

      if (output > 1.0f) output = 1.0f;
      if (output < -1.0f) output = -1.0f;

      out = (int16_t)(output * 32767.0f);
    }
    else {
      filter_state = 0.0f;
      active_sample = 0;
      active_size = 0;
    }
  }

  DAC.write(out);
  DAC.write(out);

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1);
#endif
}
