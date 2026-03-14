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
// Modal voice for 2HPico Eurorack module 
// uses Arduino DaisySP library https://github.com/rheslip/DaisySP_Teensy
// also uses Adafruit NeoPixel library
// V1.0 RH Nov 2025

// top jack - trigger input
// middle jack - pitch CV input
// bottom jack - audio out

// page 1 parameters - Red LED
// Pot 1 - structure
// pot 2 - damping
// pot 3 - brightness
// pot 4 - frequency

// page 2 parameters - Green LED
// Pot 1 - accent
// pot 2 - sustain - rotate past 12 o'clock for sustained output
// pot 3 - unused
// pot 4 - unused

// probably needs 200-250mhz overclock to run the modal model under all conditions

#include <2HPico.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code

#define MONITOR_CPU1  // define to enable 2nd core monitoring

//#define SAMPLERATE 11025 
//#define SAMPLERATE 22050 // about the best a Pico 2 can do at 250 Mhz
#define SAMPLERATE 32000
//#define SAMPLERATE 44100

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S DAC(OUTPUT);  // using once channel of a PT8211 stereo DAC


#include "daisysp.h"

// including the source files is a pain but that way you compile in only the modules you need
// DaisySP statically allocates memory and some modules e.g. reverb use a lot of ram
#include "physicalmodeling/resonator.cpp"
#include "physicalmodeling/modalvoice.cpp"

float samplerate=SAMPLERATE;  // for daisySP

// create daisySP processing objects

#define VOICES 1   // RP2350 can't quite do 2 voices at 250mHz. my boards won't run 300mHz

daisysp::ModalVoice voice;


#define CVIN_VOLT 580.6  // a/d count per volt - **** adjust this value to calibrate V/octave input
#define CVOUT_VOLT 6554 // D/A count per volt - nominally +-5v range for -+32767 DAC values- ***** adjust this value to calibrate V/octave out
#define CVOUTMIN -2*CVOUT_VOLT  // lowest output CV ie MIDI note 0

float minfreq=10;

bool trigger=0;
bool button=0;
#define NUMUISTATES 2
enum UIstates {SET1,SET2} ;
uint8_t UIstate=SET1;
uint32_t buttontimer,trigtimer,parameterupdate;

void setup() { 
  Serial.begin(115200);

#ifdef DEBUG

  Serial.println("starting setup");  
#endif

// set up I/O pins
  pinMode(TRIGGER,INPUT_PULLUP); // gate/trigger in
  pinMode(BUTTON1,INPUT_PULLUP); // button in
  pinMode(MUXCTL,OUTPUT);  // analog switch mux

#ifdef MONITOR_CPU1 // for monitoring 2nd core CPU usage
  pinMode(CPU_USE,OUTPUT); // hi = CPU busy
#endif 

  voice.Init(samplerate);       // initialize the voice object
  voice.SetFreq(150);

  analogReadResolution(AD_BITS); // set up for max resolution

  LEDS.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  LEDS.setPixelColor(0, RED); 
  LEDS.show();
    
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
      ++UIstate;
      if (UIstate >= NUMUISTATES) UIstate=SET1;
      lockpots();
    }
  }
  else {
    buttontimer=millis();
    button=0;
  }

  if ((millis() -parameterupdate) > PARAMETERUPDATE) {  // don't update the parameters too often -it messes up the daisySP model
    parameterupdate=millis();
    samplepots();

  // set parameters from panel pots
  // 
    switch (UIstate) {
      case SET1:
        LEDS.setPixelColor(0, RED); 
        if (!potlock[0]) voice.SetStructure(mapf(pot[0],0,AD_RANGE-1,0,1));// top pot on the panel
        if (!potlock[1]) voice.SetBrightness(mapf(pot[1],0,AD_RANGE-1,0,1));  //
        if (!potlock[2]) voice.SetDamping(mapf(pot[2],0,AD_RANGE-1,0,1));
        if (!potlock[3]) minfreq=(mapf(pot[3],0,AD_RANGE-1,10,100)); // 
        break;
      case SET2:
        LEDS.setPixelColor(0, GREEN);       
        if (!potlock[0]) voice.SetAccent(mapf(pot[0],0,AD_RANGE-1,0,1));
        if (!potlock[1]) voice.SetSustain((bool)map(pot[1],0,AD_RANGE-1,0,2)); // sustain = pot over half way
        break;
      default:
        break;
    }
  }

  float cv=(AD_RANGE-sampleCV2()); // CV in is inverted

  voice.SetFreq(pow(2,(cv/CVIN_VOLT))*minfreq); // ~ 7 octave range

  if (!digitalRead(TRIGGER)) {
    if (((millis()-trigtimer) > TRIG_DEBOUNCE) && !trigger) {  // trigger detection
      trigger=1;  
      voice.Trig();
    }
  }
  else {
    trigtimer=millis();
    trigger=0;
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

static  float sig;
static  int32_t outsample;


  sig=voice.Process();
  outsample = (int32_t)(sig*MULT_16)>>16; // scale output 

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE,0); // low - core1 not busy
#endif
 // write samples to DMA buffer - this is a blocking call so it stalls when buffer is full
	DAC.write(int16_t(outsample)); // left
	DAC.write(int16_t(outsample)); // right

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE,1); // hi = core1 busy
#endif
}





