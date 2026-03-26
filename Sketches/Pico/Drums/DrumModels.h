#pragma once

#include <math.h>
#include "daisysp.h"

namespace drums_internal {

static inline float Clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static inline float Clamp(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static inline float Lerp(float minimum, float maximum, float amount) {
  return minimum + (maximum - minimum) * amount;
}

static inline float Wrap01(float phase) {
  while (phase >= 1.0f) phase -= 1.0f;
  while (phase < 0.0f) phase += 1.0f;
  return phase;
}

static inline float SoftClip(float input) {
  return input / (1.0f + 0.7f * fabsf(input));
}

static inline float DecayCoefficient(float sample_rate, float seconds) {
  if (seconds <= 0.0001f || sample_rate <= 0.0f) return 0.0f;
  return expf(-1.0f / (sample_rate * seconds));
}

class OnePoleLowPass {
 public:
  OnePoleLowPass() : sample_rate_(32000.0f), coefficient_(0.0f), state_(0.0f) {}

  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    state_ = 0.0f;
    SetCutoff(1000.0f);
  }

  void SetCutoff(float cutoff_hz) {
    cutoff_hz = Clamp(cutoff_hz, 5.0f, sample_rate_ * 0.45f);
    coefficient_ = 1.0f - expf(-2.0f * 3.14159265359f * cutoff_hz / sample_rate_);
  }

  inline float Process(float input) {
    state_ += coefficient_ * (input - state_);
    return state_;
  }

 private:
  float sample_rate_;
  float coefficient_;
  float state_;
};

class AnalogSnareDrumLocal {
 public:
  static const int kNumModes = 5;

  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    trig_ = false;

    pulse_remaining_samples_ = 0;
    pulse_ = 0.0f;
    pulse_height_ = 0.0f;
    pulse_lp_ = 0.0f;
    noise_envelope_ = 0.0f;
    sustain_gain_ = 0.0f;

    SetSustain(false);
    SetFreq(200.0f);
    SetDecay(0.3f);
    SetSnappy(0.7f);
    SetTone(0.5f);

    for (int i = 0; i < kNumModes; ++i) {
      resonator_[i].Init(sample_rate_);
      phase_[i] = 0.0f;
    }
    noise_filter_.Init(sample_rate_);
  }

  void Trig() { trig_ = true; }

  void SetSustain(bool sustain) { sustain_ = sustain; }

  void SetFreq(float f0) {
    f0_ = daisysp::fclamp(f0 / sample_rate_, 0.0f, 0.4f);
  }

  void SetTone(float tone) {
    tone_ = daisysp::fclamp(tone, 0.0f, 1.0f) * 2.0f;
  }

  void SetDecay(float decay) { decay_ = decay; }

  void SetSnappy(float snappy) { snappy_ = daisysp::fclamp(snappy, 0.0f, 1.0f); }

  float Process(bool trigger = false) {
    const float decay_xt = decay_ * (1.0f + decay_ * (decay_ - 1.0f));
    const int kTriggerPulseDuration = static_cast<int>(1.0e-3f * sample_rate_);
    const float kPulseDecayTime = 0.1e-3f * sample_rate_;
    const float q = 2000.0f * powf(2.0f, daisysp::kOneTwelfth * decay_xt * 84.0f);
    const float noise_envelope_decay =
      1.0f - 0.0017f * powf(2.0f, daisysp::kOneTwelfth * (-decay_ * (50.0f + snappy_ * 10.0f)));
    const float exciter_leak = snappy_ * (2.0f - snappy_) * 0.1f;

    float snappy = snappy_ * 1.1f - 0.05f;
    snappy = daisysp::fclamp(snappy, 0.0f, 1.0f);

    float tone = tone_;

    if (trigger || trig_) {
      trig_ = false;
      pulse_remaining_samples_ = kTriggerPulseDuration;
      pulse_height_ = 7.2f;
      noise_envelope_ = 2.0f;
    }

    static const float kModeFrequencies[kNumModes] = {
      1.00f, 2.00f, 3.18f, 4.16f, 5.62f
    };

    float f[kNumModes];
    float gain[kNumModes];

    for (int i = 0; i < kNumModes; ++i) {
      f[i] = daisysp::fmin(f0_ * kModeFrequencies[i], 0.499f);
      resonator_[i].SetFreq(f[i] * sample_rate_);
      resonator_[i].SetRes((f[i] * (i == 0 ? q : q * 0.25f)) * 0.2f);
    }

    if (tone < 0.666667f) {
      tone *= 1.5f;
      gain[0] = 1.5f + (1.0f - tone) * (1.0f - tone) * 4.5f;
      gain[1] = 2.0f * tone + 0.15f;
      for (int i = 2; i < kNumModes; ++i) {
        gain[i] = 0.0f;
      }
    } else {
      tone = (tone - 0.666667f) * 3.0f;
      gain[0] = 1.5f - tone * 0.5f;
      gain[1] = 2.15f - tone * 0.7f;
      for (int i = 2; i < kNumModes; ++i) {
        gain[i] = tone;
        tone *= tone;
      }
    }

    float f_noise = f0_ * 16.0f;
    f_noise = daisysp::fclamp(f_noise, 0.0f, 0.499f);
    noise_filter_.SetFreq(f_noise * sample_rate_);
    noise_filter_.SetRes(f_noise * 1.5f);

    float pulse = 0.0f;
    if (pulse_remaining_samples_) {
      --pulse_remaining_samples_;
      pulse = pulse_remaining_samples_ ? pulse_height_ : pulse_height_ - 1.0f;
      pulse_ = pulse;
    } else {
      pulse_ *= 1.0f - 1.0f / kPulseDecayTime;
      pulse = pulse_;
    }

    const float sustain_gain_value = sustain_gain_ = 0.6f * decay_;
    noise_envelope_ *= noise_envelope_decay;
    const float shell_envelope =
      sustain_ ? sustain_gain_value : daisysp::fclamp(noise_envelope_ * 0.5f, 0.0f, 1.0f);

    pulse_lp_ = daisysp::fclamp(pulse_lp_, pulse, 0.75f);

    float shell = 0.0f;
    for (int i = 0; i < kNumModes; ++i) {
      const float excitation =
        i == 0 ? (pulse - pulse_lp_) + 0.006f * pulse : 0.026f * pulse;

      phase_[i] += f[i];
      if (phase_[i] >= 1.0f) phase_[i] -= 1.0f;

      resonator_[i].Process(excitation);

      shell += gain[i]
        * (sustain_
             ? sinf(phase_[i] * TWOPI_F) * sustain_gain_value * 0.25f
             : (resonator_[i].Band() + excitation * exciter_leak) * shell_envelope);
    }
    shell = SoftClip(shell);

    float noise = 2.0f * rand() * daisysp::kRandFrac - 1.0f;
    if (noise < 0.0f) noise = 0.0f;
    noise *= (sustain_ ? sustain_gain_value : noise_envelope_) * snappy * 2.0f;

    noise_filter_.Process(noise);
    noise = noise_filter_.Band();

    return noise + shell * (1.0f - snappy);
  }

