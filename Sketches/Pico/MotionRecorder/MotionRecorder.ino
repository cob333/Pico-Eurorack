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
// CV motion recorder/player for 2HPico hardware
// 2-channel CV motion recording with clocked steps, up to 16 steps
// outputs are bipolar (+-5V) via DAC
//
// Top jack - clock input
// Middle jack - CV out 1
// Bottom jack - CV out 2
//
// Pot 1 - manual CV motion for channel 2
// Pot 2 - manual CV motion for channel 1
// Pot 3 - sequence length (1-16 steps)
// Pot 4 - motion smoothing
//
// Button - press to start a new recording (overwrites steps as they are recorded)
//
// LED Red: ready/recording
// LED Green solid: recording complete, waiting for next clock
// LED Green blink: playback

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
#define SMOOTH_MIN 8  // minimum smoothing coefficient

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);  // stereo DAC

enum RunState {IDLE, RECORDING, READY, PLAYBACK};
RunState state = READY;

volatile int16_t target1 = 0;
volatile int16_t target2 = 0;
volatile uint16_t smoothcoef = 65535;

int16_t motion1[MAX_STEPS];
int16_t motion2[MAX_STEPS];

int16_t manual1 = 0;
int16_t manual2 = 0;

int8_t record_step = 0;
int8_t play_step = 0;
int8_t laststep = MAX_STEPS - 1;

bool button = 0;
bool clocked = 0;
bool ledflash = 0;
bool record_pending = 0;
uint32_t buttontimer = 0;
uint32_t clockdebouncetimer = 0;
uint32_t clocktimer = 0;
bool clockidle = 1;
uint32_t ledtimer = 0;
uint32_t ledcolor = RED;
uint32_t led_restore = RED;

uint16_t last_record_pot1 = 0;
uint16_t last_record_pot2 = 0;
bool record_pots_inited = 0;

int16_t clampCv(int32_t value) {
  if (value > CVOUT_LIMIT) return CVOUT_LIMIT;
  if (value < -CVOUT_LIMIT) return -CVOUT_LIMIT;
  return (int16_t)value;
}

int16_t potToCv(uint16_t potval) {
  int32_t cv = map(potval, 0, AD_RANGE - 1, -CVOUT_MAX, CVOUT_MAX);
  return clampCv(-cv);  // hardware DAC output is inverted
}

void setLed(uint32_t color) {
  if (color == ledcolor) return;
  LEDS.setPixelColor(0, color);
  LEDS.show();
  ledcolor = color;
}

void flashLed(uint32_t color, uint32_t restore) {
  setLed(color);
  ledtimer = millis();
  ledflash = 1;
  led_restore = restore;
}

void startRecording(void) {
  state = RECORDING;
  record_pending = 0;
  record_step = 0;
  play_step = 0;
  ledflash = 0;
  record_pots_inited = 0;
  setLed(RED);
}

