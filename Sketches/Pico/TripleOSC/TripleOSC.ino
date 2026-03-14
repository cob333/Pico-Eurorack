
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
Oscillaor example for new 2HP hardware 
R Heslip  Dec 2025

3 oscillators - all have same waveform to keep it simple

Top Jack - Volt/Octave input

Middle jack - FM input

Bottom Jack - output

First Parameter Page - RED

Top pot - Waveform Sine, Triangle, Saw, Ramp (should be square wave but there is something broken in DaisySP)

Second pot - 1st Oscillator tuning

Third pot - 2nd Oscillator tuning

Fourth pot - 3rd Oscillator tuning

*/

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <math.h>

#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

#define GATE TRIGGER    // semantics - ADSR is generally used with a gate signal

//#define SAMPLERATE 11025 
//#define SAMPLERATE 22050  // 2HP board antialiasing filters are set up for 22khz
#define SAMPLERATE 44100  // not much DSP needed here so run at higher sample rate

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S DAC(OUTPUT);  // using PCM1103 stereo DAC

#include "daisysp.h"

// including the source files is a pain but that way you compile in only the modules you need
// DaisySP statically allocates memory and some modules e.g. reverb use a lot of ram
#include "synthesis/oscillator.cpp"

// we will use only the anitaliased versions of the waveforms
// *** something is wrong here - daisySP generates a ramp instead of a square wave
// my code and DaisySP oscillator code looks correct to me - I can't figure it out
#define NWAVES 4
uint8_t waves[NWAVES]={Oscillator::WAVE_SIN,Oscillator::WAVE_POLYBLEP_TRI,Oscillator::WAVE_POLYBLEP_SAW,Oscillator::WAVE_POLYBLEP_SQUARE};

float samplerate=SAMPLERATE;  // for DaisySP

#define VOICES 1
#define OSCSPERVOICE 3   
#define OSC_MIN_FREQ 20
#define OSC_MUTE_THRESHOLD 32  // pot value near full CCW that mutes the oscillator

int waveform=0;
float minfreq[OSCSPERVOICE] ={50,100,200};
volatile bool oscEnabled[OSCSPERVOICE] = {1,1,1};

// create daisySP processing objects
Oscillator osc[VOICES * OSCSPERVOICE];

#define CV_VOLT_DEFAULT 580.6f  // default a/d counts per volt - trim for V/octave
#define EEPROM_BYTES 256
#define CAL_STORE_MAGIC 0x32485043u // "2HPC"
#define CAL_STORE_VERSION 1u
#define CV_VOLT_MIN 100.0f
#define CV_VOLT_MAX 2000.0f

#define NUMUISTATES 1 // only 1 page in this UI
enum UIstates {OSCS};
uint8_t UIstate=OSCS;

bool button=0;
float cvVolt = CV_VOLT_DEFAULT;

enum CalState {
  CAL_OFF = 0,
  CAL_INPUT_1V,
  CAL_INPUT_3V,
  CAL_WAIT_OUTPUT
};

bool calibrationMode = 0;
CalState calState = CAL_OFF;
float calCvAt1V = 0.0f;
float calCvAt3V = 0.0f;
uint32_t calStateTimer = 0;
uint32_t calBlinkTimer = 0;
bool calBlinkOn = 0;

#define DEBOUNCE 10
uint32_t buttontimer,parameterupdate;

struct CalibrationStore {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  float cvVolt;
  uint32_t checksum;
};

static uint32_t calibrationChecksum(const CalibrationStore &data) {
  uint32_t hash = 2166136261u; // FNV-1a 32-bit
  const uint8_t *raw = reinterpret_cast<const uint8_t*>(&data);
  for (uint32_t i = 0; i < (uint32_t)(sizeof(CalibrationStore) - sizeof(uint32_t)); ++i) {
    hash ^= raw[i];
    hash *= 16777619u;
  }
  return hash;
}

static bool loadCalibrationFromFlash() {
  CalibrationStore data;
  EEPROM.get(0, data);
  if (data.magic != CAL_STORE_MAGIC) return 0;
  if (data.version != CAL_STORE_VERSION) return 0;
  if (!isfinite(data.cvVolt)) return 0;
  if ((data.cvVolt < CV_VOLT_MIN) || (data.cvVolt > CV_VOLT_MAX)) return 0;
  if (data.checksum != calibrationChecksum(data)) return 0;
  cvVolt = data.cvVolt;
  return 1;
}

