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
// Combined ADSR + LFO modulation source for the 2HPico module.
//
// Short press on the front-panel button toggles between ADSR and LFO modes.
// In ADSR mode the button no longer acts as a manual gate.
//
// ADSR mode:
//   Pot 1 Attack
//   Pot 2 Decay
//   Pot 3 Sustain
//   Pot 4 Release
//   Trigger jack acts as gate input
//   LED uses Tiffany color
//
// LFO mode:
//   Pot 1 Rise
//   Pot 2 Fall
//   Pot 3 Waveform
//   Pot 4 Output level
//   Trigger jack acts as sync input
//   LED color follows the selected waveform

#include <2HPico.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include "pico/multicore.h"
#include "daisysp.h"
#include "control/adsr.cpp"
#include "lut.h"

#define DEBUG
#define MONITOR_CPU1

#define SAMPLERATE 44100
#define GATE_INPUT TRIGGER
#define SYNC_INPUT TRIGGER
#define SYNC_COMMAND 0

#define NUM_LFOS 1
#define NWAVES 6
#define RANGE 6
#define MIN_DELTA 300
#define MAX_AMPLITUDE AD_RANGE

enum Mode { MODE_ADSR, MODE_LFO };
enum WaveShape { RAMP, SINE, EXPO, QUARTIC, RANDOM1, PULSE };

struct lfodata {
  byte wave;
  int32_t rate1;
  int32_t rate2;
  uint16_t amplitude;
  int32_t acc;
  bool phase;
  int16_t dacout;
  int16_t scaledout;
} lfo[NUM_LFOS];

I2S DAC(OUTPUT);
Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
Adsr env;

float samplerate = SAMPLERATE;
uint32_t wavecolors[NWAVES] = {RED, ORANGE, GREEN, BLUE, AQUA, VIOLET};

volatile uint8_t currentMode = MODE_ADSR;
volatile bool gateActive = 0;
volatile int16_t adsrLedSample = 0;
volatile int16_t lfoLedSample = 0;

bool button = 0;
bool sync = 0;
uint32_t buttontimer, gatetimer, synctimer, parameterupdate;

