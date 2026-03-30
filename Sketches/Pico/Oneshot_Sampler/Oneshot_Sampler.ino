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
Middle jack - CV input, target selected between Volt/Octave and Sample Selection
Bottom jack - audio out

Button - switch parameter page

Page 1 parameters - GREEN LED
Pot 1 - Level
Pot 2 - Tone (lowpass brightness)
Pot 3 - CV in Target
Pot 4 - Pitch

Page 2 parameters - AQUA LED
Pot 1 - Start point
Pot 2 - End point
Pot 3 - Attack
Pot 4 - Decay (full CW = hold until sample end)

Page 3 parameters - BLUE LED
Pot 1 - Bank selection
Pot 2 - Sample selection
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
#include <EEPROM.h>
#include <math.h>
#include "pico/multicore.h"

//#define DEBUG
//#define MONITOR_CPU1

#define SAMPLERATE 44100
#define CONTROL_UPDATE_MS 5
#define CVIN_VOLT 580.6f
#define EEPROM_BYTES 256
#define ONESHOT_STORE_MAGIC 0x4f4e5331u
#define ONESHOT_STORE_VERSION 1u
#define SAVE_HOLD_MS 3000UL
#define FEEDBACK_BLINK_MS 120

#define PITCH_RANGE_SEMITONES 24.0f
#define ATTACK_MAX_SEC 0.30f
#define DECAY_MIN_SEC 0.01f
#define DECAY_MAX_SEC 3.00f
#define DECAY_HOLD_THRESHOLD 0.985f
#define CV_SAMPLE_SELECT_THRESHOLD 32U
#define POT_EDGE_SNAP 192U

#define TONE_MIN_HZ 250.0f
#define TONE_MAX_HZ 14000.0f

#define LUT_SIZE 256
#define SAMPLE_GAIN (1.0f / 32768.0f)

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);

#include "Samples/samples.h"

enum UIstates {PAGE_GREEN, PAGE_AQUA, PAGE_BLUE};
enum EnvState {ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN};
enum CVTarget {CV_TARGET_VOCT, CV_TARGET_SAMPLE};

uint8_t UIstate = PAGE_GREEN;
bool button = 0;
bool triggered = 0;
uint32_t buttontimer = 0;
uint32_t trigtimer = 0;
uint32_t parameterupdate = 0;

float attack_lut[LUT_SIZE];
float decay_lut[LUT_SIZE];
float tone_coeff_lut[LUT_SIZE];

uint16_t current_bank = 0;
uint16_t manual_sample = 0;
int16_t last_trigger_sample = -1;
int16_t last_trigger_bank = -1;
float pitch_pot_semitones = 0.0f;
float voct_semitones = 0.0f;
uint8_t cv_target = CV_TARGET_VOCT;
uint16_t cv_sample = 0;
bool cv_sample_active = 0;
float start_point = 0.0f;
float end_point = 1.0f;
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
volatile uint32_t pending_start_index = 0;
volatile uint32_t pending_end_index = 1;
volatile bool pending_hold = 1;
volatile bool pending_reverse = 0;
volatile uint32_t pending_trigger_serial = 0;
uint32_t buttonpress_start = 0;
bool buttonhold_handled = 0;

struct OneshotStore {
  uint32_t magic;
  uint16_t version;
  uint8_t ui_state;
  uint8_t cv_target;
  uint16_t current_bank;
  uint16_t manual_sample;
  float live_gain;
  float live_tone_coeff;
  float pitch_pot_semitones;
  float start_point;
  float end_point;
  float attack_time;
  float decay_time;
  float sample_randomness;
  uint8_t decay_hold;
  uint8_t reverse_playback;
  uint16_t reserved;
  uint32_t checksum;
};

static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

