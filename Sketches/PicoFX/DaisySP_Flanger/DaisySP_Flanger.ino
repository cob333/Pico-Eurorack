
// Copyright 2025 Rich Heslip
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
Flanger example for 2HPico DSP hardware 
R Heslip  Jan 2026

Overclock needed! Run 44khz sampling at 250mhz

Top Jack - Audio input 

Middle jack - Right Audio out (solder the DAC jumper on the back of the PCB)

Bottom Jack - Left Audio out

First Parameter Page - RED

Top pot - Delay

Second pot - Feedback

Third pot - LFO frequency

Fourth pot - LFO Depth

Second Parameter Page - Green

Top pot - Wet- Dry Mix

Second pot - Output Level

Third pot - Stereo Width

Fourth pot - LFO Waveform：
  BLUE   = Triangle
  VIOLET = Sine
  WHITE  = Square
  YELLOW = Saw
  AQUA   = Smooth Random
  ORANGE = Stepped Random

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
#define SAMPLERATE 44100  // run at higher sample rate - works OK at 150mhz

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S i2s(INPUT_PULLUP); // both input and output

#include "daisysp.h"

float samplerate=SAMPLERATE;  // for DaisySP

class PicoFlanger
{
public:
  enum LfoWaveform : uint8_t {
    WAVE_TRIANGLE = 0,
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_RANDOM_SMOOTH,
    WAVE_RANDOM_STEPPED,
    WAVE_LAST
  };

  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    del_.Init();
    lfo_phase_ = 0.0f;
    lfo_amp_ = 0.0f;
    waveform_ = WAVE_TRIANGLE;
    SetRandomSeed(0x12345678u);
    SetFeedback(0.2f);
    SetDelay(0.75f);
    SetLfoFreq(0.3f);
    SetLfoDepth(0.9f);
  }

  float Process(float in) {
    float lfo = ProcessLfo();
    del_.SetDelay(1.0f + delay_ + lfo);

    float out = del_.Read();
    del_.Write(in + out * feedback_);

    return (in + out) * 0.5f;
  }

  void SetFeedback(float feedback) {
    feedback_ = daisysp::fclamp(feedback, 0.0f, 1.0f) * 0.97f;
  }

  void SetLfoDepth(float depth) {
    depth = daisysp::fclamp(depth, 0.0f, 0.93f);
    lfo_amp_ = depth * delay_;
  }

  void SetLfoFreq(float freq) {
    lfo_freq_ = daisysp::fclamp(freq / sample_rate_, 0.0f, 0.25f);
  }

  void SetLfoWaveform(uint8_t waveform) {
    waveform_ = waveform < WAVE_LAST ? waveform : WAVE_TRIANGLE;
  }

  void SetDelay(float delay) {
    delay = 0.1f + delay * 6.9f;
    SetDelayMs(delay);
  }

  void SetDelayMs(float ms) {
    ms = fmaxf(0.1f, ms);
    delay_ = ms * 0.001f * sample_rate_;
    lfo_amp_ = fminf(lfo_amp_, delay_);
  }

  void SetRandomSeed(uint32_t seed) {
    rand_state_ = seed ? seed : 1u;
    rand_from_ = RandomBipolar();
    rand_to_ = RandomBipolar();
    rand_hold_ = RandomBipolar();
  }

