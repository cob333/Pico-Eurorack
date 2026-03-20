#pragma once

#include <math.h>
#include <stdint.h>

enum DrumModelId : uint8_t {
  MODEL_808KICK = 0,
  MODEL_HIHATS,
  MODEL_SNARE,
  NUM_DRUM_MODELS
};

struct DrumParameters {
  float tune;
  float tone;
  float level;
  float decay;
};

static const DrumParameters kDefaultDrumParameters[NUM_DRUM_MODELS] = {
  {0.26f, 0.34f, 0.90f, 0.62f},
  {0.45f, 0.64f, 0.82f, 0.22f},
  {0.42f, 0.58f, 0.86f, 0.40f},
};

static const float kDrumOutputGain[NUM_DRUM_MODELS] = {
  1.05f,
  0.52f,
  0.88f,
};

static inline float DrumClamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

static inline float DrumLerp(float minimum, float maximum, float amount) {
  return minimum + (maximum - minimum) * amount;
}

static inline float DrumWrap01(float phase) {
  while (phase >= 1.0f) phase -= 1.0f;
  while (phase < 0.0f) phase += 1.0f;
  return phase;
}

static inline float DrumSoftClip(float input) {
  return input / (1.0f + 0.7f * fabsf(input));
}

static inline float DrumDecayCoefficient(float sampleRate, float seconds) {
  if (seconds <= 0.0001f) return 0.0f;
  return expf(-1.0f / (sampleRate * seconds));
}

class DrumNoise {
 public:
  DrumNoise() : state_(0x12345678u) {}

  inline float Process() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return ((state_ & 0x00ffffffu) * (1.0f / 8388607.5f)) - 1.0f;
  }

 private:
  uint32_t state_;
};

class OnePoleLowPass {
 public:
  OnePoleLowPass() : sampleRate_(32000.0f), coefficient_(0.0f), state_(0.0f) {}

  void Init(float sampleRate) {
    sampleRate_ = sampleRate;
    state_ = 0.0f;
    SetCutoff(1000.0f);
  }

  void SetCutoff(float cutoffHz) {
    if (cutoffHz < 5.0f) cutoffHz = 5.0f;
    float maxCutoff = sampleRate_ * 0.45f;
    if (cutoffHz > maxCutoff) cutoffHz = maxCutoff;
    coefficient_ = 1.0f - expf(-2.0f * 3.14159265359f * cutoffHz / sampleRate_);
  }

  inline float Process(float input) {
    state_ += coefficient_ * (input - state_);
    return state_;
  }

  void Reset(float value = 0.0f) { state_ = value; }

 private:
  float sampleRate_;
  float coefficient_;
  float state_;
};

class OnePoleHighPass {
 public:
  void Init(float sampleRate) {
    lowpass_.Init(sampleRate);
    lowpass_.SetCutoff(1000.0f);
  }

  void SetCutoff(float cutoffHz) { lowpass_.SetCutoff(cutoffHz); }

  inline float Process(float input) {
    return input - lowpass_.Process(input);
  }

  void Reset(float value = 0.0f) { lowpass_.Reset(value); }

 private:
  OnePoleLowPass lowpass_;
};

class Kick808Voice {
 public:
  void Init(float sampleRate) {
    sampleRate_ = sampleRate;
    clickShape_.Init(sampleRate_);
    clickShape_.SetCutoff(800.0f);
    bodyShaper_.Init(sampleRate_);
    bodyShaper_.SetCutoff(2500.0f);
    parameters_ = kDefaultDrumParameters[MODEL_808KICK];
    phase_ = 0.0f;
    ampEnv_ = 0.0f;
    pitchEnv_ = 0.0f;
    transient_ = 0.0f;
    UpdateCoefficients();
  }

  void SetParameters(const DrumParameters& parameters) {
    parameters_ = Sanitize(parameters);
    UpdateCoefficients();
  }

  void Trigger() {
    phase_ = 0.0f;
    ampEnv_ = 1.25f;
    pitchEnv_ = 1.18f;
    transient_ = 1.85f;
  }

