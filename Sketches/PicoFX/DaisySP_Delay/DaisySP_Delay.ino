
// Copyright 2026 Rich Heslip
//
// Author: Rich Heslip 
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
/*
Delay example for 2HPico DSP hardware 
R Heslip  Jan 2026
Jan 30/25 - moved the setDelay() calls to the sample loop - improved the audio glitching when changing delay time

Not very CPU intensive - Works OK at 150mhz and 44khz sampling

Top Jack - Audio input 

Middle jack - In mono mode this becomes an external trigger clock input
              (solder the CV jumper on the back of the PCB)
              In ping pong mode it remains the right audio out
              (solder the DAC jumper on the back of the PCB)

Bottom Jack - Left / mono audio out

First Parameter Page - RED

Top pot - Delay Time

Second pot - Feedback

Third pot - Blends delay signal with dry signal

Fourth pot - Output level

Button - Toggles between mono and ping pong stereo output

*/

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

//#define SAMPLERATE 11025 
//#define SAMPLERATE 22050  // 
#define SAMPLERATE 44100  // not much DSP needed here so run at higher sample rate

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S i2s(INPUT_PULLUP); // both input and output

#include "daisysp.h"

// including the source files is a pain but that way you compile in only the modules you need
// DaisySP statically allocates memory and some modules e.g. reverb use a lot of ram
#include "Utility/delayline.h"

float samplerate=SAMPLERATE;  // for DaisySP

daisysp::DelayLine<float,SAMPLERATE> delayL;  // 1 second delay
daisysp::DelayLine<float,SAMPLERATE> delayR; 

#define CV_VOLT 580.6  // a/d counts per volt - trim for V/octave

#define NUMUISTATES 1 // only 1 page in this UI
enum UIstates {DELAY};
uint8_t UIstate=DELAY;

bool button=0;

#define DEBOUNCE 10
uint32_t buttontimer,parameterupdate,timelock;
volatile bool pingpongmode=0;

volatile float delayfeedback,delaymix, outputlevel;
// filtered Values for smooth delay time changes.
#define TIMEAVG 2000
float smoothed_time;
volatile float delay_time;
volatile float sync_ratio = 1.0f;

constexpr float DEFAULT_DELAY_SAMPLES = SAMPLERATE / 4.0f;
constexpr float MIN_DELAY_SAMPLES = 4.0f;
constexpr float MAX_DELAY_SAMPLES = SAMPLERATE - 4.0f;
constexpr uint32_t EXT_TIMEOUT_US = 1500000UL;
constexpr uint32_t EXT_MIN_PERIOD_US = 5000UL;
constexpr uint32_t EXT_MAX_PERIOD_US = 3000000UL;
constexpr uint32_t DELAY_CLOCK_DEBOUNCE_MS = 3;
constexpr float CLOCK_FILTER_ALPHA_SLOW = 0.12f;
constexpr float CLOCK_FILTER_ALPHA_FAST = 0.45f;
constexpr float CLOCK_UPDATE_HYSTERESIS_FRAC = 0.02f;
constexpr float CLOCK_UPDATE_HYSTERESIS_SAMPLES = 24.0f;
volatile uint32_t synced_clock_samples = (uint32_t)DEFAULT_DELAY_SAMPLES;
volatile float synced_delay_target = DEFAULT_DELAY_SAMPLES;

bool externalClock = 0;
bool rawClockState = 1;
bool debouncedClockState = 1;
uint32_t clockStateChangedMs = 0;
uint32_t lastExtEdgeUs = 0;
uint32_t lastExtSeenUs = 0;
float filteredClockSamples = DEFAULT_DELAY_SAMPLES;
bool filteredClockValid = 0;

float QuantizedClockRatio(uint16_t pot_value) {
  static const float kClockRatios[] = {
    0.16666667f, 0.25f, 0.33333334f, 0.5f, 0.66666667f,
    1.0f, 1.33333334f, 2.0f, 2.66666667f
  };

  uint16_t clockwise_value = (AD_RANGE - 1) - pot_value;
  int index = (clockwise_value * (int)(sizeof(kClockRatios) / sizeof(kClockRatios[0]))) / AD_RANGE;
  int max_index = (int)(sizeof(kClockRatios) / sizeof(kClockRatios[0])) - 1;
  if (index > max_index) index = max_index;
  return kClockRatios[index];
}