private:
  static constexpr float kTwoPi = 6.28318530717958647692f;
  static constexpr int32_t kDelayLength = 960;

  float ProcessLfo() {
    lfo_phase_ += lfo_freq_;

    while (lfo_phase_ >= 1.0f) {
      lfo_phase_ -= 1.0f;
      AdvanceRandomLfo();
    }

    float lfo = 0.0f;

    switch (waveform_) {
      case WAVE_SINE:
        lfo = sinf(lfo_phase_ * kTwoPi);
        break;
      case WAVE_SQUARE:
        lfo = lfo_phase_ < 0.5f ? 1.0f : -1.0f;
        break;
      case WAVE_SAW:
        lfo = lfo_phase_ * 2.0f - 1.0f;
        break;
      case WAVE_RANDOM_SMOOTH:
        lfo = rand_from_ + (rand_to_ - rand_from_) * lfo_phase_;
        break;
      case WAVE_RANDOM_STEPPED:
        lfo = rand_hold_;
        break;
      case WAVE_TRIANGLE:
      default:
        lfo = 1.0f - 4.0f * fabsf(lfo_phase_ - 0.5f);
        break;
    }

    return lfo * lfo_amp_;
  }

  void AdvanceRandomLfo() {
    rand_from_ = rand_to_;
    rand_to_ = RandomBipolar();
    rand_hold_ = RandomBipolar();
  }

  float RandomBipolar() {
    rand_state_ = rand_state_ * 1664525u + 1013904223u;
    return ((rand_state_ >> 8) * (1.0f / 8388607.5f)) - 1.0f;
  }

  float sample_rate_;
  float feedback_;
  float lfo_phase_;
  float lfo_freq_;
  float lfo_amp_;
  float delay_;
  uint8_t waveform_;
  uint32_t rand_state_;
  float rand_from_;
  float rand_to_;
  float rand_hold_;
  daisysp::DelayLine<float, kDelayLength> del_;
};

PicoFlanger flangerL, flangerR;

constexpr float FLANGER_DEFAULT_DELAY = 0.75f;
constexpr float FLANGER_DEFAULT_FEEDBACK = 0.2f;
constexpr float FLANGER_DEFAULT_LFO_FREQ = 0.3f;
constexpr float FLANGER_DEFAULT_LFO_DEPTH = 0.9f;
constexpr float FLANGER_STEREO_PHASE_OFFSET = 0.5f; // 180 degrees
constexpr uint32_t LFO_WAVEFORM_DISPLAY_MS = 2000;

#define CV_VOLT 580.6  // a/d counts per volt - trim for V/octave

#define NUMUISTATES 2 // 
enum UIstates {FLANGER,MIX};
uint8_t UIstate=FLANGER;

bool button=0;

#define DEBOUNCE 10
uint32_t buttontimer,parameterupdate;

float mix=0.5;
float outputlevel=0.8;
float stereowidth=1.0;
uint8_t lfowaveform = PicoFlanger::WAVE_TRIANGLE;
uint32_t wavecolortimer = 0;

void SetFlangerDelay(float value) {
  flangerL.SetDelay(value);
  flangerR.SetDelay(value);
}

void SetFlangerFeedback(float value) {
  flangerL.SetFeedback(value);
  flangerR.SetFeedback(value);
}

void SetFlangerLfoFreq(float value) {
  flangerL.SetLfoFreq(value);
  flangerR.SetLfoFreq(value);
}

void SetFlangerLfoDepth(float value) {
  flangerL.SetLfoDepth(value);
  flangerR.SetLfoDepth(value);
}

void SetFlangerLfoWaveform(uint8_t waveform) {
  flangerL.SetLfoWaveform(waveform);
  flangerR.SetLfoWaveform(waveform);
}

uint32_t GetLfoWaveformColor(uint8_t waveform) {
  switch (waveform) {
    case PicoFlanger::WAVE_TRIANGLE:
      return BLUE;
    case PicoFlanger::WAVE_SINE:
      return VIOLET;
    case PicoFlanger::WAVE_SQUARE:
      return WHITE;
    case PicoFlanger::WAVE_SAW:
      return YELLOW;
    case PicoFlanger::WAVE_RANDOM_SMOOTH:
      return AQUA;
    case PicoFlanger::WAVE_RANDOM_STEPPED:
      return ORANGE;
    default:
      return BLUE;
  }
}

uint8_t ReadLfoWaveformFromPot(uint16_t value) {
  uint32_t waveform = (uint32_t)value * PicoFlanger::WAVE_LAST / AD_RANGE;
  if (waveform >= PicoFlanger::WAVE_LAST) waveform = PicoFlanger::WAVE_LAST - 1;
  return (uint8_t)waveform;
}

