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
// DaisySP drum models for 2HPico Eurorack module
// Models: AnalogBassDrum, SyntheticBassDrum, HiHat, SyntheticSnare
//
// UI
// Button: select model in order Analog BD (RED) -> Synth BD (ORANGE) -> HiHat (YELLOW) -> Synth Snare (GREEN)
// Pot 1: Tune          (pitch)
// Pot 2: Tone          (brightness)
// Pot 3: Accent        (base accent level)
// Pot 4: Decay         (tail length)
//
// Jacks
// Top    : Trigger in (normal hit)
// Middle : Trigger in (accent hit)
// Bottom : Audio out
//
// RH / WY Mar 2026

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include "pico/multicore.h"
#include <math.h>
extern "C" {
  #include "hardware/clocks.h"
}

#define DEBUG           // comment out to remove debug code
#define MONITOR_CPU1    // define to enable 2nd core monitoring on CPU_USE pin

#define SAMPLERATE 22050
//#define SAMPLERATE 44100

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);  // PT8211/PCM5102 etc.

#include "daisysp.h"
// pull in just the modules we need to keep binary size down
#include "filters/svf.cpp"
#include "synthesis/oscillator.cpp"
#include "drums/analogbassdrum.cpp"
#include "drums/synthbassdrum.cpp"
#include "drums/hihat.cpp"
#include "drums/synthsnaredrum.cpp"

using namespace daisysp;

float samplerate = SAMPLERATE;  // for DaisySP Init

// hardware mappings
#define TRIGGER_IN TRIGGER   // top jack
#define ACCENT_IN  AIN1      // middle jack used as a second trigger

// UI state
enum DrumModel { ANALOG_BD = 0, SYNTH_BD, HIHAT, SYNTH_SNARE, NUM_MODELS };
volatile uint8_t currentModel = ANALOG_BD;

struct ModelParams {
  float tune;    // 0..1 normalized
  float tone;    // 0..1 normalized
  float accent;  // 0..1 normalized
  float decay;   // 0..1 normalized
};

// per‑model defaults (normalized)
ModelParams params[NUM_MODELS] = {
  {0.40f, 0.40f, 0.80f, 0.55f}, // Analog BD (raise accent a bit)
  {0.38f, 0.55f, 0.50f, 0.60f}, // Synth BD
  {0.70f, 0.55f, 0.50f, 0.40f}, // HiHat
  {0.45f, 0.65f, 0.55f, 0.50f}  // Synth Snare
};

// Per-model output gain to level-match (1.0 = unity)
float modelGain[NUM_MODELS] = {
  1.8f,  // Analog BD louder to match others
  1.0f,  // Synth BD
  1.0f,  // HiHat
  1.0f   // Synth Snare
};

// DaisySP voice objects
AnalogBassDrum    analogBD;
SyntheticBassDrum synthBD;
HiHat<>           hihat;
SyntheticSnareDrum synthSnare;

// inter-core flags
volatile bool trigPending   = false;
volatile bool accentPending = false;

// debounce & timing
uint32_t buttontimer = 0;
uint32_t trigtimer   = 0;
uint32_t accenttimer = 0;
uint32_t parameterupdate = 0;

bool buttonState = false;
bool trigState   = false;
bool accentState = false;

// helpers
static inline float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static inline float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

static inline float potNorm(uint16_t reading) {
  return (float)reading / (float)(AD_RANGE - 1);
}

static float tuneToHz(uint8_t model, float norm) {
  switch (model) {
    case ANALOG_BD:   return lerp(25.0f, 120.0f, norm);
    case SYNTH_BD:    return lerp(25.0f, 140.0f, norm);
    case HIHAT:       return lerp(1500.0f, 12000.0f, norm); // reduce top range to keep aliasing lower
    case SYNTH_SNARE: return lerp(80.0f, 1200.0f, norm);
    default:          return 100.0f;
  }
}

static float decayForModel(uint8_t model, float norm) {
  // small floor to avoid going to absolute zero
  switch (model) {
    case HIHAT:       return lerp(0.05f, 1.0f, norm);
    case SYNTH_SNARE: return lerp(0.05f, 1.2f, norm);
    default:          return clamp01(norm);
  }
}

static void setLedForModel(uint8_t model) {
  switch (model) {
    case ANALOG_BD:   LEDS.setPixelColor(0, RED);    break;
    case SYNTH_BD:    LEDS.setPixelColor(0, ORANGE); break;
    case HIHAT:       LEDS.setPixelColor(0, YELLOW); break;
    case SYNTH_SNARE: LEDS.setPixelColor(0, GREEN);  break;
    default:          LEDS.setPixelColor(0, GREY);   break;
  }
  LEDS.show();
}

static void applyParamsToModel(uint8_t model) {
  ModelParams p = params[model];
  float tuneHz  = tuneToHz(model, clamp01(p.tune));
  float tone    = clamp01(p.tone);
  float accent  = clamp01(p.accent);
  float decay   = decayForModel(model, clamp01(p.decay));

  switch (model) {
    case ANALOG_BD:
      analogBD.SetFreq(tuneHz);
      analogBD.SetTone(tone);
      analogBD.SetAccent(accent);
      analogBD.SetDecay(decay);
      break;
    case SYNTH_BD:
      synthBD.SetFreq(tuneHz);
      synthBD.SetTone(tone);
      synthBD.SetAccent(accent);
      synthBD.SetDecay(decay);
      // leave dirtiness/FM at library defaults
      break;
    case HIHAT:
      hihat.SetFreq(tuneHz);
      hihat.SetTone(tone);
      hihat.SetAccent(accent);
      hihat.SetDecay(decay);
      hihat.SetNoisiness(0.25f + 0.65f * (1.0f - tone)); // darker tone = more noise
      break;
    case SYNTH_SNARE:
      synthSnare.SetFreq(tuneHz);
      synthSnare.SetSnappy(tone);
      synthSnare.SetAccent(accent);
      synthSnare.SetDecay(decay);
      synthSnare.SetFmAmount(0.05f + 0.9f * tone);
      break;
    default:
      break;
  }
}

