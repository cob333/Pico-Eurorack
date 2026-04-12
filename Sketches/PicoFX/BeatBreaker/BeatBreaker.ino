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
BeatBreaker for 2HPico DSP hardware
W Yang / R Heslip  Apr 2026

Clock-sliced beat repeater / rearranger for RP2350:
- each clock interval is captured into a rolling pool of recent beat buffers
- on every new clock, the next beat either passes dry audio through or replays one
  of the recent buffers with optional reverse and beat-repeat slicing
- repeat macro raises both the chance of retriggering and the likelihood of
  choosing higher repeat counts from 2x to 8x
- mono output mirrored to both DAC channels

Top Jack - Audio input

Middle jack - Clock input (solder the CV jumper on the back of the 2HPico DSP PCB)

Bottom Jack - Audio out

Button - Clear the captured beat history and return to dry passthrough

First Parameter Page - RED

Top pot - Break probability
Second pot - Reverse probability
Third pot - Repeat macro
Fourth pot - Output level

Notes:
- The sketch runs at 22.05kHz so it can keep a longer recent-beat history in RAM.
- Very slow clocks will truncate each stored beat once the per-beat buffer is full.
*/

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>

#include "pico/multicore.h"

//#define DEBUG
//#define MONITOR_CPU1

#define SAMPLERATE 44100

constexpr uint32_t BEATBREAKER_CONTROL_UPDATE_MS = 5;
constexpr uint8_t BEATBREAKER_AUDIO_CONTROL_DIVIDER = 32;
constexpr uint8_t BEATBREAKER_HISTORY_SLOTS = 6;
constexpr uint32_t BEATBREAKER_MAX_SLICE_SAMPLES = 24000;
constexpr uint32_t BEATBREAKER_MIN_CLOCK_SAMPLES = 96;
constexpr uint32_t BEATBREAKER_CLOCK_TIMEOUT_SAMPLES = SAMPLERATE * 2;
constexpr uint32_t BEATBREAKER_CLOCK_DEBOUNCE_SAMPLES = 64;
constexpr uint16_t BEATBREAKER_Q16_MAX = 65535u;
constexpr uint8_t BEATBREAKER_SMOOTH_SHIFT = 4;
constexpr uint32_t BEATBREAKER_SEGMENT_FADE_SAMPLES = 64;
constexpr uint8_t BEATBREAKER_MAX_REPEAT_COUNT = 8;
constexpr uint8_t BEATBREAKER_LED_IDLE = LED_BRIGHT_0_25;

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S i2s(INPUT_PULLUP); // both input and output

struct SharedSettings {
  volatile uint32_t revision;
  volatile uint16_t break_prob_q16;
  volatile uint16_t reverse_prob_q16;
  volatile uint16_t repeat_macro_q16;
  volatile uint16_t level_q16;
};

struct SharedMeter {
  volatile uint8_t output_level;
  volatile uint8_t activity;
  volatile uint8_t reverse;
  volatile uint8_t synced;
};

struct PlaybackState {
  uint8_t record_slot;
  uint32_t record_pos;
  uint32_t beat_pos;
  uint32_t samples_since_edge;
  uint32_t expected_beat_samples;
  uint32_t raw_clock_stable_samples;
  bool clock_seen;
  bool raw_clock_state;
  bool debounced_clock_state;
  bool playback_active;
  bool playback_reverse;
  uint8_t playback_repeat_count;
  uint8_t playback_slot;
  uint16_t playback_length;
};

SharedSettings settings = {
  0,
  0,
  0,
  0,
  (uint16_t)((BEATBREAKER_Q16_MAX * 9u) / 10u)
};

SharedMeter meter = {
  BEATBREAKER_LED_IDLE,
  0,
  0,
  0
};

PlaybackState audio_state = {
  0,
  0,
  0,
  0,
  SAMPLERATE / 4u,
  0,
  false,
  false,
  false,
  false,
  1,
  0,
  0
};

int16_t history_buffer[BEATBREAKER_HISTORY_SLOTS][BEATBREAKER_MAX_SLICE_SAMPLES];
uint16_t history_length[BEATBREAKER_HISTORY_SLOTS];
bool history_valid[BEATBREAKER_HISTORY_SLOTS];

