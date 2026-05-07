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
// 16 step sequencer for 2HP hardware Dec 2025
// module must be jumpered for CV out on jack 2 which is used as Gate output
// uses Adafruit NeoPixel library
// V1.0 RH Dec 2025

// top jack - trigger input
// middle jack - gate output
// bottom jack - pitch CV output

// page 1 parameters - Red LED
// Pot 1 - step 1 pitch
// pot 2 - step 2 pitch
// pot 3 - step 3 pitch
// pot 4 - step 4 pitch

// page 2 parameters - Violet LED
// Pot 1 - step 5 pitch
// pot 2 - step 6 pitch
// pot 3 - step 7 pitch
// pot 4 - step 8 pitch

// page 3 parameters - Blue LED
// Pot 1 - step 9 pitch
// pot 2 - step 10 pitch
// pot 3 - step 11 pitch
// pot 4 - step 12 pitch

// page 4 parameters - Aqua LED
// Pot 1 - step 13 pitch
// pot 2 - step 14 pitch
// pot 3 - step 15 pitch
// pot 4 - step 16 pitch

// page 5 parameters Green LED
// Pot 1 - scale
// Pot 2 - clock divider
// Pot 3 - number of steps
// Pot 4 - overall pitch

//#include "MIDI.h"
#include <2HPico.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
//#include "RPi_Pico_TimerInterrupt.h"
#include <math.h>
#include <string.h>

#include "pico/multicore.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

//#define SAMPLERATE 11025
#define SAMPLERATE 22050  // saves CPU cycles on RP2350
//#define SAMPLERATE 44100

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S DAC(OUTPUT);  // 


// sequencer stuff
#define MAX_STEPS 16
#define MAX_SCALES 4   // there are more scales than this but works best if you only use a few
#define NOTERANGE 36  // 3 octave range seems reasonable
#define CLOCKIN TRIGGER  // top jack is clock
#define MAX_RATCHET 8


#define CVIN_VOLT 580.6  // a/d count per volt - **** adjust this value to calibrate V/octave input
#define CVOUT_VOLT 5456 // D/A count per volt - nominally +-5v range for -+32767 DAC values- ***** adjust this value to calibrate V/octave out
#define CVOUTMIN -2*CVOUT_VOLT  // lowest output CV ie MIDI note 0

int8_t notes[MAX_STEPS]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
uint8_t ratchets[MAX_STEPS]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
int16_t gateout=GATELOW;      // sent to right dac channel
int16_t cvout=CVOUTMIN;  // sent to left DAC channel
int16_t cvoffset=12000; // base pitch set by UI
int8_t stepindex=0;
int8_t laststep=15;
int16_t scale=0;

int8_t clockdivideby=1;  // divide input clock by this
int8_t clockdivider=1;   // counts down clocks
bool clockForceFirstStep=1; // after reset/idle, first valid clock always advances one step

bool clocked=0;  // keeps track of clock state
bool clockidle=0;  // true after clock timeout reset
bool button=0;  // keeps track of button state
bool ratchet_active=0;
bool ratchet_editing=0;
bool ratchet_mode_prev=0;
uint8_t ratchet_count=1;
uint8_t ratchet_index=0;
uint32_t ratchet_interval=0;
uint32_t ratchet_next_time=0;
uint32_t pagecolor=RED;
uint16_t ratchet_pot_last[NUMPOTS]={0,0,0,0};

#define NUMUISTATES 5
enum UIstates {SET1,SET2,SET3,SET4,SET5} ;
uint8_t UIstate=SET1;
uint32_t buttontimer,buttonpress,clocktimer,clockperiod,clockdebouncetimer,ledtimer, gatetimer, gatelength;

#define LEDOFF 100 // LED trigger flash time 
#define CLOCK_RESET_MS 1000  // reset to step 1 after 1s without clock
#define LONG_PRESS_MS 500  // hold time to enter ratchet edit mode
#define RATCHET_POT_THRESHOLD 150  // pot delta required to accept ratchet change
#define EEPROM_BYTES 256
#define SEQ_STORE_MAGIC 0x32535051u // "2SPQ"
#define SEQ_STORE_VERSION 1u
#define SAVE_DEBOUNCE_MS 5000 // minimum time between flash saves

bool sequenceDirty=0;
uint32_t sequenceLastChange=0;
uint32_t sequenceShadowHash=0;

struct SequencerStore {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  int8_t notes[MAX_STEPS];
  uint8_t ratchets[MAX_STEPS];
  int8_t laststep;
  int8_t scale;
  int8_t clockdivideby;
  int8_t reserved2;
  int16_t cvoffset;
  uint32_t checksum;
};