static void triggerCurrentModel(bool accentHit) {
  uint8_t model = currentModel;
  float baseAccent = clamp01(params[model].accent);
  float hitAccent  = accentHit ? 1.0f : baseAccent;

  // temporarily push accent up for accented hits
  switch (model) {
    case ANALOG_BD:   analogBD.SetAccent(hitAccent);   analogBD.Trig();   if (accentHit) analogBD.SetAccent(baseAccent);   break;
    case SYNTH_BD:    synthBD.SetAccent(hitAccent);    synthBD.Trig();    if (accentHit) synthBD.SetAccent(baseAccent);    break;
    case HIHAT:       hihat.SetAccent(hitAccent);      hihat.Trig();      if (accentHit) hihat.SetAccent(baseAccent);      break;
    case SYNTH_SNARE: synthSnare.SetAccent(hitAccent); synthSnare.Trig(); if (accentHit) synthSnare.SetAccent(baseAccent); break;
    default:          break;
  }
}

void setup() {
  Serial.begin(115200);
#ifdef DEBUG
  Serial.println("Drums setup");
#endif

  // bump system clock before bringing up peripherals

  pinMode(TRIGGER_IN, INPUT_PULLUP); // trigger inputs are inverted on this hardware
  pinMode(ACCENT_IN, INPUT_PULLUP);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);  // analog mux for the four pots

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT); // hi = core1 busy
#endif

  analogBD.Init(samplerate);
  synthBD.Init(samplerate);
  hihat.Init(samplerate);
  synthSnare.Init(samplerate);

  analogReadResolution(AD_BITS); // max ADC resolution

  LEDS.begin();
  setLedForModel(currentModel);

  applyParamsToModel(currentModel);

  // set up Pico I2S for PT8211 stereo DAC
  DAC.setBCLK(BCLK);
  DAC.setDATA(I2S_DATA);
  DAC.setBitsPerSample(16);
  DAC.setBuffers(1, 128, 0); // DMA buffer - 32 bit L/R words
  DAC.setLSBJFormat();       // needed for PT8211 which has funny timing
  DAC.begin(SAMPLERATE);

#ifdef DEBUG
  Serial.println("Drums ready");
#endif
}

void loop() {
  // model select button
  if (!digitalRead(BUTTON1)) {
    if (((millis() - buttontimer) > DEBOUNCE) && !buttonState) {
      buttonState = true;
      currentModel = (currentModel + 1) % NUM_MODELS;
      setLedForModel(currentModel);
      lockpots();
      applyParamsToModel(currentModel); // push stored defaults for new model
    }
  } else {
    buttontimer = millis();
    buttonState = false;
  }

  // read pots and update parameters periodically
  if ((millis() - parameterupdate) > PARAMETERUPDATE) {
    parameterupdate = millis();
    samplepots();
    ModelParams &p = params[currentModel];
    if (!potlock[0]) p.tune   = potNorm(pot[0]);
    if (!potlock[1]) p.tone   = potNorm(pot[1]);
    if (!potlock[2]) p.accent = potNorm(pot[2]);
    if (!potlock[3]) p.decay  = potNorm(pot[3]);
    applyParamsToModel(currentModel);
  }

  // normal trigger (top jack)
  if (!digitalRead(TRIGGER_IN)) {
    if (((millis() - trigtimer) > TRIG_DEBOUNCE) && !trigState) {
      trigState = true;
      trigPending = true;
    }
  } else {
    trigtimer = millis();
    trigState = false;
  }

  // accent trigger (middle jack)
  if (!digitalRead(ACCENT_IN)) {
    if (((millis() - accenttimer) > TRIG_DEBOUNCE) && !accentState) {
      accentState = true;
      accentPending = true;
    }
  } else {
    accenttimer = millis();
    accentState = false;
  }
}

// second core setup
void setup1() {
  delay(1000); // wait for core0 to bring up peripherals
}

// process audio samples on core1
void loop1() {
  static float sig = 0.0f;
  static int32_t outsample = 0;

  bool fireAccent = false;
  bool fireNormal = false;

  // service trigger flags
  if (accentPending) {
    fireAccent = true;
    accentPending = false;
  } else if (trigPending) {
    fireNormal = true;
    trigPending = false;
  }

  if (fireAccent || fireNormal) {
    triggerCurrentModel(fireAccent);
  }

  switch (currentModel) {
    case ANALOG_BD:   sig = analogBD.Process();   break;
    case SYNTH_BD:    sig = synthBD.Process();    break;
    case HIHAT:       sig = hihat.Process();      break;
    case SYNTH_SNARE: sig = synthSnare.Process(); break;
    default:          sig = 0.0f;                 break;
  }

  // apply per-model gain and soft clip
  sig *= modelGain[currentModel];
  if (sig > 1.0f) sig = 1.0f;
  if (sig < -1.0f) sig = -1.0f;

  // properly convert float -1.0..1.0 to int16 sample
  outsample = (int32_t)(sig * 32767.0f);

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0); // low = waiting for DMA slot
#endif

  DAC.write((int16_t)outsample); // left
  DAC.write((int16_t)outsample); // right

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // hi = core1 busy
#endif
}