 private:
  inline float SoftLimit(float x) {
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
  }

  inline float SoftClip(float x) {
    if (x < -3.0f) return -1.0f;
    if (x > 3.0f) return 1.0f;
    return SoftLimit(x);
  }

  float sample_rate_;
  float f0_;
  float tone_;
  float snappy_;
  float decay_;
  bool sustain_;
  bool trig_;

  int pulse_remaining_samples_;
  float pulse_;
  float pulse_height_;
  float pulse_lp_;
  float noise_envelope_;
  float sustain_gain_;

  daisysp::Svf resonator_[kNumModes];
  daisysp::Svf noise_filter_;
  float phase_[kNumModes];
};

}  // namespace drums_internal


class BassDrum808 {
 public:
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    click_shape_.Init(sample_rate_);
    body_shaper_.Init(sample_rate_);

    tone_ = 0.34f;
    decay_seconds_ = 0.62f;
    freq_hz_ = 42.0f;
    sustain_ = 0.0f;
    attack_fm_amount_ = 0.4f;
    self_fm_amount_ = 0.5f;

    phase_ = 0.0f;
    amp_env_ = 0.0f;
    pitch_env_ = 0.0f;
    transient_ = 0.0f;

    UpdateCoefficients();
  }

  void SetTone(float tone) {
    tone_ = drums_internal::Clamp01(tone * 4.0f);
    UpdateCoefficients();
  }

  void SetDecay(float decay) {
    decay_seconds_ = drums_internal::Clamp(decay, 0.02f, 2.0f);
    UpdateCoefficients();
  }

  void SetFreq(float freq) { freq_hz_ = drums_internal::Clamp(freq, 10.0f, 240.0f); }

  void SetSustain(float sustain) {
    sustain_ = drums_internal::Clamp01(sustain);
    UpdateCoefficients();
  }

  void SetAttackFmAmount(float attack_fm_amount) {
    attack_fm_amount_ = drums_internal::Clamp01(attack_fm_amount * 4.0f);
  }

  void SetSelfFmAmount(float self_fm_amount) {
    self_fm_amount_ = drums_internal::Clamp01(self_fm_amount);
    UpdateCoefficients();
  }

  void Trig() {
    phase_ = 0.0f;

    amp_env_ = 1.14f + sustain_ * 0.20f;
    pitch_env_ = (0.55f + attack_fm_amount_ * 0.85f) * 0.9375f;
    transient_ = 1.27f + tone_ * 0.60f + attack_fm_amount_ * 0.80f;
  }

  float Process() {
    amp_env_ *= amp_decay_;
    pitch_env_ *= pitch_decay_;
    transient_ *= transient_decay_;

    const float fm_envelope = drums_internal::Lerp(1.45f, 0.50f, tone_);
    float freq = freq_hz_ * (1.0f + pitch_env_ * fm_envelope);
    freq = drums_internal::Clamp(freq, 10.0f, sample_rate_ * 0.25f);

    phase_ = drums_internal::Wrap01(phase_ + freq / sample_rate_);

    const float self_fm = amp_env_ * (0.03f + 0.18f * self_fm_amount_);
    const float warped_phase =
      drums_internal::Wrap01(phase_ + sinf(phase_ * 6.28318530718f) * self_fm);

    const float fundamental = sinf(warped_phase * 6.28318530718f);
    const float overtone =
      sinf(warped_phase * 12.56637061436f) * (0.06f + 0.14f * tone_ + 0.08f * self_fm_amount_);
    const float body = body_shaper_.Process(fundamental + overtone) * amp_env_;
    const float click = click_shape_.Process(transient_) * transient_
      * (0.28f + 0.34f * tone_ + 0.20f * attack_fm_amount_);
    const float sustain_body =
      body_shaper_.Process(fundamental * 0.25f) * sustain_ * 0.08f;

    const float raw = ((body * 1.18f) + click + sustain_body) * 0.953f;

    // Match the existing DRUMS.ino gain staging where MODEL_GAIN[0] is 8.0f.
    return drums_internal::SoftClip(raw) * 0.125f;
  }

 private:
  void UpdateCoefficients() {
    const float decay_norm = drums_internal::Clamp01((decay_seconds_ - 0.02f) / 1.98f);
    const float amp_seconds =
      drums_internal::Clamp(decay_seconds_ + sustain_ * 0.30f, 0.02f, 2.2f);

    amp_decay_ = drums_internal::DecayCoefficient(sample_rate_, amp_seconds);
    pitch_decay_ = drums_internal::DecayCoefficient(
      sample_rate_, drums_internal::Lerp(0.007f, 0.040f, decay_norm));
    transient_decay_ = drums_internal::DecayCoefficient(
      sample_rate_, drums_internal::Lerp(0.0009f, 0.0032f, tone_));

    click_shape_.SetCutoff(
      drums_internal::Lerp(1800.0f, 7600.0f, tone_) + attack_fm_amount_ * 1200.0f);
    body_shaper_.SetCutoff(
      drums_internal::Lerp(1200.0f, 4200.0f, tone_) + self_fm_amount_ * 600.0f);
  }

  float sample_rate_;
  float tone_;
  float decay_seconds_;
  float freq_hz_;
  float sustain_;
  float attack_fm_amount_;
  float self_fm_amount_;

  drums_internal::OnePoleLowPass click_shape_;
  drums_internal::OnePoleLowPass body_shaper_;

  float phase_;
  float amp_env_;
  float pitch_env_;
  float transient_;
  float amp_decay_;
  float pitch_decay_;
  float transient_decay_;
};

