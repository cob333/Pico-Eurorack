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
// DAC calibration helper for 2HPico (RP2350)
// Outputs fixed voltages for calibration.
//
// Button: short press to cycle modes
// LED color indicates the current output mode.
//
// Output:
// - Left DAC channel (bottom jack) = calibration CV
// - Right DAC channel (middle jack) = same calibration CV
//
// V1.0 RH Jan 2026

#include <2HPico.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

#define SAMPLERATE 22050

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);

// DAC calibration constants
#define CVOUT_VOLT 6554 // D/A count per volt (adjust to calibrate)

enum CalMode {
  MODE_0V = 0,
  MODE_P1V,
  MODE_P2V,
  MODE_P4V,
  MODE_N1V,
  MODE_COUNT
};

static volatile int16_t cvout = 0;
static uint8_t mode = MODE_0V;
static bool button = 0;
static uint32_t buttontimer = 0;

static void applyMode() {
  switch (mode) {
    case MODE_0V:
      cvout = 0;
      LEDS.setPixelColor(0, RED);
      break;
    case MODE_P1V:
      cvout = (int16_t)(1 * CVOUT_VOLT);
      LEDS.setPixelColor(0, GREEN);
      break;
    case MODE_P2V:
      cvout = (int16_t)(2 * CVOUT_VOLT);
      LEDS.setPixelColor(0, BLUE);
      break;
    case MODE_P4V:
      cvout = (int16_t)(4 * CVOUT_VOLT);
      LEDS.setPixelColor(0, AQUA);
      break;
    case MODE_N1V:
      cvout = (int16_t)(-1 * CVOUT_VOLT);
      LEDS.setPixelColor(0, YELLOW);
      break;
    default:
      cvout = 0;
      LEDS.setPixelColor(0, RED);
      break;
  }
  LEDS.show();
}

void setup() {
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println("starting calibration setup");
#endif

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

  LEDS.begin();
  LEDS.setPixelColor(0, RED);
  LEDS.show();

  analogReadResolution(AD_BITS);

  DAC.setBCLK(BCLK);
  DAC.setDATA(I2S_DATA);
  DAC.setBitsPerSample(16);
  DAC.setBuffers(1, 128, 0);
  DAC.setLSBJFormat();
  DAC.begin(SAMPLERATE);

  applyMode();

#ifdef DEBUG
  Serial.println("finished calibration setup");
#endif
}

void loop() {
  if (!digitalRead(BUTTON1)) {
    if (((millis() - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      ++mode;
      if (mode >= MODE_COUNT) mode = MODE_0V;
      applyMode();
    }
  } else {
    buttontimer = millis();
    button = 0;
  }
}

// second core setup
void setup1() {
  delay(1000);
}

// process audio samples
void loop1() {
#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0);
#endif

  // send calibration CV to both channels
  DAC.write(int16_t(cvout)); // left
  DAC.write(int16_t(cvout)); // right

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1);
#endif
}