volatile bool clear_history_request = false;

uint16_t panel_break_q16 = 0;
uint16_t panel_reverse_q16 = 0;
uint16_t panel_repeat_q16 = 0;
uint16_t panel_level_q16 = (uint16_t)((BEATBREAKER_Q16_MAX * 9u) / 10u);

#define NUMUISTATES 1
enum UIstates {BEATBREAKER};
uint8_t UIstate = BEATBREAKER;

bool button = 0;
uint32_t buttontimer = 0;
uint32_t controlupdate = 0;

static inline uint16_t PotToQ16(uint16_t value) {
  return (uint32_t)value * BEATBREAKER_Q16_MAX / (AD_RANGE - 1u);
}

static inline int16_t Clamp16(int32_t sample) {
  if (sample > 32767) return 32767;
  if (sample < -32768) return -32768;
  return (int16_t)sample;
}

static inline bool ChanceQ16(uint16_t probability_q16) {
  if (probability_q16 == 0u) return false;
  if (probability_q16 >= BEATBREAKER_Q16_MAX) return true;
  return (uint32_t)random((long)BEATBREAKER_Q16_MAX + 1L) < probability_q16;
}

static inline uint8_t ChooseRepeatCount(uint16_t repeat_macro_q16) {
  uint8_t pulls = 1u + ((uint32_t)repeat_macro_q16 * 5u) / BEATBREAKER_Q16_MAX; // 1..6 pulls
  uint8_t best = 0u;

  for (uint8_t i = 0; i < pulls; ++i) {
    uint8_t candidate = (uint8_t)random(7L); // 0..6 -> 2..8 repeats after biasing
    if (candidate > best) best = candidate;
  }

  return 2u + best;
}

void PublishSettings(uint16_t break_prob_q16, uint16_t reverse_prob_q16, uint16_t repeat_macro_q16, uint16_t level_q16) {
  ++settings.revision; // odd means update in progress
  settings.break_prob_q16 = break_prob_q16;
  settings.reverse_prob_q16 = reverse_prob_q16;
  settings.repeat_macro_q16 = repeat_macro_q16;
  settings.level_q16 = level_q16;
  ++settings.revision; // even means stable snapshot
}

void ResetHistory(PlaybackState &state) {
  for (uint8_t i = 0; i < BEATBREAKER_HISTORY_SLOTS; ++i) {
    history_valid[i] = 0;
    history_length[i] = 0;
  }

  state.record_slot = 0;
  state.record_pos = 0;
  state.beat_pos = 0;
  state.samples_since_edge = 0;
  state.expected_beat_samples = SAMPLERATE / 4u;
  state.raw_clock_stable_samples = 0;
  state.clock_seen = false;
  state.raw_clock_state = false;
  state.debounced_clock_state = false;
  state.playback_active = false;
  state.playback_reverse = false;
  state.playback_repeat_count = 1;
  state.playback_slot = 0;
  state.playback_length = 0;
}

void FinalizeRecordedSlice(uint8_t slot, uint32_t recorded_samples) {
  if (recorded_samples >= BEATBREAKER_MIN_CLOCK_SAMPLES) {
    if (recorded_samples > BEATBREAKER_MAX_SLICE_SAMPLES) recorded_samples = BEATBREAKER_MAX_SLICE_SAMPLES;
    history_length[slot] = (uint16_t)recorded_samples;
    history_valid[slot] = 1;
  }
  else {
    history_length[slot] = 0;
    history_valid[slot] = 0;
  }
}

uint8_t CollectAvailableSlots(uint8_t excluded_slot, uint8_t *slots) {
  uint8_t count = 0;

  for (uint8_t i = 0; i < BEATBREAKER_HISTORY_SLOTS; ++i) {
    if (i == excluded_slot) continue;
    if (!history_valid[i]) continue;
    if (history_length[i] < 2u) continue;
    slots[count++] = i;
  }

  return count;
}

