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
// Dual clock generator/divider for 2HPico hardware
//
// top jack    - external clock input (auto fallback to internal if clock missing)
// middle jack - clock output 1 (right DAC channel)
// bottom jack - clock output 2 (left DAC channel)
//
// Pot 1:
//   no external clock -> clock 1 BPM (20..2000)
//   external clock    -> clock 1 divider (1..16 = 1 to 1/16)
//
// Pot 2:
//   no external clock -> clock 2 BPM (20..2000)
//   external clock    -> clock 2 divider (1..16 = 1 to 1/16)
//   sync mode         -> clock 2 ratio vs clock 1 (x8..x1../8)
//
// Pot 3 - clock 1 swing amount (0..50%)
// Pot 4 - clock 2 random amount (0..100%), resyncs every 16 clocks
// Button:
//   short press -> tap tempo for internal clock
//   long press 3s -> toggle sync mode (clock 1 master, clock 2 ratio from Pot 2 x8..x1../8)

#include <2HPico.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring on CPU_USE pin

#define SAMPLERATE 22050

#define CLOCKIN TRIGGER

#define BPM_MIN 20.0f
#define BPM_MAX 2000.0f
#define SWING_MAX 0.5f

#define TAP_MIN_US 30000UL    // 2000 BPM
#define TAP_MAX_US 3000000UL  // 20 BPM

#define EXT_TIMEOUT_US 1500000UL
#define EXT_MIN_PERIOD_US 5000UL
#define EXT_MAX_PERIOD_US 3000000UL

#define RANDOM_RESYNC_STEPS 16
#define RANDOM_MAX_DELAY_FRAC 0.8f

#define PARAM_UPDATE_MS 10
#define LONG_PRESS_SYNC_MS 3000

#define MIN_PULSE_US 2500UL
#define MAX_PULSE_US 20000UL

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);

volatile int16_t clock1out = GATELOW;  // right DAC channel -> middle jack
volatile int16_t clock2out = GATELOW;  // left DAC channel -> bottom jack

struct ClockState {
  uint8_t divider;
  uint8_t dividerCounter;
  uint32_t periodUs;
  uint32_t nextBoundaryUs;
  uint32_t pendingPulseUs;
  uint32_t pulseWidthUs;
  uint32_t pulseOffUs;
  bool pulseScheduled;
  bool pulseHigh;
  uint8_t patternStep;
  bool tapOverride;
  uint16_t tapPotAnchor;
};

ClockState clk1 = {1, 0, 500000UL, 0, 0, 10000UL, 0, 0, 0, 0, 0};
ClockState clk2 = {1, 0, 500000UL, 0, 0, 10000UL, 0, 0, 0, 0, 0};

bool button = 0;
bool clocked = 0;
bool externalClock = 0;
bool syncMode = 0;
bool longPressHandled = 0;

uint32_t buttontimer = 0;
uint32_t buttonpress = 0;
uint32_t clockdebouncetimer = 0;
uint32_t parameterupdate = 0;
uint32_t lastTapUs = 0;
uint32_t lastExtEdgeUs = 0;
uint32_t lastExtSeenUs = 0;
uint32_t extPeriodUs = 500000UL;  // default 120 BPM

float tapBpm = 120.0f;
float swingAmount = 0.0f;
float randomAmount = 0.0f;
uint32_t ledcolor = 0;
int8_t syncRatioStep = 0;  // -7..+7 => /8..x8
uint8_t syncRatioMul = 1;
uint8_t syncRatioDiv = 1;

void forceGateLow(ClockState &clk, volatile int16_t *out);

inline bool due(uint32_t nowUs, uint32_t targetUs) {
  return (int32_t)(nowUs - targetUs) >= 0;
}

uint32_t bpmToPeriodUs(float bpm) {
  if (bpm < BPM_MIN) bpm = BPM_MIN;
  if (bpm > BPM_MAX) bpm = BPM_MAX;
  return (uint32_t)(60000000.0f / bpm);
}

uint8_t potToDivider(uint16_t value) {
  uint8_t div = map(value, 0, AD_RANGE - 1, 1, 17);
  if (div < 1) div = 1;
  if (div > 16) div = 16;
  return div;
}

int8_t potToSyncRatioStep(uint16_t value) {
  int16_t step = map(value, 0, AD_RANGE - 1, -7, 8);
  if (step < -7) step = -7;
  if (step > 7) step = 7;
  return (int8_t)step;
}

