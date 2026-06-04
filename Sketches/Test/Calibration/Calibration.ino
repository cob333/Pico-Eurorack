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
// DAC/input calibration helper for 2HPico (RP2350)
// Outputs fixed voltages for DAC calibration and samples jack 2 for CV input
// calibration.
//
// Button: short press to cycle DAC output modes
//         hold 3 seconds to enter jack 2 input calibration
// LED color indicates the current output mode.
//
// Output:
// - Left DAC channel (bottom jack) = calibration CV
// - Right DAC channel (middle jack) = same calibration CV
//
// Input calibration:
// - Hold button 3 seconds: blink VIOLET 3 times, enter input calibration
// - ORANGE: patch 1V into jack 2, press button to sample it
// - YELLOW: jack 3 outputs theoretical 1V DAC value, press button to confirm
// - AQUA: patch 5V into jack 2, press button to sample it
// - BLUE: jack 3 outputs theoretical 5V DAC value, press button to confirm
// - GREEN blink 3 times on success, RED blink 3 times on failure
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
#define CVOUT_VOLT 5456 // D/A count per volt (adjust to calibrate)
#define INPUT_CAL_HOLD_MS 3000
#define INPUT_CAL_SAMPLES 256
#define CVIN_COUNTS_PER_VOLT_MIN 100.0f
#define CVIN_COUNTS_PER_VOLT_MAX 1200.0f

enum CalMode {
  MODE_0V = 0,
  MODE_P1V,
  MODE_P2V,
  MODE_P4V,
  MODE_N1V,
  MODE_COUNT
};

enum InputCalState {
  INPUT_CAL_IDLE = 0,
  INPUT_CAL_WAIT_1V,
  INPUT_CAL_CONFIRM_1V_DAC,
  INPUT_CAL_WAIT_5V,
  INPUT_CAL_CONFIRM_5V_DAC
};

static volatile int16_t cvout_left = 0;
static volatile int16_t cvout_right = 0;
static uint8_t mode = MODE_0V;
static uint8_t inputCalState = INPUT_CAL_IDLE;
static bool button = 0;
static bool longPressHandled = 0;
static uint32_t buttontimer = 0;
static uint32_t buttonpress = 0;
static uint16_t inputCalRaw1V = 0;
static uint16_t inputCalRaw5V = 0;

static void setLed(uint32_t color) {
  LEDS.setPixelColor(0, color);
  LEDS.show();
}

static void setOutputs(int16_t left, int16_t right) {
  cvout_left = left;
  cvout_right = right;
}

static int16_t dacCountsForVoltage(uint8_t volts) {
  int32_t counts = (int32_t)volts * (int32_t)CVOUT_VOLT;
  if (counts > 32767) counts = 32767;
  if (counts < -32767) counts = -32767;
  return (int16_t)counts;
}

static void blinkLed(uint32_t color, uint8_t times) {
  for (uint8_t i = 0; i < times; ++i) {
    setLed(color);
    delay(140);
    setLed(0);
    delay(120);
  }
}

static uint16_t sampleCv2Average() {
  uint32_t total = 0;
  for (uint16_t i = 0; i < INPUT_CAL_SAMPLES; ++i) {
    total += analogRead(CV2IN);
    delayMicroseconds(80);
  }
  return (uint16_t)((total + (INPUT_CAL_SAMPLES / 2)) / INPUT_CAL_SAMPLES);
}

static void applyMode() {
  int16_t out = 0;
  uint32_t color = RED;
  switch (mode) {
    case MODE_0V:
      out = 0;
      color = RED;
      break;
    case MODE_P1V:
      out = dacCountsForVoltage(1);
      color = GREEN;
      break;
    case MODE_P2V:
      out = dacCountsForVoltage(2);
      color = BLUE;
      break;
    case MODE_P4V:
      out = dacCountsForVoltage(4);
      color = AQUA;
      break;
    case MODE_N1V:
      out = (int16_t)(-1 * CVOUT_VOLT);
      color = YELLOW;
      break;
    default:
      out = 0;
      color = RED;
      break;
  }
  setOutputs(out, out);
  setLed(color);
}