  inline float Process() {
    ampEnv_ *= ampDecay_;
    pitchEnv_ *= pitchDecay_;
    transient_ *= transientDecay_;

    float baseFreq = DrumLerp(28.0f, 78.0f, parameters_.tune);
    float freq = baseFreq * (1.0f + pitchEnv_ * DrumLerp(1.45f, 0.50f, parameters_.tone));
    phase_ = DrumWrap01(phase_ + freq / sampleRate_);

    float fundamental = sinf(phase_ * 6.28318530718f);
    float overtone = sinf(phase_ * 12.56637061436f) * (0.06f + 0.14f * parameters_.tone);
    float body = bodyShaper_.Process(fundamental + overtone) * ampEnv_;
    float click = clickShape_.Process(transient_) * transient_
      * (0.40f + 0.40f * parameters_.tone);

    return DrumSoftClip((body * 1.18f) + click);
  }

 private:
  static DrumParameters Sanitize(const DrumParameters& input) {
    DrumParameters output = input;
    output.tune = DrumClamp01(output.tune);
    output.tone = DrumClamp01(output.tone);
    output.level = DrumClamp01(output.level);
    output.decay = DrumClamp01(output.decay);
    return output;
  }

  void UpdateCoefficients() {
    ampDecay_ = DrumDecayCoefficient(sampleRate_, DrumLerp(0.060f, 1.80f, parameters_.decay));
    pitchDecay_ = DrumDecayCoefficient(sampleRate_, DrumLerp(0.007f, 0.040f, parameters_.decay));
    transientDecay_ = DrumDecayCoefficient(sampleRate_, DrumLerp(0.0009f, 0.0032f, parameters_.tone));
    clickShape_.SetCutoff(DrumLerp(1800.0f, 7600.0f, parameters_.tone));
    bodyShaper_.SetCutoff(DrumLerp(1200.0f, 4200.0f, parameters_.tone));
  }

  float sampleRate_;
  DrumParameters parameters_;
  OnePoleLowPass clickShape_;
  OnePoleLowPass bodyShaper_;
  float phase_;
  float ampEnv_;
  float pitchEnv_;
  float transient_;
  float ampDecay_;
  float pitchDecay_;
  float transientDecay_;
};

class HiHatVoice {
 public:
  void Init(float sampleRate) {
    sampleRate_ = sampleRate;
    band1Lp_.Init(sampleRate_);
    band1Hp_.Init(sampleRate_);
    band2Lp_.Init(sampleRate_);
    band2Hp_.Init(sampleRate_);
    noiseHp_.Init(sampleRate_);
    for (uint8_t i = 0; i < 6; ++i) {
      phase_[i] = 0.0f;
    }
    env_ = 0.0f;
    transient_ = 0.0f;
    parameters_ = kDefaultDrumParameters[MODEL_HIHATS];
    UpdateCoefficients();
  }

  void SetParameters(const DrumParameters& parameters) {
    parameters_ = Sanitize(parameters);
    UpdateCoefficients();
  }

  void Trigger() {
    env_ = 1.05f;
    transient_ = 1.65f;
  }

  inline float Process() {
    static const float kBaseFrequencies[6] = {
      205.3f, 304.4f, 369.6f, 522.7f, 800.0f, 540.0f
    };

    float tuneScale = DrumLerp(0.72f, 1.55f, parameters_.tune);
    float cluster = 0.0f;
    for (uint8_t i = 0; i < 6; ++i) {
      phase_[i] = DrumWrap01(phase_[i] + (kBaseFrequencies[i] * tuneScale) / sampleRate_);
      cluster += (phase_[i] < 0.5f) ? 1.0f : -1.0f;
    }
    cluster *= (1.0f / 6.0f);

    env_ *= envDecay_;
    transient_ *= transientDecay_;

    float hiss = noise_.Process() * 0.62f;
    float source = (cluster * 0.82f) + hiss;
    float band1 = band1Hp_.Process(band1Lp_.Process(source));
    float band2 = band2Hp_.Process(band2Lp_.Process(source));
    float noiseBand = noiseHp_.Process((hiss * 1.18f) + (transient_ * 0.92f));

    float tone = parameters_.tone;
    float output = env_ * (
      band1 * (0.62f - 0.20f * tone) +
      band2 * (0.14f + 0.70f * tone) +
      noiseBand * (0.92f + 1.02f * tone)
    );

    return DrumSoftClip(output * 1.46f);
  }