void ChooseNextPlayback(PlaybackState &state, uint16_t break_prob_q16, uint16_t reverse_prob_q16, uint16_t repeat_macro_q16, uint32_t beat_samples) {
  state.playback_active = false;
  state.playback_reverse = false;
  state.playback_repeat_count = 1;
  state.playback_length = 0;

  if (!ChanceQ16(break_prob_q16)) return;

  uint8_t slots[BEATBREAKER_HISTORY_SLOTS];
  uint8_t count = CollectAvailableSlots(state.record_slot, slots);
  if (!count) return;

  state.playback_slot = slots[(uint8_t)random((long)count)];
  state.playback_length = history_length[state.playback_slot];
  state.playback_reverse = ChanceQ16(reverse_prob_q16);
  state.playback_repeat_count = 1;
  state.expected_beat_samples = beat_samples;

  if (ChanceQ16(repeat_macro_q16)) {
    state.playback_repeat_count = ChooseRepeatCount(repeat_macro_q16);
    if (state.playback_repeat_count > BEATBREAKER_MAX_REPEAT_COUNT) {
      state.playback_repeat_count = BEATBREAKER_MAX_REPEAT_COUNT;
    }
  }

  state.playback_active = true;
}

int16_t ReadSliceSample(uint8_t slot, uint16_t length, uint32_t phase_q16, bool reverse) {
  if (!history_valid[slot] || !length) return 0;

  uint32_t max_phase_q16 = ((uint32_t)(length - 1u) << 16);
  if (phase_q16 > max_phase_q16) phase_q16 = max_phase_q16;

  uint32_t read_phase_q16 = reverse ? (max_phase_q16 - phase_q16) : phase_q16;
  uint16_t index = (uint16_t)(read_phase_q16 >> 16);
  uint16_t frac = (uint16_t)(read_phase_q16 & 0xffffu);
  uint16_t next_index = index;

  if ((index + 1u) < length) next_index = index + 1u;

  int32_t a = history_buffer[slot][index];
  int32_t b = history_buffer[slot][next_index];

  return (int16_t)(a + (int32_t)((((int64_t)(b - a) * frac) + 32768) >> 16));
}

uint16_t SegmentEnvelopeQ16(uint32_t step_in_segment, uint32_t segment_length) {
  if (segment_length < 4u) return BEATBREAKER_Q16_MAX;

  uint32_t fade = BEATBREAKER_SEGMENT_FADE_SAMPLES;
  uint32_t max_fade = segment_length >> 2;
  if (max_fade == 0u) return BEATBREAKER_Q16_MAX;
  if (fade > max_fade) fade = max_fade;
  if (fade == 0u) return BEATBREAKER_Q16_MAX;

  uint32_t distance_to_edge = step_in_segment;
  uint32_t from_end = (segment_length - 1u) - step_in_segment;
  if (from_end < distance_to_edge) distance_to_edge = from_end;

  if (distance_to_edge >= fade) return BEATBREAKER_Q16_MAX;
  return (uint16_t)((distance_to_edge * BEATBREAKER_Q16_MAX) / fade);
}

