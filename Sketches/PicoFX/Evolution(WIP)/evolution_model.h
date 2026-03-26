#pragma once

#include <Arduino.h>

class EvolutionModel {
public:
  static constexpr uint32_t kBufferSize = 65536;
  static constexpr uint32_t kBufferMask = kBufferSize - 1;
  static constexpr uint32_t kLiveDelaySamples = 4096;

  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    write_pos_ = 0;
    seed_playhead_ = 0.0f;
    live_delay_samples_ = kLiveDelaySamples;
    seed_locked_ = false;
    snapshot_pending_ = false;
    snapshot_busy_ = false;
    snapshot_copy_index_ = 0;
    snapshot_source_start_ = 0;
    feedback_state_ = 0.0f;
    hp_state_ = 0.0f;
    lp_state_ = 0.0f;
    rng_state_ = 1u;

    for (uint8_t i = 0; i < 4; ++i) {
      axis_drive_[i] = 0.0f;
      axis_release_[i] = 0.0f;
      axis_amount_[i] = 0.0f;
      lfo_phase_[i] = 0.19f * (float)(i + 1);
    }

    for (uint32_t i = 0; i < kBufferSize; ++i) {
      live_buffer_[i] = 0;
      seed_buffer_[i] = 0;
    }
  }

  void SetDirectionControl(uint8_t index, float control) {
    if (index >= 4) return;

    control = Clamp(control, -1.0f, 1.0f);

    if (control >= 0.0f) {
      axis_drive_[index] = control;
      axis_release_[index] = 0.0f;
    } else {
      axis_drive_[index] = 0.0f;
      axis_release_[index] = -control;
    }
  }

  void RequestSeedCapture() {
    snapshot_pending_ = true;
  }

  bool SeedLocked() const {
    return seed_locked_;
  }

  bool SnapshotBusy() const {
    return snapshot_busy_;
  }

  int16_t Process(int16_t input) {
    StoreLiveSample(input);
    HandleSnapshotCopy();
    UpdateAxisAmounts();
    AdvanceLfos();

    float base_pos = seed_locked_ ? seed_playhead_ : LiveBasePosition();
    float base = ReadActive(base_pos);

    float stage = base;

    // Direction 1: temporal drift and motion around the seed.
    float drift_lfo = TriangleLfo(lfo_phase_[0]);
    float drift_range = 6.0f + axis_amount_[0] * 220.0f;
    float drift_offset = drift_range * (0.7f * drift_lfo + 0.3f * NoiseSigned());
    float drifted = ReadActive(base_pos + drift_offset);
    stage = Mix(stage, drifted, axis_amount_[0]);

    // Direction 2: cloud / diffusion via nearby taps from the same source buffer.
    float cloud_span = 10.0f + axis_amount_[1] * 320.0f;
    float tap_a = ReadActive(base_pos - cloud_span);
    float tap_b = ReadActive(base_pos + cloud_span);
    float tap_c = ReadActive(base_pos + cloud_span * (0.35f + 0.25f * TriangleLfo(lfo_phase_[1])));
    float diffused = 0.4f * stage + 0.2f * tap_a + 0.2f * tap_b + 0.2f * tap_c;
    stage = Mix(stage, diffused, axis_amount_[1]);

    // Direction 3: spectral bloom, bringing out edges and soft saturation.
    lp_state_ += 0.02f * (stage - lp_state_);
    float high = stage - lp_state_;
    float bright = SoftClip(stage + high * (0.4f + 2.2f * axis_amount_[2]));
    stage = Mix(stage, bright, axis_amount_[2]);

    // Direction 4: regenerative memory built from the evolving signal itself.
    float regen_span = 25.0f + axis_amount_[3] * 700.0f;
    float regen_tap = ReadActive(base_pos - regen_span * (0.75f + 0.25f * TriangleLfo(lfo_phase_[3])));
    feedback_state_ = Clamp(feedback_state_ * (0.92f + 0.05f * axis_amount_[3]) + 0.12f * regen_tap + 0.08f * stage, -1.0f, 1.0f);
    float regenerated = SoftClip(stage + feedback_state_ * (0.2f + 0.65f * axis_amount_[3]));
    stage = Mix(stage, regenerated, axis_amount_[3]);

    hp_state_ += 0.005f * (stage - hp_state_);
    float final_out = stage - 0.2f * hp_state_;
    final_out = Clamp(final_out * 0.8f, -1.0f, 1.0f);

    if (seed_locked_) {
      float speed_mod = 0.14f * axis_amount_[0] * TriangleLfo(lfo_phase_[0]) +
                        0.06f * axis_amount_[1] * TriangleLfo(lfo_phase_[1]);
      float speed = 1.0f + speed_mod;
      if (speed < 0.2f) speed = 0.2f;
      seed_playhead_ = Wrap(seed_playhead_ + speed);
    }

    return FloatToInt16(final_out);
  }