 private:
  static DrumParameters Sanitize(const DrumParameters& input) {
    DrumParameters output = input;
    output.tune = DrumClamp01(output.tune);
    output.tone = DrumClamp01(output.tone);
    output.level = DrumClamp01(output.level);
    output.decay = DrumClamp01(output.decay);
    return output;
  }

  void UpdateCoefficients() {
    envDecay_ = DrumDecayCoefficient(sampleRate_, DrumLerp(0.006f, 0.55f, parameters_.decay));
    transientDecay_ = DrumDecayCoefficient(sampleRate_, 0.0022f);
    band1Hp_.SetCutoff(2400.0f);
    band1Lp_.SetCutoff(DrumLerp(4300.0f, 7600.0f, parameters_.tone));
    band2Hp_.SetCutoff(DrumLerp(4200.0f, 7200.0f, parameters_.tone));
    band2Lp_.SetCutoff(DrumLerp(9200.0f, 14000.0f, parameters_.tone));
    noiseHp_.SetCutoff(DrumLerp(5000.0f, 9000.0f, parameters_.tone));
  }

  float sampleRate_;
  DrumParameters parameters_;
  DrumNoise noise_;
  OnePoleLowPass band1Lp_;
  OnePoleHighPass band1Hp_;
  OnePoleLowPass band2Lp_;
  OnePoleHighPass band2Hp_;
  OnePoleHighPass noiseHp_;
  float phase_[6];
  float env_;
  float transient_;
  float envDecay_;
  float transientDecay_;
};

class SnareVoice {
 public:
  void Init(float sampleRate) {
    sampleRate_ = sampleRate;
    noiseLp_.Init(sampleRate_);
    noiseHp_.Init(sampleRate_);
    clickHp_.Init(sampleRate_);
    clickLp_.Init(sampleRate_);
    parameters_ = kDefaultDrumParameters[MODEL_SNARE];
    phase1_ = 0.0f;
    phase2_ = 0.0f;
    bodyEnv_ = 0.0f;
    noiseEnv_ = 0.0f;
    transient_ = 0.0f;
    UpdateCoefficients();
  }

  void SetParameters(const DrumParameters& parameters) {
    parameters_ = Sanitize(parameters);
    UpdateCoefficients();
  }

  void Trigger() {
    phase1_ = 0.0f;
    phase2_ = 0.0f;
    bodyEnv_ = 1.00f;
    noiseEnv_ = 1.25f;
    transient_ = 1.60f;
  }

  inline float Process() {
    bodyEnv_ *= bodyDecay_;
    noiseEnv_ *= noiseDecay_;
    transient_ *= transientDecay_;

    float baseFreq = DrumLerp(170.0f, 330.0f, parameters_.tune);
    float bodyDrop = 1.0f + (bodyEnv_ * 0.06f);
    phase1_ = DrumWrap01(phase1_ + (baseFreq * bodyDrop) / sampleRate_);
    phase2_ = DrumWrap01(phase2_ + (baseFreq * 1.82f * bodyDrop) / sampleRate_);

    float tone = parameters_.tone;
    float body = (
      sinf(phase1_ * 6.28318530718f) * (0.78f - 0.20f * tone) +
      sinf(phase2_ * 6.28318530718f) * (0.20f + 0.25f * tone)
    ) * bodyEnv_;

    float noise = noiseHp_.Process(noiseLp_.Process(noiseGen_.Process()));
    float snappy = noise * noiseEnv_ * (0.82f + 1.18f * tone);
    float click = clickHp_.Process(clickLp_.Process(transient_)) * transient_
      * (0.22f + 0.38f * tone);

    return DrumSoftClip((body * 1.08f) + snappy + click);
  }