static uint32_t sequencerChecksum(const SequencerStore &data) {
  uint32_t hash = 2166136261u; // FNV-1a 32-bit
  const uint8_t *raw = reinterpret_cast<const uint8_t*>(&data);
  for (uint32_t i = 0; i < (uint32_t)(sizeof(SequencerStore) - sizeof(uint32_t)); ++i) {
    hash ^= raw[i];
    hash *= 16777619u;
  }
  return hash;
}

static void copyStateToStore(SequencerStore &data) {
  memset(&data, 0, sizeof(data));
  data.magic = SEQ_STORE_MAGIC;
  data.version = SEQ_STORE_VERSION;
  data.reserved = 0;
  for (int i=0; i<MAX_STEPS; ++i) {
    data.notes[i]=notes[i];
    data.ratchets[i]=ratchets[i];
  }
  data.laststep=laststep;
  data.scale=scale;
  data.clockdivideby=clockdivideby;
  data.reserved2=0;
  data.cvoffset=cvoffset;
}

static bool validateStore(const SequencerStore &data) {
  if (data.magic != SEQ_STORE_MAGIC) return 0;
  if (data.version != SEQ_STORE_VERSION) return 0;
  if (data.checksum != sequencerChecksum(data)) return 0;
  if (data.laststep < 0 || data.laststep >= MAX_STEPS) return 0;
  if (data.scale < 0 || data.scale > MAX_SCALES) return 0;
  if (data.clockdivideby < 1 || data.clockdivideby > 8) return 0;
  if (data.cvoffset < CVOUTMIN || data.cvoffset > 32767) return 0;
  for (int i=0; i<MAX_STEPS; ++i) {
    if (data.notes[i] < -1 || data.notes[i] > NOTERANGE) return 0;
    if (data.ratchets[i] < 1 || data.ratchets[i] > MAX_RATCHET) return 0;
  }
  return 1;
}

static bool loadSequenceFromFlash() {
  SequencerStore data;
  EEPROM.get(0, data);
  if (!validateStore(data)) return 0;

  for (int i=0; i<MAX_STEPS; ++i) {
    notes[i]=data.notes[i];
    ratchets[i]=data.ratchets[i];
  }
  laststep=data.laststep;
  scale=data.scale;
  clockdivideby=data.clockdivideby;
  clockdivider=clockdivideby;
  cvoffset=data.cvoffset;
  return 1;
}

static bool saveSequenceToFlash() {
  SequencerStore data;
  copyStateToStore(data);
  data.checksum=sequencerChecksum(data);
  EEPROM.put(0, data);
  return EEPROM.commit();
}

static uint32_t currentSequenceHash() {
  SequencerStore data;
  copyStateToStore(data);
  data.checksum=0;
  return sequencerChecksum(data);
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

  pinMode(TRIGGER,INPUT_PULLUP); // gate/trigger in
  pinMode(BUTTON1,INPUT_PULLUP); // button in
  pinMode(MUXCTL,OUTPUT);  // analog switch mux

  LEDS.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  LEDS.setPixelColor(0, RED); 
  LEDS.show();

  analogReadResolution(AD_BITS); // set up for max resolution

  // Prime pot[] with the current physical knob positions before lockpots().
  // Without this, loaded sequence data can be overwritten on first loop.
  samplepots();

  EEPROM.begin(EEPROM_BYTES);
  bool loaded=loadSequenceFromFlash();
#ifdef DEBUG
  if (loaded) Serial.println("loaded sequencer data from flash");
  else Serial.println("using default sequencer data");
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

#ifdef DEBUG  
  Serial.println("finished setup");  
#endif
  clocktimer=millis(); // initial clock measurement
  clockperiod=CLOCK_RESET_MS;
  clockForceFirstStep=1;
  sequenceShadowHash=currentSequenceHash();
  sequenceDirty=0;
  lockpots(); // keep loaded sequence until pots move significantly
}