void updateSyncRatio(uint16_t potValue) {
  int8_t newStep = potToSyncRatioStep(potValue);
  if (newStep == syncRatioStep) return;

  syncRatioStep = newStep;
  if (syncRatioStep >= 0) {
    syncRatioMul = (uint8_t)(syncRatioStep + 1);  // x1..x8
    syncRatioDiv = 1;
  }
  else {
    syncRatioMul = 1;
    syncRatioDiv = (uint8_t)(1 - syncRatioStep);  // /2../8
  }

  clk2.dividerCounter = 0;
  forceGateLow(clk2, &clock2out);
}

void setLed(uint32_t color) {
  if (color == ledcolor) return;
  LEDS.setPixelColor(0, color);
  LEDS.show();
  ledcolor = color;
}

uint32_t calcPulseWidthUs(uint32_t periodUs) {
  uint32_t width = periodUs / 5;
  if (width < MIN_PULSE_US) width = MIN_PULSE_US;
  if (width > MAX_PULSE_US) width = MAX_PULSE_US;
  return width;
}

void forceGateLow(ClockState &clk, volatile int16_t *out) {
  clk.pulseScheduled = 0;
  clk.pulseHigh = 0;
  *out = GATELOW;
}

void schedulePulse(ClockState &clk, uint32_t nowUs, uint32_t startUs, uint32_t periodUs) {
  if (due(nowUs, startUs)) startUs = nowUs;
  clk.pendingPulseUs = startUs;
  clk.pulseWidthUs = calcPulseWidthUs(periodUs);
  clk.pulseScheduled = 1;
}

void triggerClock1Boundary(uint32_t nowUs, uint32_t boundaryUs, uint32_t periodUs) {
  uint32_t delayUs = 0;
  if ((clk1.patternStep & 1) && (swingAmount > 0.0f)) {
    delayUs = (uint32_t)(periodUs * swingAmount);
    uint32_t maxDelay = periodUs / 2;
    if (delayUs > maxDelay) delayUs = maxDelay;
  }

  schedulePulse(clk1, nowUs, boundaryUs + delayUs, periodUs);
  clk1.patternStep ^= 1;
}

void triggerClock2Boundary(uint32_t nowUs, uint32_t boundaryUs, uint32_t periodUs) {
  uint32_t delayUs = 0;

  if (clk2.patternStep != 0 && randomAmount > 0.0f) {
    uint32_t maxDelayUs = (uint32_t)(periodUs * RANDOM_MAX_DELAY_FRAC * randomAmount);
    if (maxDelayUs > 0) delayUs = random(maxDelayUs + 1);
  }

  schedulePulse(clk2, nowUs, boundaryUs + delayUs, periodUs);

  ++clk2.patternStep;
  if (clk2.patternStep >= RANDOM_RESYNC_STEPS) clk2.patternStep = 0;
}

void servicePulseOutput(ClockState &clk, volatile int16_t *out, uint32_t nowUs) {
  if (clk.pulseScheduled && due(nowUs, clk.pendingPulseUs)) {
    clk.pulseScheduled = 0;
    clk.pulseHigh = 1;
    clk.pulseOffUs = nowUs + clk.pulseWidthUs;
    *out = GATEHIGH;
  }

  if (clk.pulseHigh && due(nowUs, clk.pulseOffUs)) {
    clk.pulseHigh = 0;
    *out = GATELOW;
  }
}

void enterInternalMode(uint32_t nowUs) {
  externalClock = 0;
  clk1.dividerCounter = 0;
  clk2.dividerCounter = 0;
  clk1.patternStep = 0;
  clk2.patternStep = 0;
  clk1.nextBoundaryUs = nowUs + clk1.periodUs;
  clk2.nextBoundaryUs = nowUs + clk2.periodUs;
  forceGateLow(clk1, &clock1out);
  forceGateLow(clk2, &clock2out);
}

void handleExternalClockEdge(uint32_t nowUs) {
  if (!externalClock) {
    externalClock = 1;
    clk1.dividerCounter = 0;
    clk2.dividerCounter = 0;
    clk1.patternStep = 0;
    clk2.patternStep = 0;
    forceGateLow(clk1, &clock1out);
    forceGateLow(clk2, &clock2out);
  }

  if (lastExtEdgeUs != 0) {
    uint32_t measuredUs = nowUs - lastExtEdgeUs;
    if (measuredUs >= EXT_MIN_PERIOD_US && measuredUs <= EXT_MAX_PERIOD_US) extPeriodUs = measuredUs;
  }
  lastExtEdgeUs = nowUs;
  lastExtSeenUs = nowUs;

  ++clk1.dividerCounter;
  if (clk1.dividerCounter >= clk1.divider) {
    clk1.dividerCounter = 0;
    clk1.periodUs = extPeriodUs * clk1.divider;
    triggerClock1Boundary(nowUs, nowUs, clk1.periodUs);
  }

  ++clk2.dividerCounter;
  if (clk2.dividerCounter >= clk2.divider) {
    clk2.dividerCounter = 0;
    clk2.periodUs = extPeriodUs * clk2.divider;
    triggerClock2Boundary(nowUs, nowUs, clk2.periodUs);
  }
}