static inline uint32_t scaleColorByLevel(uint32_t color, uint8_t level) {
  uint8_t r = (color >> 16) & 0x1f;
  uint8_t g = (color >> 8) & 0x1f;
  uint8_t b = color & 0x1f;
  r = (uint8_t)((r * level) / 31);
  g = (uint8_t)((g * level) / 31);
  b = (uint8_t)((b * level) / 31);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void resetLfoPhase() {
  if ((lfo[0].wave == SINE) || (lfo[0].wave == RAMP)) {
    lfo[0].acc = 0x1fffffff;
    lfo[0].phase = 1;
  } else {
    lfo[0].acc = 0;
    lfo[0].phase = 0;
  }
}

static void setDefaultParameters() {
  env.SetTime(ADSR_SEG_ATTACK, 0.01f);
  env.SetTime(ADSR_SEG_DECAY, 0.2f);
  env.SetSustainLevel(0.7f);
  env.SetTime(ADSR_SEG_RELEASE, 0.3f);

  lfo[0].wave = SINE;
  lfo[0].rate1 = 4000;
  lfo[0].rate2 = 4000;
  lfo[0].amplitude = MAX_AMPLITUDE;
  lfo[0].acc = 0;
  lfo[0].phase = 0;
  lfo[0].dacout = 0;
  lfo[0].scaledout = 0;
}

static void applyCurrentParameters() {
  if (currentMode == MODE_ADSR) {
    if (!potlock[0]) env.SetTime(ADSR_SEG_ATTACK, mapf(pot[0], 0, AD_RANGE - 1, 0, 2));
    if (!potlock[1]) env.SetTime(ADSR_SEG_DECAY, mapf(pot[1], 0, AD_RANGE - 1, 0, 2));
    if (!potlock[2]) env.SetSustainLevel(mapf(pot[2], 0, AD_RANGE - 1, 0, 1));
    if (!potlock[3]) env.SetTime(ADSR_SEG_RELEASE, mapf(pot[3], 0, AD_RANGE - 1, 0, 2));
    return;
  }

  if (!potlock[0]) lfo[0].rate1 = (long)(pow(10, mapf(pot[0], 0, AD_RANGE - 1, RANGE, 0))) + MIN_DELTA;
  if (!potlock[1]) lfo[0].rate2 = (long)(pow(10, mapf(pot[1], 0, AD_RANGE - 1, RANGE, 0))) + MIN_DELTA;
  if (!potlock[2]) lfo[0].wave = constrain(map(pot[2], 0, AD_RANGE, 0, NWAVES), 0, NWAVES - 1);
  if (!potlock[3]) lfo[0].amplitude = map(pot[3], 0, AD_RANGE, 0, MAX_AMPLITUDE);
}

static void updateMode() {
  if (!digitalRead(BUTTON1)) {
    if (((millis() - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      currentMode = (currentMode == MODE_ADSR) ? MODE_LFO : MODE_ADSR;
      gateActive = 0;
      sync = 0;
      gatetimer = millis();
      synctimer = millis();
      lockpots();
      if (currentMode == MODE_LFO) rp2040.fifo.push(SYNC_COMMAND);
    }
  } else {
    buttontimer = millis();
    button = 0;
  }
}

static void updateInputs() {
  if (currentMode == MODE_ADSR) {
    if (!digitalRead(GATE_INPUT)) {
      if (((millis() - gatetimer) > GATE_DEBOUNCE) && !gateActive) gateActive = 1;
    } else {
      gatetimer = millis();
      gateActive = 0;
    }
    sync = 0;
    synctimer = millis();
    return;
  }

  gateActive = 0;
  if (!digitalRead(SYNC_INPUT)) {
    if (((millis() - synctimer) > CLOCK_DEBOUNCE) && !sync) {
      sync = 1;
      rp2040.fifo.push(SYNC_COMMAND);
    }
  } else {
    synctimer = millis();
    sync = 0;
  }
}

static void updateLed() {
  uint32_t color = 0;

  if (currentMode == MODE_ADSR) {
    uint8_t level = (uint8_t)(((uint16_t)adsrLedSample >> 10) & 0x1f);
    if (level < 2) level = 2;
    color = scaleColorByLevel(TIFFANY, level);
  } else {
    int32_t level = -(lfoLedSample + 32768);
    level = (level >> 11) & 0x1f;
    color = scaleColorByLevel(wavecolors[lfo[0].wave], (uint8_t)level);
  }

  LEDS.setPixelColor(0, color);
  LEDS.show();
}

static void renderLfoSample() {
  bool phasechange = 0;
  unsigned a, x, y, delta, out;
  lfodata& osc = lfo[0];

  osc.acc += (osc.phase ? -osc.rate2 : osc.rate1);

  if (osc.acc >= 0x3fffffff) {
    osc.phase = 1;
    osc.acc = 0x3fffffff;
    phasechange = 1;
  }
  if (osc.acc <= 0) {
    osc.phase = 0;
    osc.acc = 0;
    phasechange = 1;
  }

  switch (osc.wave) {
    case RAMP:
      osc.dacout = (osc.acc >> 14) - 32768;
      break;
    case PULSE:
      osc.dacout = osc.phase ? 32767 : -32767;
      break;
    case RANDOM1:
      if (phasechange) osc.dacout = random(65535) - 32768;
      break;
    case EXPO:
      a = osc.acc >> 14;
      if (osc.phase) a = 65535 - a;
      x = (a >> 8) & 0xff;
      delta = (lut_env_expo[x + 1] - lut_env_expo[x]) >> 4;
      y = lut_env_expo[x];
      out = y + (a & 0xf) * delta;
      if (osc.phase) out = 65535 - out;
      osc.dacout = out - 32768;
      break;
    case QUARTIC:
      a = osc.acc >> 14;
      if (osc.phase) a = 65535 - a;
      x = (a >> 8) & 0xff;
      delta = (lut_env_quartic[x + 1] - lut_env_quartic[x]) >> 4;
      y = lut_env_quartic[x];
      out = y + (a & 0xf) * delta;
      if (osc.phase) out = 65535 - out;
      osc.dacout = out - 32768;
      break;
    case SINE:
    default:
      a = osc.acc >> 14;
      if (osc.phase) a = 65535 - a;
      x = (a >> 8) & 0xff;
      delta = (lut_raised_cosine[x + 1] - lut_raised_cosine[x]) >> 4;
      y = lut_raised_cosine[x];
      out = y + (a & 0xf) * delta;
      if (osc.phase) out = 65535 - out;
      osc.dacout = out - 32768;
      break;
  }

  osc.scaledout = ((int32_t)osc.dacout * (int32_t)osc.amplitude) / AD_RANGE;
  lfoLedSample = osc.dacout;

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0);
#endif
  DAC.write(osc.scaledout);
  DAC.write(osc.scaledout);
#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1);
#endif
}

static void renderAdsrSample() {
  float envelope = env.Process(gateActive);
  int16_t positive = (int16_t)(envelope * 32767.0f);
  int16_t inverted = -positive;

  adsrLedSample = positive;

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0);
#endif
  DAC.write(inverted);
  DAC.write(positive);
#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1);
#endif
}

void setup() {
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println("starting setup");
#endif

  pinMode(GATE_INPUT, INPUT_PULLUP);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  env.Init(samplerate);
  analogReadResolution(AD_BITS);
  setDefaultParameters();
  samplepots();
  applyCurrentParameters();
  buttontimer = millis();
  gatetimer = millis();
  synctimer = millis();
  parameterupdate = millis();

  LEDS.begin();
  LEDS.setPixelColor(0, TIFFANY);
  LEDS.show();

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
  updateMode();

  if ((millis() - parameterupdate) > PARAMETERUPDATE) {
    parameterupdate = millis();
    samplepots();
    applyCurrentParameters();
  }

  updateInputs();
  updateLed();
}

void setup1() {
  delay(1000);
}

void loop1() {
  while (rp2040.fifo.available()) {
    rp2040.fifo.pop();
    if (currentMode == MODE_LFO) resetLfoPhase();
  }

  if (currentMode == MODE_ADSR) {
    renderAdsrSample();
  } else {
    renderLfoSample();
  }
}