class BassDrum909 {
 public:
  void Init(float sample_rate) { drum_.Init(sample_rate); }

  void SetTone(float tone) { drum_.SetTone(tone); }
  void SetDecay(float decay) { drum_.SetDecay(decay); }
  void SetFreq(float freq) { drum_.SetFreq(freq); }
  void SetDirtiness(float dirtiness) { drum_.SetDirtiness(dirtiness); }
  void SetFmEnvelopeAmount(float amount) { drum_.SetFmEnvelopeAmount(amount); }
  void SetFmEnvelopeDecay(float decay) { drum_.SetFmEnvelopeDecay(decay); }

  void Trig() { drum_.Trig(); }

  float Process() { return drum_.Process(); }

 private:
  daisysp::SyntheticBassDrum drum_;
};

class SnareDrum808 {
 public:
  void Init(float sample_rate) { drum_.Init(sample_rate); }
  

  void SetSustain(float sustain) { drum_.SetSustain(sustain); }
  void SetDecay(float decay) { drum_.SetDecay(decay); }
  void SetFreq(float freq) { drum_.SetFreq(freq); }
  void SetTone(float tone) { drum_.SetTone(tone); }
  void SetSnappy(float snappy) { drum_.SetSnappy(snappy); }
  
  void Trig() { drum_.Trig(); }

  float Process() { return drum_.Process(); }

 private:
  drums_internal::AnalogSnareDrumLocal drum_;
  
};

class SnareDrum909 {
 public:
  void Init(float sample_rate) { drum_.Init(sample_rate); }
  

  void SetSustain(float sustain) { drum_.SetSustain(sustain); }
  void SetDecay(float decay) { drum_.SetDecay(decay); }
  void SetFreq(float freq) { drum_.SetFreq(freq); }
  void SetFmAmount(float amount) { drum_.SetFmAmount(amount); }
  void SetSnappy(float snappy) { drum_.SetSnappy(snappy); }
  
  void Trig() { drum_.Trig(); }

  float Process() { return drum_.Process(); }

 private:
  daisysp::SyntheticSnareDrum drum_;
  
};

class Hihat {
 public:
  void Init(float sample_rate) { drum_.Init(sample_rate); }
  

  void SetSustain(float sustain) { drum_.SetSustain(sustain); }
  void SetDecay(float decay) { drum_.SetDecay(decay); }
  void SetFreq(float freq) { drum_.SetFreq(freq); }
  void SetNoisiness(float noisiness) { drum_.SetNoisiness(noisiness); }
  void SetTone(float tone) { drum_.SetTone(tone); }
  void Trig() { drum_.Trig(); }

  float Process() { return drum_.Process(); }

 private:
  daisysp::HiHat <> drum_;

  
};