void handleTap(uint32_t nowUs) {
  if (lastTapUs != 0) {
    uint32_t tapPeriodUs = nowUs - lastTapUs;
    if (tapPeriodUs >= TAP_MIN_US && tapPeriodUs <= TAP_MAX_US) {
      float bpm = 60000000.0f / (float)tapPeriodUs;
      if (bpm < BPM_MIN) bpm = BPM_MIN;
      if (bpm > BPM_MAX) bpm = BPM_MAX;
      tapBpm = bpm;

      clk1.tapOverride = 1;
      clk2.tapOverride = 1;
      clk1.tapPotAnchor = pot[0];
      clk2.tapPotAnchor = pot[1];

      if (!externalClock) {
        clk1.periodUs = bpmToPeriodUs(tapBpm);
        clk2.periodUs = bpmToPeriodUs(tapBpm);
        clk1.patternStep = 0;
        clk2.patternStep = 0;
        clk1.nextBoundaryUs = nowUs;
        clk2.nextBoundaryUs = nowUs;
      }
    }
  }
  lastTapUs = nowUs;
}

void updateControls(uint32_t nowUs) {
  samplepots();

  swingAmount = mapf(pot[2], 0, AD_RANGE - 1, 0.0f, SWING_MAX);
  randomAmount = mapf(pot[3], 0, AD_RANGE - 1, 0.0f, 1.0f);

  if (externalClock) {
    uint8_t newDiv1 = potToDivider(pot[0]);
    uint8_t newDiv2 = potToDivider(pot[1]);

    if (newDiv1 != clk1.divider) {
      clk1.divider = newDiv1;
      clk1.dividerCounter = 0;
      clk1.patternStep = 0;
      forceGateLow(clk1, &clock1out);
    }

    if (newDiv2 != clk2.divider) {
      clk2.divider = newDiv2;
      clk2.dividerCounter = 0;
      clk2.patternStep = 0;
      forceGateLow(clk2, &clock2out);
    }
  }
  else {
    if (clk1.tapOverride) {
      int delta = (int)pot[0] - (int)clk1.tapPotAnchor;
      if (abs(delta) > MIN_POT_CHANGE) clk1.tapOverride = 0;
    }
    if (clk2.tapOverride && !syncMode) {
      int delta = (int)pot[1] - (int)clk2.tapPotAnchor;
      if (abs(delta) > MIN_POT_CHANGE) clk2.tapOverride = 0;
    }

    float bpm1 = clk1.tapOverride ? tapBpm : mapf(pot[0], 0, AD_RANGE - 1, BPM_MIN, BPM_MAX);
    uint32_t newPeriod1 = bpmToPeriodUs(bpm1);

    if (newPeriod1 != clk1.periodUs) {
      clk1.periodUs = newPeriod1;
      if ((int32_t)(clk1.nextBoundaryUs - nowUs) > (int32_t)clk1.periodUs) clk1.nextBoundaryUs = nowUs + clk1.periodUs;
    }

    if (syncMode) {
      updateSyncRatio(pot[1]);
      uint32_t derivedPeriod = (uint32_t)(((uint64_t)clk1.periodUs * syncRatioDiv) / syncRatioMul);
      if (derivedPeriod < 1000UL) derivedPeriod = 1000UL;
      if (derivedPeriod != clk2.periodUs) {
        clk2.periodUs = derivedPeriod;
        if ((int32_t)(clk2.nextBoundaryUs - nowUs) > (int32_t)clk2.periodUs) clk2.nextBoundaryUs = nowUs + clk2.periodUs;
      }
    }
    else {
      float bpm2 = clk2.tapOverride ? tapBpm : mapf(pot[1], 0, AD_RANGE - 1, BPM_MIN, BPM_MAX);
      uint32_t newPeriod2 = bpmToPeriodUs(bpm2);
      if (newPeriod2 != clk2.periodUs) {
        clk2.periodUs = newPeriod2;
        if ((int32_t)(clk2.nextBoundaryUs - nowUs) > (int32_t)clk2.periodUs) clk2.nextBoundaryUs = nowUs + clk2.periodUs;
      }
    }
  }
}

