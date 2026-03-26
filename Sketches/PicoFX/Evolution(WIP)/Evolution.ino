// Copyright 2026 Rich Heslip / OpenAI
//
// Evolution example for 2HPico DSP hardware
// Generated for the 2HPico PicoFX sketch family.
//
// Top Jack - Audio input
// Middle jack - Audio out mirror if the DAC jumper is fitted
// Bottom Jack - Processed audio out
//
// Top pot - Direction 1: temporal drift
// Second pot - Direction 2: diffusion cloud
// Third pot - Direction 3: spectral bloom
// Fourth pot - Direction 4: regenerative memory
//
// Pot behavior:
// - centered: hold the current amount of evolution
// - clockwise: evolve further along that direction
// - counterclockwise: decay back toward the original seed buffer
//
// Button behavior:
// - press once to capture the current rolling input buffer as a frozen seed
// - the model continues evolving from that frozen seed until another capture
//
// The audio core keeps a rolling live buffer from Jack 1, and only switches to a
// frozen buffer after a staged snapshot copy completes. This avoids a large copy
// burst in loop1() that could cause dropouts.

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>

#include "pico/multicore.h"
#include "evolution_model.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

#define SAMPLERATE 32000  // reduce to 22050 if you add heavier processing

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

I2S i2s(INPUT_PULLUP); // both input and output

EvolutionModel evolution;

struct SharedControls {
  volatile uint32_t revision;
  volatile float direction[4];
  volatile uint32_t snapshot_request;
};

struct SharedStatus {
  volatile uint32_t revision;
  volatile uint8_t seed_locked;
  volatile uint8_t snapshot_busy;
};

SharedControls controls = {0, {0.0f, 0.0f, 0.0f, 0.0f}, 0};
SharedStatus status = {0, 0, 0};

bool button = 0;
uint32_t buttontimer = 0;
uint32_t parameterupdate = 0;
uint32_t snapshotrequest = 0;

float ApplyDeadband(float value, float deadband) {
  if (value > deadband) {
    return (value - deadband) / (1.0f - deadband);
  }

  if (value < -deadband) {
    return (value + deadband) / (1.0f - deadband);
  }

  return 0.0f;
}

float MapDirectionPot(uint16_t value) {
  float normalized = mapf(value, 0, AD_RANGE - 1, -1.0f, 1.0f);
  return ApplyDeadband(normalized, 0.06f);
}

void PublishControls(float d0, float d1, float d2, float d3) {
  ++controls.revision; // odd means update in progress
  controls.direction[0] = d0;
  controls.direction[1] = d1;
  controls.direction[2] = d2;
  controls.direction[3] = d3;
  controls.snapshot_request = snapshotrequest;
  ++controls.revision; // even means stable
}

void PublishStatus(bool seed_locked, bool snapshot_busy) {
  ++status.revision; // odd means update in progress
  status.seed_locked = seed_locked ? 1u : 0u;
  status.snapshot_busy = snapshot_busy ? 1u : 0u;
  ++status.revision; // even means stable
}

void UpdateLed(bool seed_locked, bool snapshot_busy) {
  uint32_t color = RED;

  if (snapshot_busy) {
    color = ORANGE;
  } else if (seed_locked) {
    color = WHITE;
  }

  LEDS.setPixelColor(0, color);
  LEDS.show();
}

void setup() {
  Serial.begin(115200);

#ifdef DEBUG
  Serial.println("starting setup");
#endif

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT); // hi = CPU busy
#endif

  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

  LEDS.begin();
  LEDS.setPixelColor(0, RED);
  LEDS.show();

  analogReadResolution(AD_BITS);
  samplepots();

  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK); // Note: LRCLK = BCLK + 1
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();

  PublishControls(0.0f, 0.0f, 0.0f, 0.0f);
  PublishStatus(false, false);

#ifdef DEBUG
  Serial.println("finished setup");
#endif
}

void loop() {
  uint32_t now = millis();
  uint32_t statusrevision;
  bool seed_locked;
  bool snapshot_busy;

  if (!digitalRead(BUTTON1)) {
    if (((now - buttontimer) > DEBOUNCE) && !button) {
      button = 1;
      ++snapshotrequest;
    }
  } else {
    buttontimer = now;
    button = 0;
  }

  if ((now - parameterupdate) > PARAMETERUPDATE) {
    parameterupdate = now;
    samplepots();

    float d0 = controls.direction[0];
    float d1 = controls.direction[1];
    float d2 = controls.direction[2];
    float d3 = controls.direction[3];

    if (!potlock[0]) d0 = MapDirectionPot(pot[0]);
    if (!potlock[1]) d1 = MapDirectionPot(pot[1]);
    if (!potlock[2]) d2 = MapDirectionPot(pot[2]);
    if (!potlock[3]) d3 = MapDirectionPot(pot[3]);

    PublishControls(d0, d1, d2, d3);
  }

  do {
    statusrevision = status.revision;
    while (statusrevision & 1u) statusrevision = status.revision;
    seed_locked = status.seed_locked != 0;
    snapshot_busy = status.snapshot_busy != 0;
  } while (statusrevision != status.revision);

  UpdateLed(seed_locked, snapshot_busy);
}

void setup1() {
  delay(1000);
  evolution.Init((float)SAMPLERATE);
}

void loop1() {
  int32_t left;
  int32_t right;
  int16_t processed;
  int32_t out32;
  uint32_t controlrevision;
  float direction[4];
  uint32_t request_id;
  static uint32_t last_request_id = 0;
  static uint8_t status_divider = 0;

  do {
    controlrevision = controls.revision;
    while (controlrevision & 1u) controlrevision = controls.revision;
    direction[0] = controls.direction[0];
    direction[1] = controls.direction[1];
    direction[2] = controls.direction[2];
    direction[3] = controls.direction[3];
    request_id = controls.snapshot_request;
  } while (controlrevision != controls.revision);

  if (request_id != last_request_id) {
    evolution.RequestSeedCapture();
    last_request_id = request_id;
  }

  evolution.SetDirectionControl(0, direction[0]);
  evolution.SetDirectionControl(1, direction[1]);
  evolution.SetDirectionControl(2, direction[2]);
  evolution.SetDirectionControl(3, direction[3]);

  left = i2s.read();
  right = i2s.read();
  (void)right;

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1); // hi = CPU busy
#endif

  processed = evolution.Process((int16_t)(left >> 16));
  out32 = ((int32_t)processed) << 16;

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0); // low = CPU idle
#endif

  i2s.write(out32); // left / jack 3
  i2s.write(out32); // right mirror if DAC jumper fitted

  ++status_divider;
  if (status_divider == 0) {
    PublishStatus(evolution.SeedLocked(), evolution.SnapshotBusy());
  }
}