void PrimeFlangerStereoOffset(float lfofreq) {
  if (lfofreq <= 0.0f) {
    return;
  }

  int32_t offsetsamples = (int32_t)(samplerate * FLANGER_STEREO_PHASE_OFFSET / lfofreq);

  for (int32_t i = 0; i < offsetsamples; ++i) {
    flangerR.Process(0.0f);
  }
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
  i2s.setFrequency(44100);
  i2s.begin();

  flangerL.Init(samplerate);
  flangerR.Init(samplerate);
  flangerL.SetRandomSeed(0x12345678u);
  flangerR.SetRandomSeed(0x87654321u);
  SetFlangerDelay(FLANGER_DEFAULT_DELAY);
  SetFlangerFeedback(FLANGER_DEFAULT_FEEDBACK);
  SetFlangerLfoFreq(FLANGER_DEFAULT_LFO_FREQ);
  SetFlangerLfoDepth(FLANGER_DEFAULT_LFO_DEPTH);
  SetFlangerLfoWaveform(lfowaveform);
  PrimeFlangerStereoOffset(FLANGER_DEFAULT_LFO_FREQ);

#ifdef DEBUG  
  Serial.println("finished setup");  
#endif
}


void loop() {
  uint32_t now = millis();

// select UI page
  if (!digitalRead(BUTTON1)) {
    if (((now-buttontimer) > DEBOUNCE) && !button) {  // if button pressed advance to next parameter set
      button=1;  
      ++UIstate;
      if (UIstate >= NUMUISTATES) UIstate=FLANGER;
      lockpots();
    }
  }
  else {
    buttontimer=now;
    button=0;
  }

  if ((now -parameterupdate) > PARAMETERUPDATE) {  // don't update the parameters too often -sometimes it messes up the daisySP models
    parameterupdate=now;
    samplepots();

// assign parameters from panel pots
    switch (UIstate) {
        case FLANGER:
          LEDS.setPixelColor(0, RED); 
          if (!potlock[0]) SetFlangerDelay(mapf(pot[0],0,AD_RANGE-1,0,1.0)); 
          if (!potlock[1]) SetFlangerFeedback(mapf(pot[1],0,AD_RANGE-1,0,1.0)); 
          if (!potlock[2]) SetFlangerLfoFreq(mapf(pot[2],0,AD_RANGE-1,0,1.0));
          if (!potlock[3]) SetFlangerLfoDepth(mapf(pot[3],0,AD_RANGE-1,0,1.0)); // 
          break;
        case MIX:
          if (!potlock[0]) mix=(mapf(pot[0],0,AD_RANGE-1,0,1.0)); 
          if (!potlock[1]) outputlevel=(mapf(pot[1],0,AD_RANGE-1,0,1.0)); 
          if (!potlock[2]) stereowidth=(mapf(pot[2],0,AD_RANGE-1,0,1.0));
          if (!potlock[3]) {
            uint8_t newwaveform = ReadLfoWaveformFromPot(pot[3]);
            if (newwaveform != lfowaveform) {
              lfowaveform = newwaveform;
              SetFlangerLfoWaveform(lfowaveform);
            }
            wavecolortimer = now + LFO_WAVEFORM_DISPLAY_MS;
          }
          LEDS.setPixelColor(0, (now < wavecolortimer) ? GetLfoWaveformColor(lfowaveform) : GREEN); 
          break;
        default:
          break;
    }
  }
  LEDS.show();  // update LED
}

// second core setup
// second core is dedicated to sample processing
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

// process audio samples
void loop1(){
  float sigIn,wetL,wetR,wetMid,wetStereoL,wetStereoR,outL,outR;
  int32_t left,right;

// these calls will stall if no data is available
  left=i2s.read();    // input is mono but we still have to read both channels
  right=i2s.read();   // input is mono, so the second slot is discarded

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE,1); // hi = CPU busy
#endif

  sigIn=left*DIV_16; // convert input to float for DaisySP

  wetL=flangerL.Process(sigIn);
  wetR=flangerR.Process(sigIn);
  wetMid=(wetL + wetR) * 0.5f;
  wetStereoL=wetMid + (wetL - wetMid) * stereowidth;
  wetStereoR=wetMid + (wetR - wetMid) * stereowidth;

  outL=(sigIn*(1-mix)+ wetStereoL*mix)*outputlevel;
  outR=(sigIn*(1-mix)+ wetStereoR*mix)*outputlevel;

  left=(int32_t)(outL*MULT_16); // convert output back to int32
  right=(int32_t)(outR*MULT_16); // convert output back to int32

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE,0); // low - CPU not busy
#endif
// these calls will stall if buffer is full
	i2s.write(left); // left passthru
	i2s.write(right); // right passthru

}