void processInternalBoundaries(uint32_t nowUs) {
  if (externalClock) return;

  uint8_t guard = 0;
  while (due(nowUs, clk1.nextBoundaryUs) && guard < 8) {
    uint32_t boundaryUs = clk1.nextBoundaryUs;
    clk1.nextBoundaryUs += clk1.periodUs;
    triggerClock1Boundary(nowUs, boundaryUs, clk1.periodUs);

    if (syncMode) {
      if (syncRatioStep <= 0) {  // x1 or divisors
        ++clk2.dividerCounter;
        if (clk2.dividerCounter >= syncRatioDiv) {
          clk2.dividerCounter = 0;
          clk2.periodUs = (uint32_t)((uint64_t)clk1.periodUs * syncRatioDiv);
          triggerClock2Boundary(nowUs, boundaryUs, clk2.periodUs);
        }
      }
      else {
        // For multipliers, hard-sync clock 2 phase each clock 1 boundary.
        clk2.nextBoundaryUs = boundaryUs;
      }
    }

    ++guard;
  }

  if (syncMode) {
    if (syncRatioStep > 0) {
      guard = 0;
      while (due(nowUs, clk2.nextBoundaryUs) && guard < 16) {
        uint32_t boundaryUs = clk2.nextBoundaryUs;
        clk2.nextBoundaryUs += clk2.periodUs;
        triggerClock2Boundary(nowUs, boundaryUs, clk2.periodUs);
        ++guard;
      }
    }
  }
  else {
    guard = 0;
    while (due(nowUs, clk2.nextBoundaryUs) && guard < 8) {
      uint32_t boundaryUs = clk2.nextBoundaryUs;
      clk2.nextBoundaryUs += clk2.periodUs;
      triggerClock2Boundary(nowUs, boundaryUs, clk2.periodUs);
      ++guard;
    }
  }
}

void setup() {
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println("starting setup");
#endif

  pinMode(CLOCKIN, INPUT_PULLUP);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  analogReadResolution(AD_BITS);

  LEDS.begin();
  LEDS.setPixelColor(0, RED);
  LEDS.show();
  ledcolor = RED;

  randomSeed(analogRead(AIN2) + micros());

  clk1.periodUs = bpmToPeriodUs(tapBpm);
  clk2.periodUs = bpmToPeriodUs(tapBpm);
  uint32_t nowUs = micros();
  clk1.nextBoundaryUs = nowUs + clk1.periodUs;
  clk2.nextBoundaryUs = nowUs + clk2.periodUs;

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
  uint32_t nowUs = micros();
  uint32_t nowMs = millis();

  if ((nowMs - parameterupdate) > PARAM_UPDATE_MS) {
    parameterupdate = nowMs;
    updateControls(nowUs);
  }

  if (!digitalRead(BUTTON1)) {
    if (((nowMs - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      buttonpress = nowMs;
      longPressHandled = 0;
    }
    if (button && !longPressHandled && ((nowMs - buttonpress) >= LONG_PRESS_SYNC_MS)) {
      longPressHandled = 1;
      syncMode = !syncMode;

      clk2.dividerCounter = 0;
      clk2.patternStep = 0;
      forceGateLow(clk2, &clock2out);
      if (!externalClock) {
        clk1.nextBoundaryUs = nowUs + clk1.periodUs;
        clk2.nextBoundaryUs = nowUs + clk2.periodUs;
      }
    }
  }
  else {
    if (button && !longPressHandled) handleTap(nowUs);
    buttontimer = nowMs;
    button = 0;
    longPressHandled = 0;
  }

  if (!digitalRead(CLOCKIN)) {
    if (((nowMs - clockdebouncetimer) > CLOCK_DEBOUNCE) && !clocked) {
      clocked = 1;
      handleExternalClockEdge(nowUs);
    }
  }
  else {
    clocked = 0;
    clockdebouncetimer = nowMs;
  }

  if (externalClock && (nowUs - lastExtSeenUs) > EXT_TIMEOUT_US) enterInternalMode(nowUs);

  processInternalBoundaries(nowUs);

  servicePulseOutput(clk1, &clock1out, nowUs);
  servicePulseOutput(clk2, &clock2out, nowUs);

  if (button) setLed(WHITE);
  else if (externalClock) setLed(GREEN);
  else if (syncMode) setLed(ORANGE);
  else setLed(RED);
}

// second core setup
// second core is dedicated to DAC writing
void setup1() {
  delay(1000); // wait for main core to start up peripherals
}

void loop1() {
#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0); // low - core 1 stalled by DAC buffer
#endif
 // write values to DMA buffer - blocking call when buffer is full
  DAC.write(int16_t(clock2out)); // left -> bottom jack (clock 2)
  DAC.write(int16_t(clock1out)); // right -> middle jack (clock 1)

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // high - core 1 busy
#endif
}