void setup() { 
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println("starting setup");  
#endif

  pinMode(CLOCKIN, INPUT_PULLUP); // clock input
  pinMode(BUTTON1, INPUT_PULLUP); // button in
  pinMode(MUXCTL, OUTPUT);  // analog switch mux

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT); // hi = CPU busy
#endif

  analogReadResolution(AD_BITS); // set up for max resolution

  LEDS.begin();
  LEDS.setPixelColor(0, GREEN);
  LEDS.show();

  for (int i = 0; i < MAX_STEPS; ++i) {
    motion1[i] = 0;
    motion2[i] = 0;
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

  if (!digitalRead(BUTTON1)) {
    if (((millis() - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      if (state == PLAYBACK) record_pending = 1;
      else startRecording();
    }
  }
  else {
    buttontimer = millis();
    button = 0;
  }

  samplepots();

  if (state == RECORDING && !record_pots_inited) {
    last_record_pot1 = pot[0];
    last_record_pot2 = pot[1];
    record_pots_inited = 1;
  }

  if (!potlock[0]) manual2 = potToCv(pot[0]);
  if (!potlock[1]) manual1 = potToCv(pot[1]);
  if (!potlock[2]) {
    int8_t steps = map(pot[2], 0, AD_RANGE - 1, 1, MAX_STEPS + 1);
    if (steps > MAX_STEPS) steps = MAX_STEPS;
    laststep = steps - 1;
    if (play_step > laststep) play_step = 0;
  }
  if (!potlock[3]) {
    uint16_t smooth = map(pot[3], 0, AD_RANGE - 1, 65535, SMOOTH_MIN);
    smoothcoef = smooth;
  }

  if (!digitalRead(CLOCKIN)) {  // look for rising edge of clock input which is inverted
    if (((millis() - clockdebouncetimer) > CLOCK_DEBOUNCE) && !clocked) {
      clocked = 1;
      clocktimer = millis();
      clockidle = 0;

      if (state == IDLE && record_pending) {
        startRecording();
        record_pending = 0;
      }
      else if (state == IDLE) {
        flashLed(BLUE, 0);
      }

      if (state == RECORDING) {
        if (!record_pots_inited) {
          last_record_pot1 = pot[0];
          last_record_pot2 = pot[1];
          record_pots_inited = 1;
        }

        int16_t delta1 = (int16_t)pot[1] - (int16_t)last_record_pot2;
        int16_t delta2 = (int16_t)pot[0] - (int16_t)last_record_pot1;
        bool moved1 = abs(delta1) > MIN_POT_CHANGE;
        bool moved2 = abs(delta2) > MIN_POT_CHANGE;

        target1 = moved1 ? manual1 : motion1[record_step];
        target2 = moved2 ? manual2 : motion2[record_step];

        if (moved1) {
          motion1[record_step] = manual1;
          last_record_pot2 = pot[1];
        }
        if (moved2) {
          motion2[record_step] = manual2;
          last_record_pot1 = pot[0];
        }

        uint32_t stepcolor = (record_step == 0) ? YELLOW : RED;
        flashLed(stepcolor, 0);
        ++record_step;
        if (record_step > laststep) {
          state = READY;
          ledflash = 0;
          setLed(GREEN);
        }
      }
      else if (state == READY) {
        play_step = 0;
        target1 = motion1[play_step];
        target2 = motion2[play_step];
        ++play_step;
        state = PLAYBACK;
        flashLed(YELLOW, 0);
      }
      else if (state == PLAYBACK) {
        uint32_t stepcolor = (play_step == 0) ? YELLOW : GREEN;
        target1 = motion1[play_step];
        target2 = motion2[play_step];
        ++play_step;
        if (play_step > laststep) play_step = 0;
        flashLed(stepcolor, 0);

        if (record_pending && play_step == 0) state = IDLE;
      }
    }
  }
  else {
    clocked = 0;
    clockdebouncetimer = millis();
  }

  if (!clockidle && (millis() - clocktimer) > CLOCK_RESET_MS) {
    play_step = 0;
    record_step = 0;
    clockidle = 1;
  }

  if (ledflash && ((millis() - ledtimer) > LEDOFF)) {
    setLed(led_restore);
    ledflash = 0;
  }

  if (!ledflash) {
    if (state == READY) setLed(GREEN);
    else if (state == PLAYBACK) setLed(0);
    else if (state == RECORDING) setLed(0);
    else if (state == IDLE) setLed(0);
    else setLed(RED);
  }
}

// second core setup
// second core is dedicated to sample processing
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

// process CV samples
void loop1() {
  static int32_t current1 = 0;
  static int32_t current2 = 0;

  int32_t t1 = target1;
  int32_t t2 = target2;
  uint16_t smooth = smoothcoef;

  if (smooth >= 65535) {
    current1 = t1;
    current2 = t2;
  }
  else {
    current1 += ((t1 - current1) * smooth) >> 16;
    current2 += ((t2 - current2) * smooth) >> 16;
  }

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE, 0); // low = core1 stalled because I2S buffer is full
#endif
 // write samples to DMA buffer - this is a blocking call so it stalls when buffer is full
	DAC.write(int16_t(current1)); // left
	DAC.write(int16_t(current2)); // right

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // hi = core1 busy
#endif
}
