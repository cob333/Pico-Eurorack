
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
/*
R Heslip for new 2HP hardware Nov 2025

// Moog voice app - 3 oscillators, Moog style lowpass filter, ADSR env gen and LFO for modulation
 **** Needs overlock to at least 200Mhz for 96khz sample rate ****

Top Jack - gate input 

Middle jack - Volt/Octave input

Bottom Jack - output

First Parameter Page - RED

Top pot - 1st Oscillator tuning

Second pot - 2nd Oscillator tuning

Third pot - 3rd Oscillator tuning

Fourth pot - Waveform

Second Parameter Page - ORANGE

Top pot - Filter Frequency

Second pot - Filter Resonance

Third pot - Envelope to Filter Frequency mod depth 

Fourth pot - LFO to Filter Frequency mod depth 

Third Parameter Page - GREEN

Top pot - ADSR Attack

Second pot - ADSR Decay

Third pot - ADSR Sustain

Fourth pot - ADSR Release

Fourth Parameter Page - Blue

Top pot - LFO Frequency

Second pot - LFO waveform

Third pot - 

Fourth pot - 

*/

#include "2HPico.h"
#include "PicoStateStore.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

#define GATE TRIGGER    // semantics - ADSR is generally used with a gate signal

//#define SAMPLERATE 11025 
//#define SAMPLERATE 22050  // 2HP board antialiasing filters are set up for 22khz
//#define SAMPLERATE 44100
#define SAMPLERATE 96000

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S DAC(OUTPUT);  // using PCM1103 stereo DAC

#include "daisysp.h"

// including the source files is a pain but that way you compile in only the modules you need
// DaisySP statically allocates memory and some modules e.g. reverb use a lot of ram
#include "Synthesis/oscillator.cpp"
#include "Control/adsr.cpp"
#include "Filters/moogladder.cpp"

float samplerate=SAMPLERATE;  // for DaisySP

#define VOICES 1
#define OSCSPERVOICE 3   // note - the detune code is set up for 3 oscillators

// parameters we can modify via MIDI CCs
int waveform=0;
float minfreq[OSCSPERVOICE] ={50,100,200};
float filterfreq=100;
float filtersweep=SAMPLERATE/4;
float envelopefiltermod=0.2;
float filterresonance=0.1;
float lfofreq=1.0;
float lfofreqmod=0;
float lfofiltermod=0;
float adsrAttack=0.0f,adsrDecay=0.4f,adsrSustain=0.0f,adsrRelease=0.0f;
int lfoWaveform=Oscillator::WAVE_TRI;
static constexpr uint32_t STATE_KEY=0x474f4f4du;
struct SavedState{int32_t waveform,lfoWaveform;float minfreq[OSCSPERVOICE],filterfreq,envelopefiltermod,filterresonance,lfofreq,lfofiltermod,attack,decay,sustain,release;};
PicoStateStore stateStore;
static SavedState captureState(){SavedState s={waveform,lfoWaveform,{minfreq[0],minfreq[1],minfreq[2]},filterfreq,envelopefiltermod,filterresonance,lfofreq,lfofiltermod,adsrAttack,adsrDecay,adsrSustain,adsrRelease};return s;}
static constexpr uint32_t MANUAL_SAVE_HOLD_MS=3000;
uint32_t save_buttonpress=0;
bool save_hold_handled=0;

static void blinkSaveResult(bool saved){
  uint32_t color=saved?GREEN:RED;
  for(uint8_t i=0;i<3;++i){
    LEDS.setPixelColor(0,color);LEDS.show();delay(120);
    LEDS.setPixelColor(0,0);LEDS.show();delay(120);
  }
}

// create daisySP processing objects
Oscillator osc[VOICES * OSCSPERVOICE];
Oscillator lfo;
Adsr      env;
MoogLadder filt;


// a/d values from pots
// pots are used for two or more parameters so we don't change the values till
// there is a significant movement of the pots when the pots are "locked"
// this prevents a waveform or level change ("shift" parameters) from changing the ramp times when the shift button is released

#define CV_VOLT 582.52f  // a/d counts per volt - trim for V/octave

#define OSC_MIN_FREQ 20
#define FILTER_MIN_FREQ 50