static inline float clamp_range(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

static inline uint16_t snap_pot_adc(uint16_t adc_val) {
  if (adc_val >= AD_RANGE) adc_val = AD_RANGE - 1;
  if (adc_val <= POT_EDGE_SNAP) return 0;
  if (adc_val >= ((AD_RANGE - 1) - POT_EDGE_SNAP)) return AD_RANGE - 1;
  return adc_val;
}

static inline float adc_to_norm(uint16_t adc_val) {
  adc_val = snap_pot_adc(adc_val);
  return (float)adc_val / (float)(AD_RANGE - 1);
}

static inline uint16_t adc_to_lut_index(uint16_t adc_val) {
  adc_val = snap_pot_adc(adc_val);
  uint32_t idx = ((uint32_t)adc_val * (LUT_SIZE - 1)) / AD_RANGE;
  if (idx >= LUT_SIZE) idx = LUT_SIZE - 1;
  return (uint16_t)idx;
}

static inline uint16_t adc_to_choice(uint16_t adc_val, uint16_t count) {
  if (count <= 1) return 0;
  adc_val = snap_pot_adc(adc_val);
  return map(adc_val, 0, AD_RANGE - 1, 0, count - 1);
}

static inline float semitones_to_ratio(float semitones) {
  float ratio = exp2f(semitones / 12.0f);
  if (ratio < 0.125f) ratio = 0.125f;
  if (ratio > 8.0f) ratio = 8.0f;
  return ratio;
}

static void set_page_led(void) {
  if (UIstate == PAGE_GREEN) LEDS.setPixelColor(0, GREEN);
  else if (UIstate == PAGE_AQUA) LEDS.setPixelColor(0, AQUA);
  else LEDS.setPixelColor(0, BLUE);
  LEDS.show();
}

static void blink_feedback(uint32_t color, uint8_t times) {
  for (uint8_t i = 0; i < times; ++i) {
    LEDS.setPixelColor(0, color);
    LEDS.show();
    delay(FEEDBACK_BLINK_MS);
    LEDS.setPixelColor(0, 0);
    LEDS.show();
    delay(FEEDBACK_BLINK_MS);
  }
  set_page_led();
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

static inline uint16_t read_cv2_inverted(void) {
  uint16_t cv = sampleCV2();
  if (cv >= AD_RANGE) cv = AD_RANGE - 1;
  return (uint16_t)((AD_RANGE - 1) - cv);
}

static inline sample_bank_t *get_bank(uint16_t index) {
  if (NUM_BANKS == 0) return 0;
  if (index >= NUM_BANKS) index = 0;
  return &sample_banks[index];
}

static inline uint16_t current_bank_sample_count(void) {
  sample_bank_t *bank = get_bank(current_bank);
  if (bank == 0) return 0;
  return bank->count;
}

static inline void clamp_manual_sample_to_bank(void) {
  uint16_t count = current_bank_sample_count();
  if (count == 0) manual_sample = 0;
  else if (manual_sample >= count) manual_sample = count - 1;
}

static inline uint16_t get_selected_sample_index(uint16_t sample_count) {
  uint16_t selected = manual_sample;

  if (sample_count == 0) return 0;
  if ((cv_target == CV_TARGET_SAMPLE) && cv_sample_active) selected = cv_sample;
  if (selected >= sample_count) selected = sample_count - 1;
  return selected;
}

static void refresh_cv_assignment(void) {
  uint16_t cv = read_cv2_inverted();
  uint16_t sample_count = current_bank_sample_count();

  if (cv_target == CV_TARGET_VOCT) {
    voct_semitones = ((float)cv / CVIN_VOLT) * 12.0f;
    cv_sample_active = 0;
    cv_sample = manual_sample;
    return;
  }

  voct_semitones = 0.0f;

  if (sample_count <= 1) {
    cv_sample_active = 0;
    cv_sample = 0;
    return;
  }

  if (cv <= CV_SAMPLE_SELECT_THRESHOLD) {
    cv_sample_active = 0;
    cv_sample = manual_sample;
    return;
  }

  cv_sample_active = 1;
  cv_sample = map(cv, CV_SAMPLE_SELECT_THRESHOLD, AD_RANGE - 1, 0, sample_count - 1);
}

static uint32_t oneshot_store_checksum(const OneshotStore &data) {
  uint32_t hash = 2166136261u;
  const uint8_t *raw = reinterpret_cast<const uint8_t*>(&data);

  for (uint32_t i = 0; i < (uint32_t)(sizeof(OneshotStore) - sizeof(uint32_t)); ++i) {
    hash ^= raw[i];
    hash *= 16777619u;
  }
  return hash;
}

static void sanitize_runtime_state(void) {
  if (UIstate > PAGE_BLUE) UIstate = PAGE_GREEN;
  if (cv_target > CV_TARGET_SAMPLE) cv_target = CV_TARGET_VOCT;
  if (NUM_BANKS == 0) current_bank = 0;
  else if (current_bank >= NUM_BANKS) current_bank = 0;

  clamp_manual_sample_to_bank();

  live_gain = clamp_range(live_gain, 0.0f, SAMPLE_GAIN);
  live_tone_coeff = clamp01(live_tone_coeff);
  pitch_pot_semitones = clamp_range(pitch_pot_semitones, -PITCH_RANGE_SEMITONES, PITCH_RANGE_SEMITONES);
  start_point = clamp01(start_point);
  end_point = clamp01(end_point);
  attack_time = clamp_range(attack_time, 0.0f, ATTACK_MAX_SEC);
  decay_time = clamp_range(decay_time, DECAY_MIN_SEC, DECAY_MAX_SEC);
  sample_randomness = clamp01(sample_randomness);
  decay_hold = decay_hold ? 1 : 0;
  reverse_playback = reverse_playback ? 1 : 0;
}

static bool validate_store(const OneshotStore &data) {
  if (data.magic != ONESHOT_STORE_MAGIC) return 0;
  if (data.version != ONESHOT_STORE_VERSION) return 0;
  if (data.checksum != oneshot_store_checksum(data)) return 0;
  if (data.ui_state > PAGE_BLUE) return 0;
  if (data.cv_target > CV_TARGET_SAMPLE) return 0;
  if (!isfinite(data.live_gain) || (data.live_gain < 0.0f) || (data.live_gain > SAMPLE_GAIN)) return 0;
  if (!isfinite(data.live_tone_coeff) || (data.live_tone_coeff < 0.0f) || (data.live_tone_coeff > 1.0f)) return 0;
  if (!isfinite(data.pitch_pot_semitones) ||
      (data.pitch_pot_semitones < -PITCH_RANGE_SEMITONES) ||
      (data.pitch_pot_semitones > PITCH_RANGE_SEMITONES)) return 0;
  if (!isfinite(data.start_point) || (data.start_point < 0.0f) || (data.start_point > 1.0f)) return 0;
  if (!isfinite(data.end_point) || (data.end_point < 0.0f) || (data.end_point > 1.0f)) return 0;
  if (!isfinite(data.attack_time) || (data.attack_time < 0.0f) || (data.attack_time > ATTACK_MAX_SEC)) return 0;
  if (!isfinite(data.decay_time) || (data.decay_time < DECAY_MIN_SEC) || (data.decay_time > DECAY_MAX_SEC)) return 0;
  if (!isfinite(data.sample_randomness) || (data.sample_randomness < 0.0f) || (data.sample_randomness > 1.0f)) return 0;
  if (data.decay_hold > 1) return 0;
  if (data.reverse_playback > 1) return 0;
  return 1;
}

static void copy_state_to_store(OneshotStore &data) {
  data.magic = ONESHOT_STORE_MAGIC;
  data.version = ONESHOT_STORE_VERSION;
  data.ui_state = UIstate;
  data.cv_target = cv_target;
  data.current_bank = current_bank;
  data.manual_sample = manual_sample;
  data.live_gain = live_gain;
  data.live_tone_coeff = live_tone_coeff;
  data.pitch_pot_semitones = pitch_pot_semitones;
  data.start_point = start_point;
  data.end_point = end_point;
  data.attack_time = attack_time;
  data.decay_time = decay_time;
  data.sample_randomness = sample_randomness;
  data.decay_hold = decay_hold ? 1 : 0;
  data.reverse_playback = reverse_playback ? 1 : 0;
  data.reserved = 0;
}

static bool load_state_from_flash(void) {
  OneshotStore data;
  EEPROM.get(0, data);
  if (!validate_store(data)) return 0;

  UIstate = data.ui_state;
  cv_target = data.cv_target;
  current_bank = data.current_bank;
  manual_sample = data.manual_sample;
  live_gain = data.live_gain;
  live_tone_coeff = data.live_tone_coeff;
  pitch_pot_semitones = data.pitch_pot_semitones;
  start_point = data.start_point;
  end_point = data.end_point;
  attack_time = data.attack_time;
  decay_time = data.decay_time;
  sample_randomness = data.sample_randomness;
  decay_hold = data.decay_hold ? 1 : 0;
  reverse_playback = data.reverse_playback ? 1 : 0;

  sanitize_runtime_state();
  refresh_cv_assignment();
  return 1;
}

static bool save_state_to_flash(void) {
  OneshotStore data;
  sanitize_runtime_state();
  copy_state_to_store(data);
  data.checksum = oneshot_store_checksum(data);
  EEPROM.put(0, data);
  return EEPROM.commit();
}

static uint16_t choose_trigger_sample(uint16_t bank_index, uint16_t sample_count) {
  uint16_t chosen = get_selected_sample_index(sample_count);
  if ((sample_count <= 1) || (sample_randomness <= 0.0f)) return chosen;

  bool choose_random = (sample_randomness >= 0.999f);
  if (!choose_random) {
    uint16_t threshold = (uint16_t)(sample_randomness * 1000.0f);
    choose_random = ((uint16_t)random(0, 1000)) < threshold;
  }
  if (!choose_random) return chosen;

  if ((sample_count == 2) && (last_trigger_bank == (int16_t)bank_index)) {
    return (last_trigger_sample == 0) ? 1 : 0;
  }

  do {
    chosen = (uint16_t)random(0, sample_count);
  } while ((sample_count > 1) &&
           (last_trigger_bank == (int16_t)bank_index) &&
           (chosen == (uint16_t)last_trigger_sample));

  return chosen;
}

static void trigger_voice(void) {
  sample_bank_t *bank = get_bank(current_bank);
  uint16_t sample_count;
  uint16_t chosen;
  float semitones = pitch_pot_semitones + voct_semitones;
  float increment = semitones_to_ratio(semitones);
  float attack_inc = 1.0f;
  float decay_dec = 0.0f;
  uint32_t start_index = 0;
  uint32_t end_index = 1;
  const sample_t *selected_sample;
  uint32_t last_index;

  if (bank == 0) return;
  sample_count = bank->count;
  if (sample_count == 0) return;

  clamp_manual_sample_to_bank();
  chosen = choose_trigger_sample(current_bank, sample_count);
  selected_sample = &bank->samples[chosen];

  if (selected_sample->samplesize < 2) return;
  last_index = selected_sample->samplesize - 1;

  start_index = (uint32_t)(clamp01(start_point) * (float)last_index);
  end_index = (uint32_t)(clamp01(end_point) * (float)last_index);

  if (start_index >= last_index) start_index = last_index - 1;
  if (end_index <= start_index) end_index = start_index + 1;
  if (end_index > last_index) {
    end_index = last_index;
    if (end_index <= start_index) start_index = end_index - 1;
  }

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
  pending_start_index = start_index;
  pending_end_index = end_index;
  pending_hold = decay_hold;
  pending_reverse = reverse_playback;
  pending_trigger_serial++;

  last_trigger_bank = (int16_t)current_bank;
  last_trigger_sample = (int16_t)chosen;
}

static void update_page_one(void) {
  if (!potlock[0]) live_gain = adc_to_norm(pot[0]) * SAMPLE_GAIN;
  if (!potlock[1]) live_tone_coeff = tone_coeff_lut[adc_to_lut_index(pot[1])];
  if (!potlock[2]) cv_target = (snap_pot_adc(pot[2]) >= (AD_RANGE / 2)) ? CV_TARGET_SAMPLE : CV_TARGET_VOCT;
  if (!potlock[3]) pitch_pot_semitones = mapf(snap_pot_adc(pot[3]), 0, AD_RANGE - 1, -PITCH_RANGE_SEMITONES, PITCH_RANGE_SEMITONES);
}

static void update_page_two(void) {
  if (!potlock[0]) start_point = adc_to_norm(pot[0]);
  if (!potlock[1]) end_point = adc_to_norm(pot[1]);
  if (!potlock[2]) attack_time = attack_lut[adc_to_lut_index(pot[2])];
  if (!potlock[3]) {
    float decay_norm = adc_to_norm(pot[3]);
    decay_hold = decay_norm >= DECAY_HOLD_THRESHOLD;
    if (!decay_hold) decay_time = decay_lut[adc_to_lut_index(pot[3])];
  }
}

static void update_page_three(void) {
  uint16_t sample_count;

  if (!potlock[0]) {
    if (NUM_BANKS > 1) current_bank = adc_to_choice(pot[0], NUM_BANKS);
    else current_bank = 0;
    clamp_manual_sample_to_bank();
  }
  sample_count = current_bank_sample_count();
  if (!potlock[1]) {
    if (sample_count > 1) manual_sample = adc_to_choice(pot[1], sample_count);
    else manual_sample = 0;
  }
  clamp_manual_sample_to_bank();
  if (!potlock[2]) sample_randomness = adc_to_norm(pot[2]);
  if (!potlock[3]) reverse_playback = snap_pot_adc(pot[3]) >= (AD_RANGE / 2);
}

static void init_state_from_pots(void) {
  if (NUM_BANKS > 1) current_bank = 0;
  else current_bank = 0;
  live_gain = adc_to_norm(pot[0]) * SAMPLE_GAIN;
  live_tone_coeff = tone_coeff_lut[adc_to_lut_index(pot[1])];
  cv_target = (snap_pot_adc(pot[2]) >= (AD_RANGE / 2)) ? CV_TARGET_SAMPLE : CV_TARGET_VOCT;
  if (current_bank_sample_count() > 1) manual_sample = adc_to_choice(pot[1], current_bank_sample_count());
  else manual_sample = 0;
  pitch_pot_semitones = mapf(snap_pot_adc(pot[3]), 0, AD_RANGE - 1, -PITCH_RANGE_SEMITONES, PITCH_RANGE_SEMITONES);
  start_point = 0.0f;
  end_point = 1.0f;
  attack_time = 0.0f;
  decay_time = 0.40f;
  sample_randomness = 0.0f;
  decay_hold = 0;
  reverse_playback = 0;
  sanitize_runtime_state();
  refresh_cv_assignment();
}

static void service_button(void) {
  bool pressed = !digitalRead(BUTTON1);
  uint32_t now = millis();

  if (pressed != button) {
    if ((now - buttontimer) > DEBOUNCE) {
      buttontimer = now;
      button = pressed;

      if (button) {
        buttonpress_start = now;
        buttonhold_handled = 0;
      }
      else if (!buttonhold_handled) {
        UIstate = (UIstate + 1) % 3;
        lockpots();
        set_page_led();
      }
    }
    return;
  }

  if (button && !buttonhold_handled && ((now - buttonpress_start) >= SAVE_HOLD_MS)) {
    bool saved = save_state_to_flash();
    buttonhold_handled = 1;
    blink_feedback(saved ? GREEN : RED, 3);
  }
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

  EEPROM.begin(EEPROM_BYTES);
  if (load_state_from_flash()) {
    lockpots();
  }
  else {
    init_state_from_pots();
  }

  set_page_led();

  DAC.setBCLK(BCLK);
  DAC.setDATA(I2S_DATA);
  DAC.setBitsPerSample(16);
  DAC.setBuffers(1, 128, 0);
  DAC.setLSBJFormat();
  DAC.begin(SAMPLERATE);
}

void loop() {
  service_button();

  if ((millis() - parameterupdate) >= CONTROL_UPDATE_MS) {
    parameterupdate = millis();
    samplepots();

    switch (UIstate) {
      case PAGE_GREEN:
        update_page_one();
        break;
      case PAGE_AQUA:
        update_page_two();
        break;
      case PAGE_BLUE:
        update_page_three();
        break;
      default:
        break;
    }

    clamp_manual_sample_to_bank();
    refresh_cv_assignment();
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
  static uint32_t start_index = 0;
  static uint32_t end_index = 1;
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
    start_index = pending_start_index;
    end_index = pending_end_index;
    hold = pending_hold;
    reverse = pending_reverse;
    filter_state = 0.0f;

    if ((active_sample != 0) && (active_size > 1)) {
      phase = reverse ? (float)end_index : (float)start_index;
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
      if (phase >= (float)end_index) {
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
      if (phase < (float)start_index) {
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
