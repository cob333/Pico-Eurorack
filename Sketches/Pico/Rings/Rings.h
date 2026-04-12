// Copyright 2026 Wenhao Yang
//
// Author: Wenhao Yang
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
// -----------------------------------------------------------------------------
//
// Rings DSP host for 2HPico. This wraps the Mutable Instruments Rings engine
// from the vendored RINGS library and applies the control/audio strategy used
// by the local RingsEngine and RingsEngineScarp references.

#ifndef PICO_EURORACK_RINGS_H_
#define PICO_EURORACK_RINGS_H_

#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <STMLIB.h>
#include <RINGS_ARDUINO.h>

namespace rings {
float Dsp::sr = static_cast<float>(SAMPLERATE);
float Dsp::a3 = 440.0f / static_cast<float>(SAMPLERATE);
}  // namespace rings

namespace pico_rings {

constexpr float kSampleRate = static_cast<float>(SAMPLERATE);
constexpr size_t kBlockSize = rings::kMaxBlockSize;
constexpr uint8_t kNumModels = rings::RESONATOR_MODEL_LAST;
constexpr uint8_t kMaxPolyphony = rings::kMaxPolyphony;
constexpr float kMinNote = 12.0f;
constexpr float kMaxNote = 108.0f;
constexpr float kDefaultStructure = 0.25f;
constexpr float kDefaultDamping = 0.55f;
constexpr float kDefaultBrightness = 0.55f;
constexpr float kDefaultPosition = 0.25f;
constexpr float kDefaultNote = 48.0f;
constexpr float kOutputGain = 0.85f;
constexpr float kMonoMixGain = 0.5f;
constexpr float kOutputGateThreshold = 0.0012f;
constexpr uint8_t kOutputGateCloseBlocks = 32;

struct EngineState {
  rings::Part part;
  rings::Strummer strummer;
  rings::PerformanceState performance_state;
  rings::Patch patch;

  uint16_t reverb_buffer[32768];
  float silence[kBlockSize];
  float out[kBlockSize];
  float aux[kBlockSize];
  int16_t render_buffer[kBlockSize];