float ClampDelaySamples(float samples) {
  if (samples < MIN_DELAY_SAMPLES) return MIN_DELAY_SAMPLES;
  if (samples > MAX_DELAY_SAMPLES) return MAX_DELAY_SAMPLES;
  return samples;
}

void UpdateSyncedDelayTarget(bool force) {
  float newTarget = ClampDelaySamples(filteredClockSamples * sync_ratio);
  float diff = fabsf(newTarget - synced_delay_target);
  float hysteresis = synced_delay_target * CLOCK_UPDATE_HYSTERESIS_FRAC;
  if (hysteresis < CLOCK_UPDATE_HYSTERESIS_SAMPLES) hysteresis = CLOCK_UPDATE_HYSTERESIS_SAMPLES;

  if (force || diff >= hysteresis) synced_delay_target = newTarget;
}

void enterFreeRunSyncClock() {
  externalClock = 0;
  lastExtEdgeUs = 0;
}

void handleExternalClockEdge(uint32_t nowUs) {
  if (!externalClock) externalClock = 1;

  if (lastExtEdgeUs != 0) {
    uint32_t measuredUs = nowUs - lastExtEdgeUs;
    if (measuredUs >= EXT_MIN_PERIOD_US && measuredUs <= EXT_MAX_PERIOD_US) {
      float measuredSamples = ((float)measuredUs * samplerate) / 1000000.0f;
      measuredSamples = ClampDelaySamples(measuredSamples);

      if (!filteredClockValid) {
        filteredClockSamples = measuredSamples;
        filteredClockValid = 1;
      }
      else {
        float deltaFrac = fabsf(measuredSamples - filteredClockSamples) / filteredClockSamples;
        float alpha = (deltaFrac > 0.10f) ? CLOCK_FILTER_ALPHA_FAST : CLOCK_FILTER_ALPHA_SLOW;
        filteredClockSamples += (measuredSamples - filteredClockSamples) * alpha;
      }

      synced_clock_samples = (uint32_t)filteredClockSamples;
      UpdateSyncedDelayTarget(0);
    }
  }

  lastExtEdgeUs = nowUs;
  lastExtSeenUs = nowUs;
}

void setup() { 
  Serial.begin(115200);

#ifdef DEBUG

  Serial.println("starting setup");  
#endif

// set up I/O pins
 
#ifdef MONITOR_CPU1 // for monitoring 2nd core CPU usage
  pinMode(CPU_USE,OUTPUT); // hi = CPU busy
#endif 

  pinMode(BUTTON1,INPUT_PULLUP); // button in
  pinMode(MUXCTL,OUTPUT);  // analog switch mux
  pinMode(CV2IN, INPUT_PULLUP); // middle jack can be used as external sync clock in mono mode

  LEDS.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  LEDS.setPixelColor(0, RED); 
  LEDS.show();

  analogReadResolution(AD_BITS); // set up for max resolution

// set up I2S for 32 bits in and out
// PCM1808 is 24 bit only but I could not get 24 bit I2S working. 32 bits is little if any extra overhead
  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK); // Note: LRCLK = BCLK + 1
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(22050);
  i2s.begin();

  delayL.Init();
  delayR.Init();
  delayL.SetDelay(DEFAULT_DELAY_SAMPLES);
  delayR.SetDelay(DEFAULT_DELAY_SAMPLES);
  delayfeedback=0.5;
  delaymix=0.7;
  outputlevel=0.5;
  delay_time=DEFAULT_DELAY_SAMPLES;
  synced_delay_target=DEFAULT_DELAY_SAMPLES;

#ifdef DEBUG  
  Serial.println("finished setup");  
#endif
}