void loop() {
// ******* still need to implement reset sequence on hold
  bool buttonraw = !digitalRead(BUTTON1);
  if (buttonraw) {
    if (((millis()-buttontimer) > DEBOUNCE) && !button) {  // button pressed
      button=1;
      buttonpress=millis();
      ratchet_editing=0;
    }
  }
  else {
    if (button) {  // button released
      uint32_t held_ms=millis()-buttonpress;
      if ((held_ms < LONG_PRESS_MS) && !ratchet_editing) {  // short press changes page
        ++UIstate;
        if (UIstate >= NUMUISTATES) UIstate=SET1;
        lockpots();
      }
      button=0;
      ratchet_editing=0;
    }
    buttontimer=millis();
  }

  samplepots();

  bool ratchet_mode = button && ((millis()-buttonpress) >= LONG_PRESS_MS) && (UIstate != SET5);
  if (ratchet_mode && !ratchet_mode_prev) {
    for (int i=0; i<NUMPOTS; ++i) ratchet_pot_last[i]=pot[i];
  }
  ratchet_mode_prev = ratchet_mode;

// set parameters from panel pots
// 
  switch (UIstate) {
    case SET1:
      pagecolor=RED;
      LEDS.setPixelColor(0, pagecolor);  // set sequencer step values from pot settings
      if (ratchet_mode) {
        if (!potlock[0]) {
          int delta=abs((int)pot[0]-(int)ratchet_pot_last[0]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[0],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[0] != val) { ratchets[0]=val; ratchet_editing=1; }
            ratchet_pot_last[0]=pot[0];
          }
        }
        if (!potlock[1]) {
          int delta=abs((int)pot[1]-(int)ratchet_pot_last[1]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[1],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[1] != val) { ratchets[1]=val; ratchet_editing=1; }
            ratchet_pot_last[1]=pot[1];
          }
        }
        if (!potlock[2]) {
          int delta=abs((int)pot[2]-(int)ratchet_pot_last[2]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[2],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[2] != val) { ratchets[2]=val; ratchet_editing=1; }
            ratchet_pot_last[2]=pot[2];
          }
        }
        if (!potlock[3]) {
          int delta=abs((int)pot[3]-(int)ratchet_pot_last[3]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[3],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[3] != val) { ratchets[3]=val; ratchet_editing=1; }
            ratchet_pot_last[3]=pot[3];
          }
        }
      }
      else {
        if (!potlock[0]) notes[0]=map(pot[0],0,AD_RANGE-1,-1,NOTERANGE); // top pot on the panel
        if (!potlock[1]) notes[1]=map(pot[1],0,AD_RANGE-1,-1,NOTERANGE);  // -1 is note off
        if (!potlock[2]) notes[2]=map(pot[2],0,AD_RANGE-1,-1,NOTERANGE);
        if (!potlock[3]) notes[3]=map(pot[3],0,AD_RANGE-1,-1,NOTERANGE);
      }
      break;
    case SET2:
      pagecolor=VIOLET;
      LEDS.setPixelColor(0, pagecolor);
      if (ratchet_mode) {
        if (!potlock[0]) {
          int delta=abs((int)pot[0]-(int)ratchet_pot_last[0]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[0],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[4] != val) { ratchets[4]=val; ratchet_editing=1; }
            ratchet_pot_last[0]=pot[0];
          }
        }
        if (!potlock[1]) {
          int delta=abs((int)pot[1]-(int)ratchet_pot_last[1]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[1],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[5] != val) { ratchets[5]=val; ratchet_editing=1; }
            ratchet_pot_last[1]=pot[1];
          }
        }
        if (!potlock[2]) {
          int delta=abs((int)pot[2]-(int)ratchet_pot_last[2]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[2],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[6] != val) { ratchets[6]=val; ratchet_editing=1; }
            ratchet_pot_last[2]=pot[2];
          }
        }
        if (!potlock[3]) {
          int delta=abs((int)pot[3]-(int)ratchet_pot_last[3]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[3],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[7] != val) { ratchets[7]=val; ratchet_editing=1; }
            ratchet_pot_last[3]=pot[3];
          }
        }
      }
      else {
        if (!potlock[0]) notes[4]=map(pot[0],0,AD_RANGE-1,-1,NOTERANGE); // top pot on the panel
        if (!potlock[1]) notes[5]=map(pot[1],0,AD_RANGE-1,-1,NOTERANGE);  //
        if (!potlock[2]) notes[6]=map(pot[2],0,AD_RANGE-1,-1,NOTERANGE);
        if (!potlock[3]) notes[7]=map(pot[3],0,AD_RANGE-1,-1,NOTERANGE);
      }
      break;
    case SET3:
      pagecolor=BLUE;
      LEDS.setPixelColor(0, pagecolor);
      if (ratchet_mode) {
        if (!potlock[0]) {
          int delta=abs((int)pot[0]-(int)ratchet_pot_last[0]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[0],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[8] != val) { ratchets[8]=val; ratchet_editing=1; }
            ratchet_pot_last[0]=pot[0];
          }
        }
        if (!potlock[1]) {
          int delta=abs((int)pot[1]-(int)ratchet_pot_last[1]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[1],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[9] != val) { ratchets[9]=val; ratchet_editing=1; }
            ratchet_pot_last[1]=pot[1];
          }
        }
        if (!potlock[2]) {
          int delta=abs((int)pot[2]-(int)ratchet_pot_last[2]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[2],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[10] != val) { ratchets[10]=val; ratchet_editing=1; }
            ratchet_pot_last[2]=pot[2];
          }
        }
        if (!potlock[3]) {
          int delta=abs((int)pot[3]-(int)ratchet_pot_last[3]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[3],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[11] != val) { ratchets[11]=val; ratchet_editing=1; }
            ratchet_pot_last[3]=pot[3];
          }
        }
      }
      else {
        if (!potlock[0]) notes[8]=map(pot[0],0,AD_RANGE-1,-1,NOTERANGE); // top pot on the panel
        if (!potlock[1]) notes[9]=map(pot[1],0,AD_RANGE-1,-1,NOTERANGE);  //
        if (!potlock[2]) notes[10]=map(pot[2],0,AD_RANGE-1,-1,NOTERANGE);
        if (!potlock[3]) notes[11]=map(pot[3],0,AD_RANGE-1,-1,NOTERANGE);
      }
      break;
    case SET4:
      pagecolor=AQUA;
      LEDS.setPixelColor(0, pagecolor);
      if (ratchet_mode) {
        if (!potlock[0]) {
          int delta=abs((int)pot[0]-(int)ratchet_pot_last[0]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[0],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[12] != val) { ratchets[12]=val; ratchet_editing=1; }
            ratchet_pot_last[0]=pot[0];
          }
        }
        if (!potlock[1]) {
          int delta=abs((int)pot[1]-(int)ratchet_pot_last[1]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[1],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[13] != val) { ratchets[13]=val; ratchet_editing=1; }
            ratchet_pot_last[1]=pot[1];
          }
        }
        if (!potlock[2]) {
          int delta=abs((int)pot[2]-(int)ratchet_pot_last[2]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[2],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[14] != val) { ratchets[14]=val; ratchet_editing=1; }
            ratchet_pot_last[2]=pot[2];
          }
        }
        if (!potlock[3]) {
          int delta=abs((int)pot[3]-(int)ratchet_pot_last[3]);
          if (delta >= RATCHET_POT_THRESHOLD) {
            uint8_t val=map(pot[3],0,AD_RANGE-1,1,MAX_RATCHET);
            if (val < 1) val=1;
            if (val > MAX_RATCHET) val=MAX_RATCHET;
            if (ratchets[15] != val) { ratchets[15]=val; ratchet_editing=1; }
            ratchet_pot_last[3]=pot[3];
          }
        }
      }
      else {
        if (!potlock[0]) notes[12]=map(pot[0],0,AD_RANGE-1,-1,NOTERANGE); // top pot on the panel
        if (!potlock[1]) notes[13]=map(pot[1],0,AD_RANGE-1,-1,NOTERANGE);  //
        if (!potlock[2]) notes[14]=map(pot[2],0,AD_RANGE-1,-1,NOTERANGE);
        if (!potlock[3]) notes[15]=map(pot[3],0,AD_RANGE-1,-1,NOTERANGE);
      }
      break;
    case SET5:
      pagecolor=GREEN;
      LEDS.setPixelColor(0, pagecolor);
      if (!potlock[0]) scale=map(pot[0],0,AD_RANGE,0,MAX_SCALES);  // set scale - too many scales gets hard to discern by ear
      if (!potlock[1]) clockdivideby=map(pot[1],0,AD_RANGE-1,1,8); // set clock divider
      if (!potlock[2]) {
        int16_t steps=(pot[2]*MAX_STEPS + (AD_RANGE/2)) / AD_RANGE; // round to reach full range
        if (steps < 1) steps=1;
        if (steps > MAX_STEPS) steps=MAX_STEPS;
        laststep=steps-1; // set number of steps
      }
      if (!potlock[3]) cvoffset=map(pot[3],0,AD_RANGE-1,CVOUTMIN,32767); // sets overall pitch
      break;
    default:
      break;
  }

  if (button) {
    LEDS.setPixelColor(0, pagecolor);
    LEDS.show();
  }

  if (!digitalRead(CLOCKIN)) {  // look for rising edge of clock input which is inverted
    if (((millis()-clockdebouncetimer) > CLOCK_DEBOUNCE) && !clocked) {  // true if we have a debounced clock rising edge
      bool advanceStep=0;
      if (clockForceFirstStep) {
        clockForceFirstStep=0;
        clockdivider=clockdivideby; // restart divider window after forced first step
        advanceStep=1;
      } else {
        --clockdivider;
        if (clockdivider <=0) {
          clockdivider=clockdivideby;
          advanceStep=1;
        }
      }
      clocked=1;
      if (advanceStep) {
        uint32_t now=millis();
        if (!clockidle) clockperiod=now-clocktimer; // measure clock so we can set gate time relative to clock period
        clocktimer=now;
        clockidle=0;

        uint8_t rcount=ratchets[stepindex];
        if (rcount < 1) rcount=1;
        ratchet_count=rcount;
        ratchet_index=0;
        ratchet_active=0;
        ratchet_interval=clockperiod/ratchet_count;
        if (ratchet_interval < 1) ratchet_interval=1;

        if (notes[stepindex]>=0) {  // negative note value is silent so don't change CV
          cvout=-(quantize(notes[stepindex],scales[scale],0)*(CVOUT_VOLT/12)+CVOUTMIN+cvoffset); // 1v per octave. note numbers are MIDI style 0-127. DAC out is inverted. 
          gateout=GATEHIGH;
          if (ratchet_count > 1) {
            gatelength=ratchet_interval/2;
            if (gatelength < 1) gatelength=1;
            ratchet_active=1;
            ratchet_index=1;
            ratchet_next_time=now+ratchet_interval;
          }
          else gatelength=clockperiod/2; // could be made adjustable
          gatetimer=now; // start gate timer
 //         Serial.printf("scale %s note %s \n",scalenames[scale],notenames[notes[stepindex]%12]);
        }
        else gateout=GATELOW;
        if (!button) {
          uint32_t stepcolor = (stepindex == 0) ? YELLOW : 0;
          LEDS.setPixelColor(0, stepcolor); // blink each step, orange on step 1
          LEDS.show();  // update LED
          ledtimer=millis(); // start led flash timer
        }
        ++stepindex; // advance sequencer  could add sequencer modes - pingpong, reverse etc
        if (stepindex > laststep) stepindex=0;
      }
    }
  }
  else {   
      clocked=0;
      clockdebouncetimer=millis();
  }