 private:
  static DrumParameters Sanitize(const DrumParameters& input) {
    DrumParameters output = input;
    output.tune = DrumClamp01(output.tune);
    output.tone = DrumClamp01(output.tone);
    output.level = DrumClamp01(output.level);
    output.decay = DrumClamp01(output.decay);
    return output;
  }

  void UpdateCoefficients() {
    bodyDecay_ = DrumDecayCoefficient(sampleRate_, DrumLerp(0.020f, 0.42f, parameters_.decay));
    noiseDecay_ = DrumDecayCoefficient(sampleRate_, DrumLerp(0.018f, 0.55f, parameters_.decay));
    transientDecay_ = DrumDecayCoefficient(sampleRate_, 0.0022f);
    noiseLp_.SetCutoff(DrumLerp(5200.0f, 13000.0f, parameters_.tone));
    noiseHp_.SetCutoff(DrumLerp(1400.0f, 4200.0f, parameters_.tone));
    clickLp_.SetCutoff(DrumLerp(3600.0f, 10500.0f, parameters_.tone));
    clickHp_.SetCutoff(2200.0f);
  }

  float sampleRate_;
  DrumParameters parameters_;
  DrumNoise noiseGen_;
  OnePoleLowPass noiseLp_;
  OnePoleHighPass noiseHp_;
  OnePoleHighPass clickHp_;
  OnePoleLowPass clickLp_;
  float phase1_;
  float phase2_;
  float bodyEnv_;
  float noiseEnv_;
  float transient_;
  float bodyDecay_;
  float noiseDecay_;
  float transientDecay_;
};

class DrumVoiceBank {
 public:
  void Init(float sampleRate) {
    sampleRate_ = sampleRate;
    currentModel_ = MODEL_808KICK;

    kick808_.Init(sampleRate_);
    hats_.Init(sampleRate_);
    snare_.Init(sampleRate_);

    for (uint8_t i = 0; i < NUM_DRUM_MODELS; ++i) {
      parameters_[i] = kDefaultDrumParameters[i];
      SetParameters(i, parameters_[i]);
    }
  }

  void SetModel(uint8_t model) {
    if (model >= NUM_DRUM_MODELS) model = MODEL_808KICK;
    currentModel_ = model;
  }

  void SetParameters(uint8_t model, const DrumParameters& parameters) {
    if (model >= NUM_DRUM_MODELS) return;

    parameters_[model].tune = DrumClamp01(parameters.tune);
    parameters_[model].tone = DrumClamp01(parameters.tone);
    parameters_[model].level = DrumClamp01(parameters.level);
    parameters_[model].decay = DrumClamp01(parameters.decay);

    switch (model) {
      case MODEL_808KICK:
        kick808_.SetParameters(parameters_[model]);
        break;
      case MODEL_HIHATS:
        hats_.SetParameters(parameters_[model]);
        break;
      default:
        snare_.SetParameters(parameters_[model]);
        break;
    }
  }

  void Trigger() {
    switch (currentModel_) {
      case MODEL_808KICK:
        kick808_.Trigger();
        break;
      case MODEL_HIHATS:
        hats_.Trigger();
        break;
      default:
        snare_.Trigger();
        break;
    }
  }

  inline float Process() {
    switch (currentModel_) {
      case MODEL_808KICK:
        return kick808_.Process() * kDrumOutputGain[MODEL_808KICK];
      case MODEL_HIHATS:
        return hats_.Process() * kDrumOutputGain[MODEL_HIHATS];
      default:
        return snare_.Process() * kDrumOutputGain[MODEL_SNARE];
    }
  }

 private:
  float sampleRate_;
  uint8_t currentModel_;
  DrumParameters parameters_[NUM_DRUM_MODELS];
  Kick808Voice kick808_;
  HiHatVoice hats_;
  SnareVoice snare_;
};