bool gate=0;
bool button=0;

#define NUMUISTATES 4
enum UIstates {OSCS,FILTER,ADSR,LFO} ;
uint8_t UIstate=OSCS;

#define DEBOUNCE 10
uint32_t buttontimer,gatetimer,parameterupdate;

void setup() { 
  Serial.begin(115200);

#ifdef DEBUG

  Serial.println("starting setup");  
#endif

// set up I/O pins
 
#ifdef MONITOR_CPU1 // for monitoring 2nd core CPU usage
  pinMode(CPU_USE,OUTPUT); // hi = CPU busy
#endif 

  pinMode(GATE,INPUT_PULLUP); // gate/trigger in
  pinMode(BUTTON1,INPUT_PULLUP); // button in
  pinMode(MUXCTL,OUTPUT);  // analog switch mux

  LEDS.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  LEDS.setPixelColor(0, RED); 
  LEDS.show();

  for (int j=0; j< OSCSPERVOICE; ++j) {
    osc[j].Init(samplerate);       // initialize the voice objects
    osc[j].SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);  //    
    osc[j].SetFreq(minfreq[j]);
  }

  env.Init(samplerate);
  env.SetTime(ADENV_SEG_DECAY, 0.4f);
  filt.Init(samplerate);
  filt.SetRes(filterresonance); // filter resonance

  lfo.Init(samplerate);
  lfo.SetFreq(lfofreq);
  lfo.SetWaveform(Oscillator::WAVE_TRI);

  analogReadResolution(AD_BITS);
  stateStore.begin(STATE_KEY,1);SavedState saved;
  if(stateStore.load(&saved,sizeof(saved))&&saved.waveform>=Oscillator::WAVE_TRI&&saved.waveform<=Oscillator::WAVE_POLYBLEP_TRI&&isfinite(saved.filterfreq)&&saved.filterfreq>=20&&saved.filterfreq<=2500&&isfinite(saved.filterresonance)&&saved.filterresonance>=0&&saved.filterresonance<=0.95){waveform=saved.waveform;lfoWaveform=saved.lfoWaveform;for(uint8_t i=0;i<OSCSPERVOICE;++i){minfreq[i]=saved.minfreq[i];osc[i].SetWaveform(waveform);}filterfreq=saved.filterfreq;envelopefiltermod=saved.envelopefiltermod;filterresonance=saved.filterresonance;lfofreq=saved.lfofreq;lfofiltermod=saved.lfofiltermod;adsrAttack=saved.attack;adsrDecay=saved.decay;adsrSustain=saved.sustain;adsrRelease=saved.release;filt.SetRes(filterresonance);lfo.SetFreq(lfofreq);lfo.SetWaveform(lfoWaveform);env.SetTime(ADSR_SEG_ATTACK,adsrAttack);env.SetTime(ADSR_SEG_DECAY,adsrDecay);env.SetSustainLevel(adsrSustain);env.SetTime(ADSR_SEG_RELEASE,adsrRelease);lockpots();}

  analogReadResolution(AD_BITS); // set up for max resolution

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
    if (((millis()-buttontimer) > DEBOUNCE) && !button) {  // if button pressed advance to next parameter set
      button=1;  
      save_buttonpress=millis();
      save_hold_handled=0;
      ++UIstate;
      if (UIstate >= NUMUISTATES) UIstate=OSCS;
      lockpots();
    }
  }
  else {
    buttontimer=millis();
    button=0;
    save_hold_handled=0;
  }

  if(button&&!save_hold_handled&&((millis()-save_buttonpress)>=MANUAL_SAVE_HOLD_MS)){
    save_hold_handled=1;
    SavedState saved=captureState();
    blinkSaveResult(stateStore.save(&saved,sizeof(saved)));
  }

  if ((millis() -parameterupdate) > PARAMETERUPDATE) {  // don't update the parameters too often -sometimes it messes up the daisySP models
    parameterupdate=millis();
    samplepots();

// set synth parameters from panel pots
// 
    switch (UIstate) {
        case OSCS:
          LEDS.setPixelColor(0, RED); 

          if (!potlock[0]) minfreq[0]=(mapf(pot[0],0,AD_RANGE-1,20,160)); // 4 octave range
          if (!potlock[1]) minfreq[1]=(mapf(pot[1],0,AD_RANGE-1,20,160)); // 4 octave range
          if (!potlock[2]) minfreq[2]=(mapf(pot[2],0,AD_RANGE-1,20,160)); // 4 octave range
          if (!potlock[3]) {
            waveform=(map(pot[3],0,AD_RANGE-1,Oscillator::WAVE_TRI,Oscillator::WAVE_POLYBLEP_TRI));
            for (int16_t i=0;i< OSCSPERVOICE;++i) osc[i].SetWaveform(waveform);
          }
          break;
        
        case FILTER:
          LEDS.setPixelColor(0, ORANGE);    
          if (!potlock[0]) filterfreq=(mapf(pot[0],0,AD_RANGE-1,20,2500)); //    
          if (!potlock[1]) {filterresonance=mapf(pot[1],0,AD_RANGE-1,0,0.95);filt.SetRes(filterresonance);}
          if (!potlock[2]) envelopefiltermod=(mapf(pot[2],0,AD_RANGE-1,0,1.0));        
          if (!potlock[3]) lfofiltermod=(mapf(pot[3],0,AD_RANGE-1,0,1.0));
          break;
        case ADSR:
          LEDS.setPixelColor(0, GREEN);
          if (!potlock[0]) {adsrAttack=mapf(pot[0],0,AD_RANGE-1,0,2);env.SetTime(ADSR_SEG_ATTACK,adsrAttack);}
          if (!potlock[1]) {adsrDecay=mapf(pot[1],0,AD_RANGE-1,0,2);env.SetTime(ADSR_SEG_DECAY,adsrDecay);}
          if (!potlock[2]) {adsrSustain=mapf(pot[2],0,AD_RANGE-1,0,1);env.SetSustainLevel(adsrSustain);}
          if (!potlock[3]) {adsrRelease=mapf(pot[3],0,AD_RANGE-1,0,2);env.SetTime(ADSR_SEG_RELEASE,adsrRelease);}
          break; 
        case LFO:
          LEDS.setPixelColor(0, BLUE);
          if (!potlock[2]) {lfofreq=mapf(pot[2],0,AD_RANGE-1,0.1,10);lfo.SetFreq(lfofreq);}
          if (!potlock[3]) {lfoWaveform=map(pot[3],0,AD_RANGE-1,Oscillator::WAVE_TRI,Oscillator::WAVE_POLYBLEP_TRI);lfo.SetWaveform(lfoWaveform);}
          break;

    }
  }

  float cv=(AD_RANGE-sampleCV2()); // CV in is inverted. this number will always be at least 1 so we don't divide by zero below

  osc[0].SetFreq(pow(2,(cv/CV_VOLT))*minfreq[0]); // ~ 7 octave range
  osc[1].SetFreq(pow(2,(cv/CV_VOLT))*minfreq[1]);
  osc[2].SetFreq(pow(2,(cv/CV_VOLT))*minfreq[2]);

  if (!digitalRead(GATE)) {  // if gate input is active, tell core 1 to process ADSR
    if (((millis()-gatetimer) > GATE_DEBOUNCE) && !gate) {  
      gate=1;  
    }
  }
  else {
    gatetimer=millis();
    gate=0;
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

static  float freq,out,sig,filtsig,envelope,outsig,wetvl, wetvr,lfomod;
static  int32_t outsample;

  sig=0;
  for (int j=0; j < OSCSPERVOICE; ++j) {
    sig+=osc[j].Process(); // sum oscillators in each voice
  }
//  sig=sig/OSCSPERVOICE; // scale down by number of oscillators
  sig=sig/2;
  envelope=env.Process(gate);
  lfomod=lfo.Process();

  filt.SetFreq(constrain(filterfreq+envelope*filtersweep*envelopefiltermod+filtersweep*lfofiltermod*lfomod,1,SAMPLERATE/2));
//  filt.SetFreq(200+envelope*3000*(lfo.Process()+1));
  sig=filt.Process(sig);

  sig=sig*envelope;   // VCA
  out=sig;

  outsample = (int32_t)(out*MULT_16)>>16; // scaling 

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