void loop() {
  uint32_t nowUs = micros();
  uint32_t nowMs = millis();

// only one page in this app but the code is here in case somebody wants to add pages
  if (!digitalRead(BUTTON1)) {
    if (((nowMs-buttontimer) > DEBOUNCE) && !button) {  // if button pressed advance to next parameter set
      button=1;  
      ++UIstate;
      if (UIstate >= NUMUISTATES) UIstate=DELAY; // only one parameter set
      pingpongmode=!pingpongmode; // button toggles pingpong mode
      if (pingpongmode) LEDS.setPixelColor(0, GREEN); 
      else LEDS.setPixelColor(0, RED); 
      LEDS.show();
      lockpots();
    }
  }
  else {
    buttontimer=nowMs;
    button=0;
  }

  if (!pingpongmode) {
    bool rawClockLow = !digitalRead(CV2IN);
    if (rawClockLow != rawClockState) {
      rawClockState = rawClockLow;
      clockStateChangedMs = nowMs;
    }

    if (((nowMs - clockStateChangedMs) >= DELAY_CLOCK_DEBOUNCE_MS) && (debouncedClockState != rawClockState)) {
      debouncedClockState = rawClockState;
      if (debouncedClockState) handleExternalClockEdge(nowUs);
    }

    if (externalClock && (nowUs - lastExtSeenUs) > EXT_TIMEOUT_US) enterFreeRunSyncClock();
  }
  else {
    rawClockState = 1;
    debouncedClockState = 1;
    clockStateChangedMs = nowMs;
  }

  samplepots();
  smoothed_time+=mapf(pot[0],0,AD_RANGE-1,0,SAMPLERATE); // calculate moving average of time setting - any jumping around is very noticable in the audio
  smoothed_time-=smoothed_time/TIMEAVG; 

  if ((nowMs -parameterupdate) > PARAMETERUPDATE) {  // don't update the parameters too often -sometimes it messes up the daisySP models
    parameterupdate=nowMs;

// assign parameters from panel pots
    switch (UIstate) {
        case DELAY:
          if (!potlock[0]) {
            if (pingpongmode) delay_time=ClampDelaySamples(smoothed_time/TIMEAVG);
            else {
              sync_ratio=QuantizedClockRatio(pot[0]);
              UpdateSyncedDelayTarget(1);
            }
          }
          if (!potlock[1]) delayfeedback=(mapf(pot[1],0,AD_RANGE-1,0,1.0)); // 
          if (!potlock[2]) delaymix=(mapf(pot[2],0,AD_RANGE-1,0,1.0)); // 
          if (!potlock[3]) outputlevel=(mapf(pot[3],0,AD_RANGE-1,0,1.0)); // 
          break;
        default:
          break;
    }
  }

}

// second core setup
// second core is dedicated to sample processing
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

// process audio samples
void loop1(){
  float sigL,sigR,delayedL,delayedR;
  static float current_time = DEFAULT_DELAY_SAMPLES;
  int32_t left,right;



// these calls will stall if not data is available
  left=i2s.read();    // input is mono but we still have to read both channels
  right=i2s.read();

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE,1); // hi = CPU busy
#endif

// ramp delay time up or down by one sample to match the time set by user 
// this minimizes the glitches caused by large changes in delay time
  float target_time = delay_time;
  if (!pingpongmode) {
    target_time = synced_delay_target;
  }

  float slew_step = pingpongmode ? 1.0f : 64.0f;
  if (current_time < target_time) {
    current_time += slew_step;
    if (current_time > target_time) current_time = target_time;
  }
  else if (current_time > target_time) {
    current_time -= slew_step;
    if (current_time < target_time) current_time = target_time;
  }

  delayL.SetDelay(current_time);
  delayR.SetDelay(current_time);

  sigL=left*DIV_16; // convert input to float for DaisySP
  sigR=left*DIV_16; 

  delayedL=delayL.Read();

  if (pingpongmode) {  // ping pong mode - bounce the signal from left to right and back
    delayedR=delayR.Read();
    delayL.Write(sigL + (delayedR*delayfeedback));
    delayR.Write((delayedL*delayfeedback)); 

    sigL=(sigL+ delayedL*delaymix)*outputlevel; // add delay to dry signal 
    sigR=(sigR+ delayedR*delaymix)*outputlevel;
  }
  else {
    delayR.Write(0.0f); // keep the unused right delay line flushed while mono sync mode is active
    delayL.Write(sigL + (delayedL*delayfeedback));
    sigL=(sigL+ delayedL*delaymix)*outputlevel; // mono delay output is mirrored to both channels
    sigR=sigL;
  }

  left=(int32_t)(sigL*MULT_16); // convert output back to int32
  right=(int32_t)(sigR*MULT_16); // convert output back to int32

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE,0); // low - CPU not busy
#endif

// these calls will stall if buffer is full
	i2s.write(left); // left passthru
	i2s.write(right); // right passthru

}