//Serial.printf("clkdiv %d len %d \n", clockdivideby,laststep);

  if (!clockidle && (millis()-clocktimer) > CLOCK_RESET_MS) {
    stepindex=0;
    clockdivider=clockdivideby;
    clockForceFirstStep=1; // first pulse after idle reset always starts sequence
    gateout=GATELOW;
    clockidle=1;
    ratchet_active=0;
  }

  if (ratchet_active) {
    uint32_t now=millis();
    if ((int32_t)(now-ratchet_next_time) >= 0) {
      gateout=GATEHIGH;
      gatetimer=now;
      gatelength=ratchet_interval/2;
      if (gatelength < 1) gatelength=1;
      ++ratchet_index;
      if (ratchet_index >= ratchet_count) ratchet_active=0;
      else ratchet_next_time+=ratchet_interval;
    }
  }

  if ((millis()-gatetimer) > gatelength) gateout=GATELOW;  // turn off gate after gate length

  if (!button && (millis()-ledtimer) > LEDOFF ) LEDS.show();  // update LEDs only if not doing off flash

  uint32_t currentHash=currentSequenceHash();
  if (currentHash != sequenceShadowHash) {
    sequenceShadowHash=currentHash;
    sequenceDirty=1;
    sequenceLastChange=millis();
  }

  if (sequenceDirty && ((millis()-sequenceLastChange) >= SAVE_DEBOUNCE_MS)) {
    bool saved=saveSequenceToFlash();
    if (saved) {
      sequenceDirty=0;
#ifdef DEBUG
      Serial.println("saved sequencer data to flash");
#endif
    }
#ifdef DEBUG
    if (!saved) Serial.println("warning: failed to save sequencer data");
#endif
  }
}

// second core setup
// second core is dedicated to sample processing
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

// process audio samples
void loop1(){

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE,0); // low - CPU not busy
#endif
 // write values to DMA buffer - this is a blocking call so it stalls when buffer is full
	DAC.write(int16_t(cvout)); // left
	DAC.write(int16_t(gateout)); // right

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE,1); // hi = CPU busy
#endif
}