static bool saveCalibrationToFlash() {
  CalibrationStore data;
  data.magic = CAL_STORE_MAGIC;
  data.version = CAL_STORE_VERSION;
  data.reserved = 0;
  data.cvVolt = cvVolt;
  data.checksum = calibrationChecksum(data);
  EEPROM.put(0, data);
  return EEPROM.commit();
}

static void updateOscTuningFromPot(uint8_t oscIndex, uint16_t potValue) {
  if (potValue <= OSC_MUTE_THRESHOLD) {
    oscEnabled[oscIndex] = 0;
    return;
  }

  oscEnabled[oscIndex] = 1;
  minfreq[oscIndex] = mapf(potValue, OSC_MUTE_THRESHOLD + 1, AD_RANGE - 1, 20, 160); // 4 octave tuning range
}

static void setLedColor(uint32_t color) {
  LEDS.setPixelColor(0, color);
  LEDS.show();
}

static void blinkLed(uint32_t color, uint8_t times, uint16_t onMs, uint16_t offMs) {
  for (uint8_t i = 0; i < times; ++i) {
    setLedColor(color);
    delay(onMs);
    setLedColor(0);
    delay(offMs);
  }
}

static bool buttonPressedEdge() {
  if (!digitalRead(BUTTON1)) {
    if (((millis()-buttontimer) > DEBOUNCE) && !button) {
      button=1;
      return 1;
    }
  }
  else {
    buttontimer=millis();
    button=0;
  }
  return 0;
}

static float sampleVOctCV() {
  return (float)(AD_RANGE - sampleCV1()); // CV input is inverted on hardware
}

static void startInputCalibration(bool showEntryFlash) {
  calibrationMode = 1;
  calState = CAL_INPUT_1V;
  if (showEntryFlash) blinkLed(GREEN, 3, 120, 120);
  setLedColor(GREEN);
}

static void finishCalibrationToOscMode() {
  calibrationMode = 0;
  calState = CAL_OFF;
  setLedColor(RED);
}

static void updateCalibration() {
  switch (calState) {
    case CAL_INPUT_1V:
      setLedColor(GREEN);
      if (buttonPressedEdge()) {
        calCvAt1V = sampleVOctCV();
        calState = CAL_INPUT_3V;
        setLedColor(YELLOW);
      }
      break;

    case CAL_INPUT_3V:
      setLedColor(YELLOW);
      if (buttonPressedEdge()) {
        calCvAt3V = sampleVOctCV();
        float span = fabsf(calCvAt3V - calCvAt1V); // 3V - 1V => 2V span
        if (span > 10.0f) {
          cvVolt = span * 0.5f;
          bool saved = saveCalibrationToFlash();
#ifdef DEBUG
          if (!saved) Serial.println("warning: failed to save calibration");
#else
          (void)saved;
#endif
        }
#ifdef DEBUG
        Serial.print("cal in: 1V=");
        Serial.print(calCvAt1V);
        Serial.print(" 3V=");
        Serial.print(calCvAt3V);
        Serial.print(" cvVolt=");
        Serial.println(cvVolt);
#endif
        calState = CAL_WAIT_OUTPUT;
        calStateTimer = millis();
        calBlinkTimer = millis();
        calBlinkOn = 1;
        setLedColor(RED);
      }
      break;

    case CAL_WAIT_OUTPUT:
      if ((millis() - calBlinkTimer) > 200) {
        calBlinkTimer = millis();
        calBlinkOn = !calBlinkOn;
        setLedColor(calBlinkOn ? RED : 0);
      }

      if (buttonPressedEdge()) {
        startInputCalibration(0);
      } else if ((millis() - calStateTimer) >= 5000) {
        finishCalibrationToOscMode();
      }
      break;

    case CAL_OFF:
    default:
      break;
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

  pinMode(CV1IN,INPUT); // top jack (V/Oct)
  pinMode(BUTTON1,INPUT_PULLUP); // button in
  pinMode(MUXCTL,OUTPUT);  // analog switch mux

  LEDS.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  setLedColor(RED);

  for (int j=0; j< OSCSPERVOICE; ++j) {
    osc[j].Init(samplerate);       // initialize the voice objects
    osc[j].SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);      
    osc[j].SetFreq(minfreq[j]);
  }

  analogReadResolution(AD_BITS); // set up for max resolution
  EEPROM.begin(EEPROM_BYTES);
  bool loaded = loadCalibrationFromFlash();
#ifdef DEBUG
  if (loaded) {
    Serial.print("loaded cvVolt from flash: ");
    Serial.println(cvVolt);
  } else {
    Serial.print("using default cvVolt: ");
    Serial.println(cvVolt);
  }
#else
  (void)loaded;
#endif

// set up Pico I2S for PT8211 stereo DAC
	DAC.setBCLK(BCLK);
	DAC.setDATA(I2S_DATA);
	DAC.setBitsPerSample(16);
	DAC.setBuffers(1, 128, 0); // DMA buffer - 32 bit L/R words
	DAC.setLSBJFormat();  // needed for PT8211 which has funny timing
	DAC.begin(SAMPLERATE);

  delay(20); // allow pull-up to settle after power-up
  if (!digitalRead(BUTTON1)) {
    // enter self-calibration when button is held while powering up
    button = 1; // consume current hold; require release before first calibration click
    startInputCalibration(1);
  }

#ifdef DEBUG  
  Serial.println("finished setup");  
#endif
}


