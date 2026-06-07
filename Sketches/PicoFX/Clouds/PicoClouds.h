// Copyright 2026 Wenhao Yang
//
// Mutable Instruments Clouds host for 2HPico PicoFX.
// The Clouds DSP sources and their STMLIB dependencies are vendored in this
// sketch directory so the engine has no external library dependency.

#ifndef PICO_EURORACK_CLOUDS_H_
#define PICO_EURORACK_CLOUDS_H_

#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "STMLIB.h"
#include "CLOUDS.h"

namespace pico_clouds {

constexpr float kSampleRate = static_cast<float>(SAMPLERATE);
constexpr size_t kBlockSize = 32;
constexpr size_t kLargeBufferSize = 118784;
constexpr size_t kSmallBufferSize = 65536 - 128;
constexpr uint8_t kStretchMaintenancePeriodBlocks = 4;
constexpr uint8_t kNumModes = 3;
constexpr uint8_t kNumQualities = 4;
constexpr uint8_t kNumSlots = 4;
constexpr float kInputGain = 1.0f;
constexpr float kOutputGain = 0.95f;
constexpr float kGranularOutputTrim = 0.72f;
constexpr float kStretchOutputTrim = 1.0f;
constexpr float kLoopingDelayOutputTrim = 1.0f;
constexpr float kDefaultPosition = 0.0f;
constexpr float kDefaultSize = 0.5f;
constexpr float kDefaultPitch = 0.0f;
constexpr float kDefaultDensity = 0.25f;
constexpr float kDefaultTexture = 0.5f;
constexpr float kDefaultDryWet = 0.5f;
constexpr float kDefaultStereoSpread = 0.5f;
constexpr float kDefaultFeedback = 0.0f;
constexpr float kDefaultReverb = 0.0f;
constexpr float kParameterSmoothing = 0.08f;

struct UiState {
  float position;
  float size;
  float density;
  float texture;
  float dry_wet;
  float stereo_spread;
  float feedback;
  float reverb;
  float pitch;
  uint8_t mode;
  uint8_t quality;
  uint8_t slot;
  bool freeze;
};

struct EngineState {
  clouds::GranularProcessor processor;
  uint8_t large_buffer[kLargeBufferSize];
  uint8_t small_buffer[kSmallBufferSize];
  clouds::FloatFrame input[kBlockSize];
  clouds::FloatFrame output[kBlockSize];
  int16_t render_l[kBlockSize];
  int16_t render_r[kBlockSize];
  UiState target;
  UiState smooth;
  uint8_t active_mode;
  uint8_t active_quality;
  uint8_t selected_slot;
  uint8_t maintenance_divider;
  bool slot_valid[kNumSlots];
};

static EngineState g_engine;

inline uint8_t ClampMode(uint8_t mode) {
  return mode >= kNumModes ? kNumModes - 1 : mode;
}

inline uint8_t ClampQuality(uint8_t quality) {
  return quality >= kNumQualities ? kNumQualities - 1 : quality;
}

inline uint8_t ClampSlot(uint8_t slot) {
  return slot >= kNumSlots ? kNumSlots - 1 : slot;
}

inline clouds::PlaybackMode PlaybackModeFromIndex(uint8_t mode) {
  switch (ClampMode(mode)) {
    case 0:
      return clouds::PLAYBACK_MODE_GRANULAR;
    case 1:
      return clouds::PLAYBACK_MODE_STRETCH;
    case 2:
    default:
      return clouds::PLAYBACK_MODE_LOOPING_DELAY;
  }
}

inline int32_t QualityFromIndex(uint8_t quality) {
  // Clouds quality bits: bit 0 = mono, bit 1 = low fidelity.
  switch (ClampQuality(quality)) {
    case 0:
      return 0;  // stereo, 16-bit
    case 1:
      return 1;  // mono, 16-bit
    case 2:
      return 2;  // stereo, 8-bit mu-law
    case 3:
    default:
      return 3;  // mono, 8-bit mu-law
  }
}

inline bool IsMonoQuality(uint8_t quality) {
  return (QualityFromIndex(quality) & 1) != 0;
}

inline float OutputTrimForMode(uint8_t mode) {
  switch (ClampMode(mode)) {
    case 0:
      return kGranularOutputTrim;
    case 1:
      return kStretchOutputTrim;
    case 2:
    default:
      return kLoopingDelayOutputTrim;
  }
}

inline float Smooth(float current, float target) {
  return current + (target - current) * kParameterSmoothing;
}

inline UiState DefaultUiState() {
  UiState state;
  state.position = kDefaultPosition;
  state.size = kDefaultSize;
  state.density = kDefaultDensity;
  state.texture = kDefaultTexture;
  state.dry_wet = kDefaultDryWet;
  state.stereo_spread = kDefaultStereoSpread;
  state.feedback = kDefaultFeedback;
  state.reverb = kDefaultReverb;
  state.pitch = kDefaultPitch;
  state.mode = 0;
  state.quality = 0;
  state.slot = 0;
  state.freeze = false;
  return state;
}

inline void ApplyProcessorStaticSettings() {
  clouds::Parameters *parameters = g_engine.processor.mutable_parameters();
  parameters->position = g_engine.smooth.position;
  parameters->size = g_engine.smooth.size;
  parameters->density = g_engine.smooth.density;
  parameters->texture = g_engine.smooth.texture;
  parameters->dry_wet = g_engine.smooth.dry_wet;
  parameters->stereo_spread = IsMonoQuality(g_engine.active_quality)
      ? 0.0f
      : g_engine.smooth.stereo_spread;
  parameters->feedback = g_engine.smooth.feedback;
  parameters->reverb = g_engine.smooth.reverb;
  parameters->pitch = g_engine.smooth.pitch;
  parameters->freeze = g_engine.target.freeze;
  parameters->trigger = false;
  parameters->gate = false;
}

inline void Init() {
  memset(&g_engine, 0, sizeof(g_engine));
  g_engine.target = DefaultUiState();
  g_engine.smooth = g_engine.target;
  g_engine.active_mode = g_engine.target.mode;
  g_engine.active_quality = g_engine.target.quality;
  g_engine.selected_slot = g_engine.target.slot;
  g_engine.maintenance_divider = 0;

  g_engine.processor.Init(
      g_engine.large_buffer,
      kLargeBufferSize,
      g_engine.small_buffer,
      kSmallBufferSize);
  g_engine.processor.set_sample_rate(kSampleRate);
  g_engine.processor.set_playback_mode(PlaybackModeFromIndex(g_engine.active_mode));
  g_engine.processor.set_quality(QualityFromIndex(g_engine.active_quality));
  ApplyProcessorStaticSettings();
  g_engine.processor.Prepare();
}

inline void SetUiState(
    float position,
    float size,
    float density,
    float texture,
    float dry_wet,
    float stereo_spread,
    float feedback,
    float reverb,
    float pitch,
    uint8_t mode,
    uint8_t quality,
    uint8_t slot,
    bool freeze) {
  g_engine.target.position = constrain(position, 0.0f, 1.0f);
  g_engine.target.size = constrain(size, 0.0f, 1.0f);
  g_engine.target.density = constrain(density, 0.0f, 1.0f);
  g_engine.target.texture = constrain(texture, 0.0f, 1.0f);
  g_engine.target.dry_wet = constrain(dry_wet, 0.0f, 1.0f);
  g_engine.target.stereo_spread = constrain(stereo_spread, 0.0f, 1.0f);
  g_engine.target.feedback = constrain(feedback, 0.0f, 0.96f);
  g_engine.target.reverb = constrain(reverb, 0.0f, 1.0f);
  g_engine.target.pitch = constrain(pitch, -24.0f, 24.0f);
  g_engine.target.mode = ClampMode(mode);
  g_engine.target.quality = ClampQuality(quality);
  g_engine.target.slot = ClampSlot(slot);
  g_engine.target.freeze = freeze;
}

inline void RenderBlock(const int32_t *input, int16_t *out_l, int16_t *out_r) {
  for (size_t i = 0; i < kBlockSize; ++i) {
    const float sample = static_cast<float>(input[i]) * DIV_16 * kInputGain;
    g_engine.input[i].l = sample;
    g_engine.input[i].r = sample;
  }

  g_engine.smooth.position = Smooth(g_engine.smooth.position, g_engine.target.position);
  g_engine.smooth.size = Smooth(g_engine.smooth.size, g_engine.target.size);
  g_engine.smooth.density = Smooth(g_engine.smooth.density, g_engine.target.density);
  g_engine.smooth.texture = Smooth(g_engine.smooth.texture, g_engine.target.texture);
  g_engine.smooth.dry_wet = Smooth(g_engine.smooth.dry_wet, g_engine.target.dry_wet);
  g_engine.smooth.stereo_spread = Smooth(g_engine.smooth.stereo_spread, g_engine.target.stereo_spread);
  g_engine.smooth.feedback = Smooth(g_engine.smooth.feedback, g_engine.target.feedback);
  g_engine.smooth.reverb = Smooth(g_engine.smooth.reverb, g_engine.target.reverb);
  g_engine.smooth.pitch = Smooth(g_engine.smooth.pitch, g_engine.target.pitch);

  if (g_engine.target.mode != g_engine.active_mode) {
    g_engine.active_mode = g_engine.target.mode;
    g_engine.maintenance_divider = 0;
    g_engine.processor.set_playback_mode(PlaybackModeFromIndex(g_engine.active_mode));
    g_engine.processor.Prepare();
  }

  if (g_engine.target.quality != g_engine.active_quality) {
    g_engine.active_quality = g_engine.target.quality;
    g_engine.maintenance_divider = 0;
    g_engine.processor.set_quality(QualityFromIndex(g_engine.active_quality));
    g_engine.processor.Prepare();
  }

  ApplyProcessorStaticSettings();
  g_engine.processor.Process(g_engine.input, g_engine.output, kBlockSize);

  switch (g_engine.active_mode) {
    case 1:
      // Stretch mode maintenance runs the WSOLA correlator. Running it every
      // block is expensive on RP2350, so spread the search over several blocks.
      if (++g_engine.maintenance_divider >= kStretchMaintenancePeriodBlocks) {
        g_engine.maintenance_divider = 0;
        g_engine.processor.Prepare();
      }
      break;
    default:
      break;
  }

  const float output_gain = kOutputGain * OutputTrimForMode(g_engine.active_mode);
  const bool mono_output = IsMonoQuality(g_engine.active_quality);
  for (size_t i = 0; i < kBlockSize; ++i) {
    if (mono_output) {
      const float mono = (g_engine.output[i].l + g_engine.output[i].r) * 0.5f;
      const int16_t sample = stmlib::Clip16(static_cast<int32_t>(mono * 32767.0f * output_gain));
      out_l[i] = sample;
      out_r[i] = sample;
    } else {
      out_l[i] = stmlib::Clip16(static_cast<int32_t>(g_engine.output[i].l * 32767.0f * output_gain));
      out_r[i] = stmlib::Clip16(static_cast<int32_t>(g_engine.output[i].r * 32767.0f * output_gain));
    }
  }
}

inline bool SaveSlot(uint8_t slot) {
  g_engine.selected_slot = ClampSlot(slot);
  // Full Clouds sample-memory slots are too large to keep four copies in RP2350
  // SRAM. The hook is kept here so flash-backed slots can be added without
  // changing the UI/audio host.
  g_engine.slot_valid[g_engine.selected_slot] = false;
  return false;
}

inline bool LoadSlot(uint8_t slot) {
  g_engine.selected_slot = ClampSlot(slot);
  return g_engine.slot_valid[g_engine.selected_slot];
}

}  // namespace pico_clouds

#endif  // PICO_EURORACK_CLOUDS_H_