int16_t RenderPlaybackSample(const PlaybackState &state) {
  if (!state.playback_active || state.playback_length < 2u) return 0;

  uint8_t repeats = state.playback_repeat_count;
  if (repeats < 1u) repeats = 1u;

  uint32_t segment_length = state.expected_beat_samples / repeats;
  if (segment_length < 1u) segment_length = 1u;

  uint32_t step_in_segment = state.beat_pos % segment_length;
  uint32_t phase_q16 = 0;

  if (segment_length > 1u) {
    uint64_t numerator = ((uint64_t)step_in_segment * (uint64_t)(state.playback_length - 1u)) << 16;
    phase_q16 = (uint32_t)(numerator / (uint64_t)(segment_length - 1u));
  }

  int16_t sample = ReadSliceSample(state.playback_slot, state.playback_length, phase_q16, state.playback_reverse);
  uint16_t env_q16 = SegmentEnvelopeQ16(step_in_segment, segment_length);
  return (int16_t)(((int32_t)sample * env_q16) >> 16);
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
  pinMode(CV2IN, INPUT_PULLUP);

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

  randomSeed(((uint32_t)analogRead(AIN2) << 12) ^ (uint32_t)analogRead(AIN3) ^ micros());
  PublishSettings(panel_break_q16, panel_reverse_q16, panel_repeat_q16, panel_level_q16);
  ResetHistory(audio_state);

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
      if (UIstate >= NUMUISTATES) UIstate = BEATBREAKER;
      clear_history_request = true;
      lockpots();
    }
  }
  else {
    buttontimer = now;
    button = 0;
  }

  if ((now - controlupdate) >= BEATBREAKER_CONTROL_UPDATE_MS) {
    controlupdate = now;
    samplepots();

    switch (UIstate) {
      case BEATBREAKER:
        if (!potlock[0]) panel_break_q16 = PotToQ16(pot[0]);
        if (!potlock[1]) panel_reverse_q16 = PotToQ16(pot[1]);
        if (!potlock[2]) panel_repeat_q16 = PotToQ16(pot[2]);
        if (!potlock[3]) panel_level_q16 = PotToQ16(pot[3]);
        break;
      default:
        break;
    }

    PublishSettings(panel_break_q16, panel_reverse_q16, panel_repeat_q16, panel_level_q16);

    uint8_t level_led = meter.output_level;
    uint8_t activity_led = meter.activity;
    uint8_t reverse_led = meter.reverse;
    uint8_t synced_led = meter.synced;

    uint8_t red = activity_led;
    uint8_t green = level_led;
    uint8_t blue = reverse_led;

    if (!synced_led && !red && !green && !blue) {
      red = BEATBREAKER_LED_IDLE;
      green = BEATBREAKER_LED_IDLE;
      blue = BEATBREAKER_LED_IDLE;
    }
    else if (synced_led && !red && !blue && green < BEATBREAKER_LED_IDLE) {
      green = BEATBREAKER_LED_IDLE;
    }

    LEDS.setPixelColor(0, RGB(red, green, blue));
    LEDS.show();
  }
}

// second core setup
// second core is dedicated to sample processing
void setup1() {
  delay(1000); // wait for main core to start up peripherals
  ResetHistory(audio_state);
}

