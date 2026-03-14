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
// Bernoulli gates for 2HPico hardware, 
// inspired by Mutable Instruments Branches;
//
//  Short press button to set primary mode
//    Gate mode: Pot1 sets Left/Right probability (center 50/50; turn left favors RED Out, turn right favors Green Out).
//    Toggle mode: Pot1 sets probability of flipping the current output (far left = never flip, far right = flip every trigger).
//  Long press buttonto set output type
//    Trigger: ~10 ms pulse
//    Latch  : hold high until the next flip
//
//   Pot1 Probability
//   Pot2 Output Level 0‑5 V
//   Pot3 unused
//   Pot4 unused
//
// Jacks:
//   Top    : Trigger In
//   AUX : Red Out
//   OUT : Green Out

#include <2HPico.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include "pico/multicore.h"

#define DEBUG           // comment out to remove debug code
#define MONITOR_CPU1    // define to enable 2nd core monitoring on CPU_USE pin

#define SAMPLERATE 44100

#define TRIGGER_IN TRIGGER

#define TRIG_PULSE_MS 10      // trigger pulse length
#define LONG_PRESS_MS 1200    // >1.2s = secondary mode toggle
#define PARAM_UPDATE_MS 10

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);  // PT8211/PCM5102 etc.

enum PrimaryMode { GATE_MODE = 0, TOGGLE_MODE, NUM_PRIMARY };
enum OutputMode  { TRIGGER_MODE = 0, LATCH_MODE, NUM_OUTPUT };

volatile int16_t leftOut  = GATELOW;   // left DAC channel -> bottom jack
volatile int16_t rightOut = GATELOW;   // right DAC channel -> middle jack

PrimaryMode primaryMode = GATE_MODE;
OutputMode outputMode   = TRIGGER_MODE;

// state
bool buttonState = false;
bool longPressHandled = false;
bool trigState = false;
bool currentLeft = true;   // remembered side for toggle mode

uint32_t buttontimer = 0;
uint32_t buttonpress = 0;
uint32_t trigdebounce = 0;
uint32_t parameterupdate = 0;

// pulse bookkeeping (trigger mode)
bool leftPulse = false;
bool rightPulse = false;
uint32_t leftPulseOff = 0;
uint32_t rightPulseOff = 0;

int16_t gateHighLevel = GATEHIGH;  // scaled by Pot2
float prob = 0.5f;

static inline float potNorm(uint16_t v) {
  return (float)v / (float)(AD_RANGE - 1);
}

void setLed(uint32_t color) {
  static uint32_t last = 0;
  if (color == last) return;
  LEDS.setPixelColor(0, color);
  LEDS.show();
  last = color;
}

void updateParameters() {
  samplepots();
  prob = potNorm(pot[0]);

  // scale level 0..5V -> 0..32767 counts (DAC is inverted in hardware)
  int32_t counts = map(pot[1], 0, AD_RANGE - 1, 0, 32767);
  gateHighLevel = (int16_t)(-counts);
}

void makeOutputsLow() {
  leftOut = GATELOW;
  rightOut = GATELOW;
  leftPulse = rightPulse = false;
}

void schedulePulse(bool toLeft, uint32_t nowMs) {
  if (toLeft) {
    leftOut = gateHighLevel;
    rightOut = GATELOW;
    leftPulse = true;
    rightPulse = false;
    leftPulseOff = nowMs + TRIG_PULSE_MS;
  }
  else {
    rightOut = gateHighLevel;
    leftOut = GATELOW;
    rightPulse = true;
    leftPulse = false;
    rightPulseOff = nowMs + TRIG_PULSE_MS;
  }
}

void latchOutput(bool toLeft) {
  leftOut  = toLeft ? gateHighLevel : GATELOW;
  rightOut = toLeft ? GATELOW : gateHighLevel;
  leftPulse = rightPulse = false;
}