private:
  static constexpr uint16_t kSnapshotCopyPerSample = 16;

  int16_t live_buffer_[kBufferSize];
  int16_t seed_buffer_[kBufferSize];
  uint32_t write_pos_;
  uint32_t live_delay_samples_;

  float sample_rate_;
  float seed_playhead_;
  float axis_drive_[4];
  float axis_release_[4];
  float axis_amount_[4];
  float lfo_phase_[4];
  float feedback_state_;
  float hp_state_;
  float lp_state_;
  uint32_t rng_state_;

  bool seed_locked_;
  bool snapshot_pending_;
  bool snapshot_busy_;
  uint32_t snapshot_copy_index_;
  uint32_t snapshot_source_start_;

  static float Clamp(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
  }

  static float Mix(float dry, float wet, float amount) {
    amount = Clamp(amount, 0.0f, 1.0f);
    return dry + (wet - dry) * amount;
  }

  static float SoftClip(float x) {
    x = Clamp(x, -1.5f, 1.5f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
  }

  static int16_t FloatToInt16(float value) {
    value = Clamp(value, -1.0f, 1.0f);
    return (int16_t)(value * 32767.0f);
  }

  float TriangleLfo(float phase) const {
    phase -= floorf(phase);
    float tri = 1.0f - 4.0f * fabsf(phase - 0.5f);
    return Clamp(tri, -1.0f, 1.0f);
  }

  float NoiseSigned() {
    rng_state_ = rng_state_ * 1664525u + 1013904223u;
    return ((float)((rng_state_ >> 8) & 0xffffu) / 32767.5f) - 1.0f;
  }

  float Wrap(float position) const {
    while (position >= (float)kBufferSize) position -= (float)kBufferSize;
    while (position < 0.0f) position += (float)kBufferSize;
    return position;
  }

  uint32_t WrapIndex(int32_t index) const {
    return (uint32_t)index & kBufferMask;
  }

  float ReadBuffer(const int16_t* buffer, float position) const {
    position = Wrap(position);

    int32_t index_a = (int32_t)position;
    int32_t index_b = index_a + 1;
    float frac = position - (float)index_a;

    float a = (float)buffer[WrapIndex(index_a)] / 32768.0f;
    float b = (float)buffer[WrapIndex(index_b)] / 32768.0f;

    return a + (b - a) * frac;
  }

  float ReadActive(float position) const {
    return seed_locked_ ? ReadBuffer(seed_buffer_, position) : ReadBuffer(live_buffer_, position);
  }

  float LiveBasePosition() const {
    int32_t delayed = (int32_t)write_pos_ - (int32_t)live_delay_samples_;
    return (float)WrapIndex(delayed);
  }

  void StoreLiveSample(int16_t input) {
    live_buffer_[write_pos_] = input;
    write_pos_ = (write_pos_ + 1u) & kBufferMask;
  }

  void StartSnapshot() {
    snapshot_pending_ = false;
    snapshot_busy_ = true;
    snapshot_copy_index_ = 0;
    snapshot_source_start_ = write_pos_;
    seed_locked_ = false;
  }

  void HandleSnapshotCopy() {
    if (snapshot_pending_ && !snapshot_busy_) {
      StartSnapshot();
    }

    if (!snapshot_busy_) return;

    for (uint16_t i = 0; i < kSnapshotCopyPerSample; ++i) {
      if (snapshot_copy_index_ >= kBufferSize) {
        snapshot_busy_ = false;
        seed_locked_ = true;
        seed_playhead_ = 0.0f;
        feedback_state_ = 0.0f;
        hp_state_ = 0.0f;
        lp_state_ = 0.0f;
        return;
      }

      uint32_t src = (snapshot_source_start_ + snapshot_copy_index_) & kBufferMask;
      seed_buffer_[snapshot_copy_index_] = live_buffer_[src];
      ++snapshot_copy_index_;
    }
  }

  void UpdateAxisAmounts() {
    for (uint8_t i = 0; i < 4; ++i) {
      float chase = 0.00015f + 0.0012f * axis_drive_[i] * axis_drive_[i];
      axis_amount_[i] += (axis_drive_[i] - axis_amount_[i]) * chase;

      float retreat = 0.00008f + 0.0016f * axis_release_[i] * axis_release_[i];
      axis_amount_[i] *= (1.0f - retreat);

      if (axis_amount_[i] < 0.0002f) axis_amount_[i] = 0.0f;
      if (axis_amount_[i] > 1.0f) axis_amount_[i] = 1.0f;
    }
  }

  void AdvanceLfos() {
    for (uint8_t i = 0; i < 4; ++i) {
      float rate_hz = 0.03f + axis_amount_[i] * (0.12f + 0.07f * (float)i);
      lfo_phase_[i] += rate_hz / sample_rate_;
      if (lfo_phase_[i] >= 1.0f) lfo_phase_[i] -= 1.0f;
    }
  }
};