// process audio samples
void loop1() {
  static uint8_t control_divider = 0;
  static uint32_t applied_revision = 0xffffffffu;
  static uint16_t break_prob_q16 = 0;
  static uint16_t reverse_prob_q16 = 0;
  static uint16_t repeat_macro_q16 = 0;
  static int32_t target_level_q16 = (BEATBREAKER_Q16_MAX * 9u) / 10u;
  static int32_t current_level_q16 = (BEATBREAKER_Q16_MAX * 9u) / 10u;
  static uint8_t vu = BEATBREAKER_LED_IDLE;

  int32_t left = i2s.read();
  i2s.read(); // discard right input slot; input is mono

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // hi = CPU busy
#endif

  if (++control_divider >= BEATBREAKER_AUDIO_CONTROL_DIVIDER) {
    control_divider = 0;

    uint32_t revision;
    uint16_t new_break_prob_q16;
    uint16_t new_reverse_prob_q16;
    uint16_t new_repeat_macro_q16;
    uint16_t new_level_q16;

    do {
      revision = settings.revision;
      while (revision & 1u) revision = settings.revision;
      new_break_prob_q16 = settings.break_prob_q16;
      new_reverse_prob_q16 = settings.reverse_prob_q16;
      new_repeat_macro_q16 = settings.repeat_macro_q16;
      new_level_q16 = settings.level_q16;
    } while (revision != settings.revision);

    if (revision != applied_revision) {
      break_prob_q16 = new_break_prob_q16;
      reverse_prob_q16 = new_reverse_prob_q16;
      repeat_macro_q16 = new_repeat_macro_q16;
      target_level_q16 = new_level_q16;
      applied_revision = revision;
    }

    current_level_q16 += (target_level_q16 - current_level_q16) >> BEATBREAKER_SMOOTH_SHIFT;
  }

  if (clear_history_request) {
    ResetHistory(audio_state);
    clear_history_request = false;
  }

  int16_t input_sample = (int16_t)(left >> 16);

  bool raw_clock = !digitalRead(CV2IN); // middle jack clock input is treated as active-low
  bool clock_edge = false;

  if (raw_clock == audio_state.raw_clock_state) {
    if (audio_state.raw_clock_stable_samples < 0xffffffffu) ++audio_state.raw_clock_stable_samples;
  }
  else {
    audio_state.raw_clock_state = raw_clock;
    audio_state.raw_clock_stable_samples = 1u;
  }

  if ((audio_state.raw_clock_stable_samples >= BEATBREAKER_CLOCK_DEBOUNCE_SAMPLES) &&
      (audio_state.debounced_clock_state != audio_state.raw_clock_state)) {
    bool previous_clock_state = audio_state.debounced_clock_state;
    audio_state.debounced_clock_state = audio_state.raw_clock_state;
    if (audio_state.debounced_clock_state && !previous_clock_state) clock_edge = true;
  }

  if (clock_edge) {
    uint32_t measured_beat_samples = audio_state.samples_since_edge;
    if (!audio_state.clock_seen) measured_beat_samples = audio_state.record_pos;
    if (measured_beat_samples < BEATBREAKER_MIN_CLOCK_SAMPLES) measured_beat_samples = BEATBREAKER_MIN_CLOCK_SAMPLES;

    FinalizeRecordedSlice(audio_state.record_slot, audio_state.record_pos);
    audio_state.record_slot = (audio_state.record_slot + 1u) % BEATBREAKER_HISTORY_SLOTS;
    audio_state.record_pos = 0;
    history_valid[audio_state.record_slot] = 0;
    history_length[audio_state.record_slot] = 0;

    audio_state.beat_pos = 0;
    audio_state.samples_since_edge = 0;
    audio_state.expected_beat_samples = measured_beat_samples;
    audio_state.clock_seen = true;

    ChooseNextPlayback(audio_state, break_prob_q16, reverse_prob_q16, repeat_macro_q16, measured_beat_samples);
  }

  if (audio_state.record_pos < BEATBREAKER_MAX_SLICE_SAMPLES) {
    history_buffer[audio_state.record_slot][audio_state.record_pos] = input_sample;
    ++audio_state.record_pos;
  }

  int16_t processed_sample = input_sample;
  if (audio_state.playback_active && audio_state.clock_seen) {
    processed_sample = RenderPlaybackSample(audio_state);
  }

  int32_t scaled_sample = ((int64_t)processed_sample * current_level_q16) >> 16;
  int16_t output_sample = Clamp16(scaled_sample);
  int32_t output32 = (int32_t)output_sample << 16;

  uint32_t abs_output = (output_sample < 0) ? (uint32_t)(-((int32_t)output_sample)) : (uint32_t)output_sample;
  uint8_t target_vu = (uint8_t)(abs_output >> 10); // 0..31 approx
  if (target_vu > LED_BRIGHT_1) target_vu = LED_BRIGHT_1;

  if (target_vu > vu) vu += (uint8_t)(((uint16_t)(target_vu - vu) + 1u) >> 1);
  else if (target_vu < vu) vu -= (uint8_t)(((uint16_t)(vu - target_vu) + 7u) >> 3);

  meter.output_level = vu;
  meter.activity = audio_state.playback_active ? (audio_state.playback_repeat_count > 1u ? LED_BRIGHT_1 : LED_BRIGHT_0_5) : 0;
  meter.reverse = (audio_state.playback_active && audio_state.playback_reverse) ? LED_BRIGHT_0_5 : 0;
  meter.synced = audio_state.clock_seen ? BEATBREAKER_LED_IDLE : 0;

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0); // low - CPU not busy
#endif

  i2s.write(output32);
  i2s.write(output32);

  if (audio_state.samples_since_edge < 0xffffffffu) ++audio_state.samples_since_edge;
  if (audio_state.beat_pos < 0xffffffffu) ++audio_state.beat_pos;

  if (audio_state.clock_seen && (audio_state.samples_since_edge > BEATBREAKER_CLOCK_TIMEOUT_SAMPLES)) {
    audio_state.clock_seen = false;
    audio_state.playback_active = false;
    audio_state.playback_repeat_count = 1;
  }
}