  uint8_t active_model;
  uint8_t active_polyphony;
  uint8_t quiet_blocks;
  bool output_gate_open;
};

static volatile float g_structure = kDefaultStructure;
static volatile float g_damping = kDefaultDamping;
static volatile float g_brightness = kDefaultBrightness;
static volatile float g_note = kDefaultNote;
static volatile float g_position = kDefaultPosition;
static volatile uint8_t g_polyphony = 1;
static volatile uint8_t g_model = rings::RESONATOR_MODEL_MODAL;
static volatile bool g_trigger_pending = false;

static EngineState g_engine;

inline uint8_t ClampPolyphony(uint8_t requested, uint8_t model) {
  uint8_t limited = constrain(requested, 1, kMaxPolyphony);
  if (model == rings::RESONATOR_MODEL_FM_VOICE && limited > 2) {
    limited = 2;
  }
  return limited;
}

inline float OutputGainForModel(uint8_t model) {
  switch (model) {
    case rings::RESONATOR_MODEL_FM_VOICE:
      return 0.45f;
    case rings::RESONATOR_MODEL_STRING_AND_REVERB:
      return 0.5f;
    default:
      return kOutputGain;
  }
}

inline void Init() {
  memset(g_engine.reverb_buffer, 0, sizeof(g_engine.reverb_buffer));
  memset(g_engine.silence, 0, sizeof(g_engine.silence));
  memset(g_engine.out, 0, sizeof(g_engine.out));
  memset(g_engine.aux, 0, sizeof(g_engine.aux));
  memset(g_engine.render_buffer, 0, sizeof(g_engine.render_buffer));

  rings::Dsp::setSr(kSampleRate);

  g_engine.part.Init(g_engine.reverb_buffer);
  g_engine.strummer.Init(0.01f, kSampleRate / static_cast<float>(kBlockSize));
  g_engine.part.set_polyphony(1);
  g_engine.part.set_model(rings::RESONATOR_MODEL_MODAL);
  g_engine.part.set_bypass(false);

  g_engine.performance_state.strum = false;
  g_engine.performance_state.internal_exciter = true;
  g_engine.performance_state.internal_strum = false;
  g_engine.performance_state.internal_note = false;
  g_engine.performance_state.tonic = 12.0f;
  g_engine.performance_state.note = kDefaultNote;
  g_engine.performance_state.fm = 0.0f;
  g_engine.performance_state.chord = 0;

  g_engine.patch.structure = kDefaultStructure;
  g_engine.patch.damping = kDefaultDamping;
  g_engine.patch.brightness = kDefaultBrightness;
  g_engine.patch.position = kDefaultPosition;

  g_engine.active_model = rings::RESONATOR_MODEL_MODAL;
  g_engine.active_polyphony = 1;
  g_engine.quiet_blocks = kOutputGateCloseBlocks;
  g_engine.output_gate_open = false;
}

inline void SetUiState(
    float structure,
    float damping,
    float brightness,
    float note,
    float position,
    uint8_t polyphony,
    uint8_t model) {
  g_structure = constrain(structure, 0.0f, 1.0f);
  g_damping = constrain(damping, 0.0f, 1.0f);
  g_brightness = constrain(brightness, 0.0f, 1.0f);
  g_note = constrain(note, kMinNote, kMaxNote);
  g_position = constrain(position, 0.0f, 1.0f);
  g_polyphony = polyphony;
  g_model = constrain(model, 0, kNumModels - 1);
}

inline void QueueTrigger() {
  g_trigger_pending = true;
}

inline bool ConsumeTrigger() {
  if (g_trigger_pending) {
    g_trigger_pending = false;
    return true;
  }

  return false;
}

inline void UpdateOutputGate(float block_peak, bool strummed) {
  if (strummed || block_peak > kOutputGateThreshold) {
    g_engine.output_gate_open = true;
    g_engine.quiet_blocks = 0;
    return;
  }

  if (g_engine.quiet_blocks < kOutputGateCloseBlocks) {
    ++g_engine.quiet_blocks;
    return;
  }

  g_engine.output_gate_open = false;
}

inline void RenderBlock() {
  const float structure = g_structure;
  const float damping = g_damping;
  const float brightness = g_brightness;
  const float note = g_note;
  const float position = g_position;
  const uint8_t model = constrain(g_model, 0, kNumModels - 1);
  const uint8_t polyphony = ClampPolyphony(g_polyphony, model);

  if (model != g_engine.active_model) {
    g_engine.part.set_model(static_cast<rings::ResonatorModel>(model));
    g_engine.active_model = model;
  }

  if (polyphony != g_engine.active_polyphony) {
    g_engine.part.set_polyphony(polyphony);
    g_engine.active_polyphony = polyphony;
  }

  g_engine.patch.structure = structure;
  g_engine.patch.damping = damping;
  g_engine.patch.brightness = brightness;
  g_engine.patch.position = position;

  g_engine.performance_state.tonic = 12.0f;
  g_engine.performance_state.note = note;
  g_engine.performance_state.fm = 0.0f;
  g_engine.performance_state.chord =
      static_cast<int32_t>(roundf(structure * static_cast<float>(rings::kNumChords - 1)));
  g_engine.performance_state.strum = ConsumeTrigger();

  g_engine.strummer.Process(g_engine.silence, kBlockSize, &g_engine.performance_state);
  const bool strummed = g_engine.performance_state.strum;
  g_engine.part.Process(
      g_engine.performance_state,
      g_engine.patch,
      g_engine.silence,
      g_engine.out,
      g_engine.aux,
      kBlockSize);

  const float gain = OutputGainForModel(model);
  float block_peak = 0.0f;
  for (size_t i = 0; i < kBlockSize; ++i) {
    const float mono = (g_engine.out[i] + g_engine.aux[i]) * kMonoMixGain * gain;
    block_peak = max(block_peak, fabsf(mono));
    g_engine.render_buffer[i] =
        stmlib::Clip16(static_cast<int32_t>(mono * 32767.0f));
  }

  UpdateOutputGate(block_peak, strummed);
  if (!g_engine.output_gate_open) {
    memset(g_engine.render_buffer, 0, sizeof(g_engine.render_buffer));
  }
}

inline int16_t NextSample() {
  static size_t output_index = kBlockSize;

  if (output_index >= kBlockSize) {
    RenderBlock();
    output_index = 0;
  }

  return g_engine.render_buffer[output_index++];
}

}  // namespace pico_rings

#endif  // PICO_EURORACK_RINGS_H_
