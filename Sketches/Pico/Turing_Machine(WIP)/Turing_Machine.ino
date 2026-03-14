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
// Turing Machine style random step sequencer for 2HPico hardware
// 16 steps max, CV + random trigger output
//
// Top jack - clock input (CVIN)
// Middle jack - CV out (OUT1)
// Bottom jack - trigger out (OUT2)
//
// Pot 1 - sequence stability (random/lock/slip)
// Pot 2 - output range (CV variation amount)
// Pot 3 - sequence length (1-16 steps)
// Pot 4 - CV smoothing (slew)
//
// LED Red: clock flash
// LED Orange: step 1
// No clock for 1s: next clock restarts from step 1

#include <2HPico.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring on CPU_USE pin

//#define SAMPLERATE 11025
#define SAMPLERATE 22050  // CV output doesn't need full audio bandwidth
//#define SAMPLERATE 44100

#define MAX_STEPS 16
#define CLOCKIN TRIGGER

#define CVOUT_VOLT 6554 // D/A count per volt - nominally +-5v range for -+32767 DAC values
#define CVOUT_MAX (5 * CVOUT_VOLT)
#define CVOUT_LIMIT 32767

#define LEDOFF 80  // LED flash time in ms
#define CLOCK_RESET_MS 1000  // reset to step 1 after 1s without clock
#define TRIG_MS 12  // trigger pulse width
#define SMOOTH_MIN 8  // minimum smoothing coefficient

#define POT_RANDOM_LOW (AD_RANGE * 45 / 100)
#define POT_RANDOM_HIGH (AD_RANGE * 55 / 100)
#define POT_LOCK_LOW (AD_RANGE * 70 / 100)
#define POT_LONG_LOW (AD_RANGE * 90 / 100)

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);  // stereo DAC

volatile int16_t target_cv = 0;
volatile int16_t gateout = GATELOW;
volatile uint16_t smoothcoef = 65535;

int16_t seq_cv[MAX_STEPS];
uint8_t seq_gate[MAX_STEPS];

int8_t stepindex = 0;
int8_t laststep = MAX_STEPS - 1;
int8_t base_steps = MAX_STEPS;

int32_t range_cv = CVOUT_LIMIT;
bool clocked = 0;
bool clockidle = 1;
bool ledflash = 0;
uint32_t ledtimer = 0;
uint32_t ledcolor = 0;
uint32_t clocktimer = 0;
uint32_t clockdebouncetimer = 0;
uint32_t gatetimer = 0;

int16_t clampCv(int32_t value) {
  if (value > CVOUT_LIMIT) return CVOUT_LIMIT;
  if (value < -CVOUT_LIMIT) return -CVOUT_LIMIT;
  return (int16_t)value;
}

void setLed(uint32_t color) {
  if (color == ledcolor) return;
  LEDS.setPixelColor(0, color);
  LEDS.show();
  ledcolor = color;
}

void flashLed(uint32_t color) {
  setLed(color);
  ledtimer = millis();
  ledflash = 1;
}

void randomizeStep(int8_t index) {
  seq_cv[index] = (int16_t)random(-CVOUT_LIMIT, CVOUT_LIMIT + 1);
  seq_gate[index] = (random(0, 100) < 50) ? 1 : 0;
}

void setup() { 
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println("starting setup");  
#endif

  pinMode(CLOCKIN, INPUT_PULLUP); // clock input
  pinMode(MUXCTL, OUTPUT);  // analog switch mux

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT); // hi = CPU busy
#endif

  analogReadResolution(AD_BITS); // set up for max resolution

  LEDS.begin();
  LEDS.setPixelColor(0, RED);
  LEDS.show();

  randomSeed(analogRead(CV1IN) ^ analogRead(CV2IN) ^ micros());
  for (int i = 0; i < MAX_STEPS; ++i) {
    randomizeStep(i);
  }