void loop() {
  if (calibrationMode) {
    updateCalibration();
    return;
  }

// only one page in this app but the code is here in case somebody wants to add pages
  if (buttonPressedEdge()) {  // if button pressed advance to next parameter set
    ++UIstate;
    if (UIstate >= NUMUISTATES) UIstate=OSCS;
    lockpots();
  }

  if ((millis() -parameterupdate) > PARAMETERUPDATE) {  // don't update the parameters too often -sometimes it messes up the daisySP models
    parameterupdate=millis();
    samplepots();

// assign parameters from panel pots
    switch (UIstate) {
        case OSCS:
          setLedColor(RED);
          if (!potlock[0]) {
            uint8_t waveform=(map(pot[0],0,AD_RANGE,0,NWAVES));
            for (int16_t i=0;i< OSCSPERVOICE;++i) osc[i].SetWaveform(waveform);
          }
          if (!potlock[1]) updateOscTuningFromPot(0, pot[1]);
          if (!potlock[2]) updateOscTuningFromPot(1, pot[2]);
          if (!potlock[3]) updateOscTuningFromPot(2, pot[3]);
          break;
        default:
          break;
    }
  }

  float cv=(float)(AD_RANGE-sampleCV1()); // V/Oct input (top jack), inverted in hardware
  float FM=(float)(AD_RANGE-sampleCV2())/AD_RANGE-0.5; // FM input (middle jack), range +-0.5 octave

  osc[0].SetFreq(pow(2,(cv/cvVolt)+FM)*minfreq[0]); // ~ 7 octave CV range
  osc[1].SetFreq(pow(2,(cv/cvVolt)+FM)*minfreq[1]);
  osc[2].SetFreq(pow(2,(cv/cvVolt)+FM)*minfreq[2]);
}

// second core setup
// second core is dedicated to sample processing
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

// process audio samples
void loop1(){

static  float sig;
static  int32_t outsample;

  sig=0;
  for (int j=0; j < OSCSPERVOICE; ++j) {
    float oscSig = osc[j].Process(); // keep phase running even when muted
    if (oscEnabled[j]) sig += oscSig; // pots 2/3/4 full CCW mute osc 1/2/3
  }
  sig=sig*0.68; // scale down to avoid overflow
  outsample = (int32_t)(sig*MULT_16)>>16; // scaling 

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE,0); // low - CPU not busy
#endif
 // write samples to DMA buffer - this is a blocking call so it stalls when buffer is full
	DAC.write(int16_t(outsample)); // left
	DAC.write(int16_t(outsample)); // right

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE,1); // hi = CPU busy
#endif
}
