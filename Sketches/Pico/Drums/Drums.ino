// Copyright 2025 Rich Heslip
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
// Drum voice app for 2HPico Eurorack module
// custom drum models for RP2350
// V1.0 WY Mar 2026

// top jack - trigger input
// middle jack - tune modulation input
// bottom jack - audio out

// button - cycle drum model

// 808kick - Red LED
// hihats - Yellow LED
// snare - Green LED

// Pot 1 - Tune
// Pot 2 - Tone
// Pot 3 - Output level
// Pot 4 - Decay

// performance notes:
// - .ino handles UI, trigger detection, LED and I2S
// - DrumModels.h contains all drum synthesis code
// - only the active model is processed on core 1
// - 44.1kHz keeps the drum transients crisp while staying within RP2350 budget

#include <2HPico.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "pico/multicore.h"

#include "DrumModels.h"

//#define DEBUG
#define MONITOR_CPU1

#define SAMPLERATE 44100
#define TUNE_CV_THRESHOLD 120

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);

float samplerate = SAMPLERATE;

DrumVoiceBank drums;
DrumParameters modelParams[NUM_DRUM_MODELS] = {
  kDefaultDrumParameters[MODEL_808KICK],
  kDefaultDrumParameters[MODEL_HIHATS],
  kDefaultDrumParameters[MODEL_SNARE],
};

volatile uint8_t currentModel = MODEL_808KICK;
volatile uint8_t triggerPending = 0;
volatile uint32_t controlRevision = 1;
volatile float tuneCvAmount = 0.0f;

bool button = 0;
bool trigger = 0;
uint32_t buttontimer = 0;
uint32_t trigtimer = 0;
uint32_t parameterupdate = 0;

static uint32_t modelColor(uint8_t model) {
  switch (model) {
    case MODEL_808KICK: return RED;
    case MODEL_HIHATS: return YELLOW;
    default: return GREEN;
  }
}

static void setLedForModel(uint8_t model) {
  LEDS.setPixelColor(0, modelColor(model));
  LEDS.show();
}

static float readTuneCvAmount() {
  float cv = (float)(AD_RANGE - sampleCV2());
  if (cv < TUNE_CV_THRESHOLD) return 0.0f;
  float normalized = (cv - TUNE_CV_THRESHOLD) / (float)((AD_RANGE - 1) - TUNE_CV_THRESHOLD);
  if (normalized < 0.0f) normalized = 0.0f;
  if (normalized > 1.0f) normalized = 1.0f;
  return normalized;
}

static void advanceModel() {
  uint8_t nextModel = currentModel + 1;
  if (nextModel >= NUM_DRUM_MODELS) nextModel = MODEL_808KICK;
  currentModel = nextModel;
  lockpots();
  ++controlRevision;
  setLedForModel(currentModel);
}

static void updateCurrentModelFromPots() {
  samplepots();

  uint8_t model = currentModel;
  DrumParameters previous = modelParams[model];
  DrumParameters updated = previous;

  if (!potlock[0]) updated.tune = mapf(pot[0], 0, AD_RANGE - 1, 0.0f, 1.0f);
  if (!potlock[1]) updated.tone = mapf(pot[1], 0, AD_RANGE - 1, 0.0f, 1.0f);
  if (!potlock[2]) updated.level = mapf(pot[2], 0, AD_RANGE - 1, 0.0f, 1.0f);
  if (!potlock[3]) updated.decay = mapf(pot[3], 0, AD_RANGE - 1, 0.0f, 1.0f);

  float previousTuneCv = tuneCvAmount;
  float updatedTuneCv = readTuneCvAmount();

  if ((fabsf(updated.tune - previous.tune) > 0.001f)
      || (fabsf(updated.tone - previous.tone) > 0.001f)
      || (fabsf(updated.level - previous.level) > 0.001f)
      || (fabsf(updated.decay - previous.decay) > 0.001f)
      || (fabsf(updatedTuneCv - previousTuneCv) > 0.01f)) {
    modelParams[model] = updated;
    tuneCvAmount = updatedTuneCv;
    ++controlRevision;
  }
}

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  Serial.println("starting setup");
#endif

  pinMode(TRIGGER, INPUT_PULLUP);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  LEDS.begin();
  setLedForModel(currentModel);

  analogReadResolution(AD_BITS);
  samplepots();

  drums.Init(samplerate);
  for (uint8_t i = 0; i < NUM_DRUM_MODELS; ++i) {
    drums.SetParameters(i, modelParams[i]);
  }
  drums.SetModel(currentModel);

  DAC.setBCLK(BCLK);
  DAC.setDATA(I2S_DATA);
  DAC.setBitsPerSample(16);
  DAC.setBuffers(1, 128, 0);
  DAC.setLSBJFormat();
  DAC.begin(SAMPLERATE);

#ifdef DEBUG
  Serial.println("finished setup");
#endif
}

void loop() {
  if (!digitalRead(BUTTON1)) {
    if (((millis() - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      advanceModel();
    }
  } else {
    buttontimer = millis();
    button = 0;
  }

  if ((millis() - parameterupdate) > PARAMETERUPDATE) {
    parameterupdate = millis();
    updateCurrentModelFromPots();
  }

  if (!digitalRead(TRIGGER)) {
    if (((millis() - trigtimer) > TRIG_DEBOUNCE) && !trigger) {
      trigger = 1;
      triggerPending = 1;
    }
  } else {
    trigtimer = millis();
    trigger = 0;
  }
}

void setup1() {
  delay(1000);
}

void loop1() {
  static uint32_t lastControlRevision = 0;
  static float sig = 0.0f;
  static int32_t outsample = 0;

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1);
#endif

  uint8_t model = currentModel;
  uint32_t revision = controlRevision;
  if (revision != lastControlRevision) {
    DrumParameters effective = modelParams[model];
    effective.tune = DrumClamp01(effective.tune + tuneCvAmount);
    drums.SetParameters(model, effective);
    drums.SetModel(model);
    lastControlRevision = revision;
  }

  uint8_t pendingTrigger = triggerPending;
  if (pendingTrigger) {
    triggerPending = 0;
    drums.Trigger();
  }

  sig = drums.Process() * modelParams[model].level;
  if (sig > 1.0f) sig = 1.0f;
  if (sig < -1.0f) sig = -1.0f;
  outsample = (int32_t)(sig * MULT_16) >> 16;

  DAC.write(int16_t(outsample));
  DAC.write(int16_t(outsample));

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0);
#endif
}