// set up Pico I2S for PT8211 stereo DAC
	DAC.setBCLK(BCLK);
	DAC.setDATA(I2S_DATA);
	DAC.setBitsPerSample(16);
	DAC.setBuffers(1, 128, 0); // DMA buffer - 32 bit L/R words
	DAC.setLSBJFormat();  // needed for PT8211 which has funny timing
	DAC.begin(SAMPLERATE);

#ifdef DEBUG  
  Serial.println("finished setup");  
#endif
}

void loop() {
  samplepots();

  if (!potlock[1]) range_cv = map(pot[1], 0, AD_RANGE - 1, 0, CVOUT_LIMIT);
  if (!potlock[2]) {
    int8_t steps = map(pot[2], 0, AD_RANGE - 1, 1, MAX_STEPS + 1);
    if (steps > MAX_STEPS) steps = MAX_STEPS;
    base_steps = steps;
  }
  if (!potlock[3]) {
    uint16_t smooth = map(pot[3], 0, AD_RANGE - 1, 65535, SMOOTH_MIN);
    smoothcoef = smooth;
  }

  int8_t effective_steps = base_steps;
  if (pot[0] >= POT_LONG_LOW && base_steps < MAX_STEPS) {
    effective_steps = base_steps * 2;
    if (effective_steps > MAX_STEPS) effective_steps = MAX_STEPS;
  }
  laststep = effective_steps - 1;
  if (stepindex > laststep) stepindex = 0;

  if (!digitalRead(CLOCKIN)) {  // look for rising edge of clock input which is inverted
    if (((millis() - clockdebouncetimer) > CLOCK_DEBOUNCE) && !clocked) {
      clocked = 1;
      bool was_idle = clockidle;
      clocktimer = millis();
      clockidle = 0;
      if (was_idle) stepindex = 0;

      bool random_mode = (pot[0] >= POT_RANDOM_LOW && pot[0] <= POT_RANDOM_HIGH);
      bool lock_mode = (pot[0] >= POT_LOCK_LOW);
      bool update_step = false;

      if (random_mode) update_step = true;
      else if (lock_mode) update_step = false;
      else {
        uint8_t change_prob = map(pot[0], 0, POT_LOCK_LOW - 1, 20, 5);
        if (random(0, 100) < change_prob) update_step = true;
      }

      if (update_step) randomizeStep(stepindex);

      int32_t cv = (int32_t)seq_cv[stepindex] * range_cv;
      cv /= CVOUT_LIMIT;
      target_cv = clampCv(-cv);  // hardware DAC output is inverted

      if (seq_gate[stepindex]) {
        gateout = GATEHIGH;
        gatetimer = millis();
      }
      else gateout = GATELOW;

      uint32_t stepcolor = (stepindex == 0) ? YELLOW : RED;
      flashLed(stepcolor);

      ++stepindex;
      if (stepindex > laststep) stepindex = 0;
    }
  }
  else {
    clocked = 0;
    clockdebouncetimer = millis();
  }

  if (!clockidle && (millis() - clocktimer) > CLOCK_RESET_MS) {
    stepindex = 0;
    clockidle = 1;
  }

  if (gateout == GATEHIGH && (millis() - gatetimer) > TRIG_MS) gateout = GATELOW;

  if (ledflash && ((millis() - ledtimer) > LEDOFF)) {
    setLed(0);
    ledflash = 0;
  }
}

// second core setup
// second core is dedicated to sample processing
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

// process CV samples
void loop1() {
  static int32_t current_cv = 0;

  int32_t tcv = target_cv;
  uint16_t smooth = smoothcoef;

  if (smooth >= 65535) current_cv = tcv;
  else current_cv += ((tcv - current_cv) * smooth) >> 16;

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE, 0); // low = core1 stalled because I2S buffer is full
#endif
 // write samples to DMA buffer - this is a blocking call so it stalls when buffer is full
	DAC.write(int16_t(current_cv)); // left
	DAC.write(int16_t(gateout)); // right

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // hi = core1 busy
#endif
}