static void beginInputCalibration() {
  inputCalState = INPUT_CAL_WAIT_1V;
  setOutputs(0, 0);
  setLed(ORANGE);

#ifdef DEBUG
  Serial.println("input calibration: patch 1V into jack 2, press button");
#endif
}

static bool finishInputCalibration() {
  if (inputCalRaw1V <= inputCalRaw5V) return false; // CV input is inverted.

  const float countsPerVolt = ((float)inputCalRaw1V - (float)inputCalRaw5V) / 4.0f;
  const float zeroCounts = (float)inputCalRaw1V + countsPerVolt;
  if (countsPerVolt < CVIN_COUNTS_PER_VOLT_MIN ||
      countsPerVolt > CVIN_COUNTS_PER_VOLT_MAX ||
      zeroCounts < 0.0f ||
      zeroCounts > (float)AD_RANGE + countsPerVolt) {
    return false;
  }

#ifdef DEBUG
  Serial.print("input calibration raw 1V: ");
  Serial.println(inputCalRaw1V);
  Serial.print("input calibration raw 5V: ");
  Serial.println(inputCalRaw5V);
  Serial.print("CV2 counts per volt: ");
  Serial.println(countsPerVolt, 4);
  Serial.print("CV2 zero counts: ");
  Serial.println(zeroCounts, 4);
#endif

  return true;
}

static void completeInputCalibration(bool ok) {
  inputCalState = INPUT_CAL_IDLE;
  setOutputs(0, 0);
  blinkLed(ok ? GREEN : RED, 3);
  applyMode();
}

static void advanceInputCalibration() {
  switch (inputCalState) {
    case INPUT_CAL_WAIT_1V:
      inputCalRaw1V = sampleCv2Average();
      setOutputs(dacCountsForVoltage(1), 0); // jack 3 only
      setLed(YELLOW);
      inputCalState = INPUT_CAL_CONFIRM_1V_DAC;
#ifdef DEBUG
      Serial.print("sampled jack 2 at 1V: ");
      Serial.println(inputCalRaw1V);
      Serial.println("jack 3 outputting theoretical 1V DAC value, press button");
#endif
      break;
    case INPUT_CAL_CONFIRM_1V_DAC:
      setOutputs(0, 0);
      setLed(AQUA);
      inputCalState = INPUT_CAL_WAIT_5V;
#ifdef DEBUG
      Serial.println("input calibration: patch 5V into jack 2, press button");
#endif
      break;
    case INPUT_CAL_WAIT_5V:
      inputCalRaw5V = sampleCv2Average();
      setOutputs(dacCountsForVoltage(5), 0); // jack 3 only
      setLed(BLUE);
      inputCalState = INPUT_CAL_CONFIRM_5V_DAC;
#ifdef DEBUG
      Serial.print("sampled jack 2 at 5V: ");
      Serial.println(inputCalRaw5V);
      Serial.println("jack 3 outputting theoretical 5V DAC value, press button");
#endif
      break;
    case INPUT_CAL_CONFIRM_5V_DAC:
      completeInputCalibration(finishInputCalibration());
      break;
    default:
      completeInputCalibration(false);
      break;
  }
}

static void serviceButton() {
  const uint32_t now = millis();
  if (!digitalRead(BUTTON1)) {
    if (((now - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      buttonpress = now;
      longPressHandled = 0;
    }
    if (button && !longPressHandled &&
        inputCalState == INPUT_CAL_IDLE &&
        ((now - buttonpress) >= INPUT_CAL_HOLD_MS)) {
      longPressHandled = 1;
      blinkLed(VIOLET, 3);
      beginInputCalibration();
    }
  } else {
    if (button && !longPressHandled) {
      if (inputCalState == INPUT_CAL_IDLE) {
        ++mode;
        if (mode >= MODE_COUNT) mode = MODE_0V;
        applyMode();
      } else {
        advanceInputCalibration();
      }
    }
    buttontimer = now;
    button = 0;
    longPressHandled = 0;
  }
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
  pinMode(CV2IN, INPUT);

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
  serviceButton();
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

  // send calibration CV to the DAC channels
  DAC.write(int16_t(cvout_left));  // left, bottom jack / jack 3
  DAC.write(int16_t(cvout_right)); // right, middle jack

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1);
#endif
}
