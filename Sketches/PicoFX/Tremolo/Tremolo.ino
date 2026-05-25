// Tremolo effect for 2HPico
// Author: Generated
// Contributor: Wenhao Yang
//
// Top Jack - Audio input
// Middle jack - Speed CV (aux)
// Bottom Jack - Audio out
//
// Panel mapping (single parameter page):
// Pot 1 - Tremolo Depth (0-1)
// Pot 2 - Tremolo Speed (Hz, exponential mapping)
// Pot 3 - Dry/Wet Mix (0 = dry, 1 = wet)
// Pot 4 - Level (output gain)

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "pico/multicore.h"

#define DEBUG
#define MONITOR_CPU1

#define SAMPLERATE 44100

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S i2s(INPUT_PULLUP);

float samplerate = SAMPLERATE;

constexpr float TREMOLO_MIN_RATE_HZ = 0.05f;
constexpr float TREMOLO_MAX_RATE_HZ = 20.0f;
constexpr float TREMOLO_DEFAULT_DEPTH = 0.8f;
constexpr float TREMOLO_DEFAULT_RATE_HZ = 4.0f;
constexpr float TREMOLO_DEFAULT_MIX = 0.8f;
constexpr float TREMOLO_DEFAULT_LEVEL = 0.9f;
constexpr uint8_t TREMOLO_CONTROL_POLL_DIVIDER = 32;

class SineLfo {
public:
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    phase_ = 0.0f;
    phase_inc_ = 0.0f;
    SetRateHz(TREMOLO_DEFAULT_RATE_HZ);
  }

  void SetRateHz(float hz) {
    if (hz < TREMOLO_MIN_RATE_HZ) hz = TREMOLO_MIN_RATE_HZ;
    if (hz > TREMOLO_MAX_RATE_HZ) hz = TREMOLO_MAX_RATE_HZ;
    phase_inc_ = hz / sample_rate_;
  }

  // Returns 0..1 sine wave
  float Process() {
    phase_ += phase_inc_;
    while (phase_ >= 1.0f) phase_ -= 1.0f;
    return 0.5f * (1.0f + sinf(phase_ * 2.0f * 3.14159265358979323846f));
  }

private:
  float sample_rate_;
  float phase_;
  float phase_inc_;
};

struct SharedSettings {
  volatile uint32_t revision;
  volatile float depth;
  volatile float rate_hz;
  volatile float mix;
  volatile float level;
};

struct SharedLedState {
  volatile uint32_t revision;
  volatile float vu;
};

SharedSettings settings = {0, TREMOLO_DEFAULT_DEPTH, TREMOLO_DEFAULT_RATE_HZ, TREMOLO_DEFAULT_MIX, TREMOLO_DEFAULT_LEVEL};
SharedLedState ledstate = {0, 0.0f};

SineLfo tremLfo;

uint32_t control_counter = 0;

float MapRateHz(uint16_t value) {
  float normalized = mapf(value, 0, AD_RANGE - 1, 0.0f, 1.0f);
  return TREMOLO_MIN_RATE_HZ * expf(logf(TREMOLO_MAX_RATE_HZ / TREMOLO_MIN_RATE_HZ) * normalized);
}

void PublishSettings(float depth, float rate_hz, float mix, float level) {
  ++settings.revision; // odd = update in progress
  settings.depth = depth;
  settings.rate_hz = rate_hz;
  settings.mix = mix;
  settings.level = level;
  ++settings.revision; // even = stable
}

void PublishLedState(float vu) {
  ++ledstate.revision;
  ledstate.vu = vu;
  ++ledstate.revision;
}

void setup() {
  Serial.begin(115200);

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

  LEDS.begin();
  LEDS.setPixelColor(0, RED);
  LEDS.show();

  analogReadResolution(AD_BITS);

  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK);
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();

  tremLfo.Init(samplerate);
  PublishSettings(settings.depth, settings.rate_hz, settings.mix, settings.level);
}

void loop() {
  static uint32_t parameterupdate = 0;
  uint32_t now = millis();

  if ((now - parameterupdate) > PARAMETERUPDATE) {
    parameterupdate = now;
    samplepots();

    float depth = settings.depth;
    float base_rate = settings.rate_hz;
    float mix = settings.mix;
    float level = settings.level;

    if (!potlock[0]) depth = mapf(pot[0], 0, AD_RANGE - 1, 0.0f, 1.0f);
    if (!potlock[1]) base_rate = MapRateHz(pot[1]);
    if (!potlock[2]) mix = mapf(pot[2], 0, AD_RANGE - 1, 0.0f, 1.0f);
    if (!potlock[3]) level = mapf(pot[3], 0, AD_RANGE - 1, 0.0f, 1.0f);

    // read aux CV (middle jack = CV2) and use it to scale speed multiplicatively (1x .. 10x)
    uint16_t cv = sampleCV2();
    float cv_norm = mapf(cv, 0, AD_RANGE - 1, 0.0f, 1.0f);
    float rate_hz = base_rate * (1.0f + cv_norm * 9.0f);

    PublishSettings(depth, rate_hz, mix, level);
  }
}

// second core setup
void setup1() {
  delay(1000); // wait for main core to start up peripherals
  tremLfo.Init(samplerate); // init lfo on core1
}

void loop1() {
  while (true) {
    int32_t left = i2s.read();
    int32_t right = i2s.read();

#ifdef MONITOR_CPU1
    digitalWrite(CPU_USE, 1);
#endif

    float sigIn = left * DIV_16;

    // snapshot settings
    float depth, rate_hz, mix, level;
    do {
      uint32_t rev = settings.revision;
      while (rev & 1u) rev = settings.revision;
      depth = settings.depth;
      rate_hz = settings.rate_hz;
      mix = settings.mix;
      level = settings.level;
      if (rev == settings.revision) break;
    } while (1);

    tremLfo.SetRateHz(rate_hz);
    float lfo = tremLfo.Process(); // 0..1

    float wet = sigIn * (1.0f - depth * lfo);
    float out = (sigIn * (1.0f - mix) + wet * mix) * level;

    // update LED VU
    float vu = fabsf(out);
    PublishLedState(vu);

    int32_t outI = (int32_t)(out * MULT_16);

#ifdef MONITOR_CPU1
    digitalWrite(CPU_USE, 0);
#endif

    i2s.write(outI);
    i2s.write(outI);
  }
}