void handleTrigger(uint32_t nowMs) {
  float r = (float)random(0, 10000) / 9999.0f; // 0..1
  bool toLeft = true;

  if (primaryMode == GATE_MODE) {
    toLeft = (r < prob);  // prob -> left, (1-prob) -> right
  }
  else { // TOGGLE_MODE
    if (r < prob) currentLeft = !currentLeft;
    toLeft = currentLeft;
  }

  if (outputMode == TRIGGER_MODE) schedulePulse(toLeft, nowMs);
  else latchOutput(toLeft);  // LATCH_MODE
}

void updateButton(uint32_t nowMs) {
  if (!digitalRead(BUTTON1)) {  // pressed (active low)
    if (((nowMs - buttontimer) > DEBOUNCE) && !buttonState) {
      buttonState = true;
      buttonpress = nowMs;
      longPressHandled = false;
    }
    if (buttonState && !longPressHandled && (nowMs - buttonpress) >= LONG_PRESS_MS) {
      longPressHandled = true;
      outputMode = (OutputMode)((outputMode + 1) % NUM_OUTPUT);
      lockpots();
    }
  }
  else {
    if (buttonState && !longPressHandled) {  // short press
      primaryMode = (PrimaryMode)((primaryMode + 1) % NUM_PRIMARY);
      lockpots();
    }
    buttontimer = nowMs;
    buttonState = false;
    longPressHandled = false;
  }
}

void updateLed() {
  // Show current output: Left = GREEN, Right = RED, idle = LED off.
  if (leftOut == gateHighLevel && rightOut == GATELOW) {
    setLed(GREEN);
  }
  else if (rightOut == gateHighLevel && leftOut == GATELOW) {
    setLed(RED);
  }
  else {
    setLed(0);
  }
}

void setup() {
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println("Branches setup");
#endif

  pinMode(TRIGGER_IN, INPUT_PULLUP);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  analogReadResolution(AD_BITS);
  LEDS.begin();
  setLed(0);

  randomSeed(analogRead(AIN2) + micros());

  // init pots
  lockpots();
  updateParameters();

  // set up Pico I2S for PT8211 stereo DAC
  DAC.setBCLK(BCLK);
  DAC.setDATA(I2S_DATA);
  DAC.setBitsPerSample(16);
  DAC.setBuffers(2, 64, 0); // DMA buffer - 32 bit L/R words
  DAC.setLSBJFormat();       // PT8211 timing quirk
  DAC.begin(SAMPLERATE);

#ifdef DEBUG
  Serial.println("Branches ready");
#endif
}

void loop() {
  uint32_t nowMs = millis();

  if ((nowMs - parameterupdate) > PARAM_UPDATE_MS) {
    parameterupdate = nowMs;
    updateParameters();
  }

  updateButton(nowMs);

  if (!digitalRead(TRIGGER_IN)) {
    if (((nowMs - trigdebounce) > TRIG_DEBOUNCE) && !trigState) {
      trigState = true;
      handleTrigger(nowMs);
    }
  }
  else {
    trigState = false;
    trigdebounce = nowMs;
  }

  if (outputMode == TRIGGER_MODE) {
    if (leftPulse && (int32_t)(nowMs - leftPulseOff) >= 0) {
      leftOut = GATELOW;
      leftPulse = false;
    }
    if (rightPulse && (int32_t)(nowMs - rightPulseOff) >= 0) {
      rightOut = GATELOW;
      rightPulse = false;
    }
  }

  updateLed();
}

// second core: dedicated DAC writing
void setup1() {
  delay(1000); // wait for main core to start up peripherals
}

void loop1() {
#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0); // low = stalled by DAC buffer
#endif
  // write samples to DMA buffer - blocking when buffer is full
  DAC.write(int16_t(leftOut));   // left channel  -> bottom jack (Left Out)
  DAC.write(int16_t(rightOut));  // right channel -> middle jack (Right Out)

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // high = core busy
#endif
}
