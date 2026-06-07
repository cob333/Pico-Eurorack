// Copyright 2026 Wenhao Yang
//
// Mutable Instruments Clouds for 2HPico PicoFX.
//
// Jack 1 - Audio input
// Jack 2 - Left audio output
// Jack 3 - Right audio output
//
// Button:
// Short click - switch parameter page
// Hold 3 seconds - toggle freeze
//
// Page 1 - Green LED
// Pot 1 - Position
// Pot 2 - Size
// Pot 3 - Density
// Pot 4 - Texture
//
// Page 2 - Yellow LED
// Pot 1 - Dry/Wet
// Pot 2 - Stereo Spread
// Pot 3 - Feedback
// Pot 4 - Reverb
//
// Page 3 - Orange LED
// Pot 1 - Pitch, -24..+24 semitones
// Pot 2 - Playback mode: granular, stretch, looping delay
// Pot 3 - Quality
// Pot 4 - Slot action while frozen

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#include "pico/multicore.h"

#define MONITOR_CPU1
#define SAMPLERATE 32000

#include "PicoClouds.h"

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S i2s(INPUT_PULLUP);

enum UIstates { PAGE1, PAGE2, PAGE3 };
constexpr uint8_t NUMUISTATES = 3;
uint8_t UIstate = PAGE1;

constexpr uint32_t kControlUpdateMs = 12;
constexpr uint32_t kSaveHoldMs = 3000;
constexpr uint32_t kFreezeBlinkMs = 260;
constexpr uint32_t kChoiceFeedbackMs = 3000;
constexpr uint16_t kDiscreteHysteresis = 96;
constexpr uint16_t kPitchCenterDeadband = 144;

bool button = false;
bool buttonhold_handled = false;
uint32_t buttontimer = 0;
uint32_t buttonpress_start = 0;
uint32_t parameterupdate = 0;
uint32_t feedback_until = 0;
uint32_t feedback_color = 0;

float position_value = pico_clouds::kDefaultPosition;
float size_value = pico_clouds::kDefaultSize;
float density_value = pico_clouds::kDefaultDensity;
float texture_value = pico_clouds::kDefaultTexture;
float dry_wet_value = pico_clouds::kDefaultDryWet;
float stereo_spread_value = pico_clouds::kDefaultStereoSpread;
float feedback_value = pico_clouds::kDefaultFeedback;
float reverb_value = pico_clouds::kDefaultReverb;
float pitch_value = pico_clouds::kDefaultPitch;
uint8_t mode_value = 0;
uint8_t quality_value = 0;
uint8_t slot_value = 0;
bool freeze_value = false;

static const uint32_t kChoiceColors[4] = {
  TIFFANY,
  AQUA,
  VIOLET,
  RED
};

static void setLedColor(uint32_t color) {
  LEDS.setPixelColor(0, color);
  LEDS.show();
}

static uint32_t pageLedColor() {
  switch (UIstate) {
    case PAGE1:
      return GREEN;
    case PAGE2:
      return YELLOW;
    case PAGE3:
    default:
      return ORANGE;
  }
}

static void showChoiceFeedback(uint8_t index) {
  feedback_color = kChoiceColors[index & 0x03];
  feedback_until = millis() + kChoiceFeedbackMs;
  setLedColor(feedback_color);
}

static void updateUiLed() {
  const uint32_t now = millis();
  if ((int32_t)(feedback_until - now) > 0) {
    setLedColor(feedback_color);
    return;
  }

  if (freeze_value) {
    const bool blue_phase = ((now / kFreezeBlinkMs) & 1u) != 0;
    setLedColor(blue_phase ? BLUE : pageLedColor());
    return;
  }

  setLedColor(pageLedColor());
}

inline uint8_t MapDiscrete(uint16_t value, uint8_t num_values) {
  uint32_t scaled = static_cast<uint32_t>(value) * num_values;
  uint8_t result = static_cast<uint8_t>(scaled / AD_RANGE);
  if (result >= num_values) {
    result = num_values - 1;
  }
  return result;
}

inline uint8_t MapDiscreteHysteretic(uint16_t value, uint8_t current, uint8_t num_values) {
  if (num_values <= 1) return 0;
  if (current >= num_values) current = num_values - 1;

  const uint32_t bin = AD_RANGE / num_values;
  const uint32_t lower = static_cast<uint32_t>(current) * bin;
  const uint32_t upper = static_cast<uint32_t>(current + 1) * bin;

  if (current > 0 && value + kDiscreteHysteresis < lower) {
    return MapDiscrete(value, num_values);
  }
  if (current + 1 < num_values && value > upper + kDiscreteHysteresis) {
    return MapDiscrete(value, num_values);
  }
  return current;
}

inline float MapPitchWithCenterDeadband(uint16_t value) {
  const int32_t center = (AD_RANGE - 1) / 2;
  const int32_t delta = static_cast<int32_t>(value) - center;
  if (abs(delta) <= kPitchCenterDeadband) {
    return 0.0f;
  }

  if (delta > 0) {
    return mapf(
        value,
        center + kPitchCenterDeadband,
        AD_RANGE - 1,
        0.0f,
        24.0f);
  }

  return mapf(
      value,
      0,
      center - kPitchCenterDeadband,
      -24.0f,
      0.0f);
}

static void pushUiState() {
  pico_clouds::SetUiState(
      position_value,
      size_value,
      density_value,
      texture_value,
      dry_wet_value,
      stereo_spread_value,
      feedback_value,
      reverb_value,
      pitch_value,
      mode_value,
      quality_value,
      slot_value,
      freeze_value);
}

static void serviceButton() {
  const bool pressed = !digitalRead(BUTTON1);
  const uint32_t now = millis();

  if (pressed != button) {
    if ((now - buttontimer) > DEBOUNCE) {
      buttontimer = now;
      button = pressed;

      if (button) {
        buttonpress_start = now;
        buttonhold_handled = false;
      } else if (!buttonhold_handled) {
        ++UIstate;
        if (UIstate >= NUMUISTATES) {
          UIstate = PAGE1;
        }
        lockpots();
        feedback_until = 0;
        updateUiLed();
      }
    }
    return;
  }

  if (button && !buttonhold_handled && ((now - buttonpress_start) >= kSaveHoldMs)) {
    freeze_value = !freeze_value;
    buttonhold_handled = true;
    pushUiState();
    updateUiLed();
  }
}

static void serviceControls() {
  if ((millis() - parameterupdate) <= kControlUpdateMs) {
    return;
  }
  parameterupdate = millis();
  samplepots();

  switch (UIstate) {
    case PAGE1:
      if (!potlock[0]) {
        position_value = mapf(pot[0], 0, AD_RANGE - 1, 0.0f, 1.0f);
      }
      if (!potlock[1]) {
        size_value = mapf(pot[1], 0, AD_RANGE - 1, 0.0f, 1.0f);
      }
      if (!potlock[2]) {
        density_value = mapf(pot[2], 0, AD_RANGE - 1, 0.0f, 1.0f);
      }
      if (!potlock[3]) {
        texture_value = mapf(pot[3], 0, AD_RANGE - 1, 0.0f, 1.0f);
      }
      break;

    case PAGE2:
      if (!potlock[0]) {
        dry_wet_value = mapf(pot[0], 0, AD_RANGE - 1, 0.0f, 1.0f);
      }
      if (!potlock[1]) {
        stereo_spread_value = mapf(pot[1], 0, AD_RANGE - 1, 0.0f, 1.0f);
      }
      if (!potlock[2]) {
        feedback_value = mapf(pot[2], 0, AD_RANGE - 1, 0.0f, 0.96f);
      }
      if (!potlock[3]) {
        reverb_value = mapf(pot[3], 0, AD_RANGE - 1, 0.0f, 1.0f);
      }
      break;

    case PAGE3:
      if (!potlock[0]) {
        pitch_value = MapPitchWithCenterDeadband(pot[0]);
      }
      if (!potlock[1]) {
        const uint8_t new_mode = MapDiscreteHysteretic(pot[1], mode_value, pico_clouds::kNumModes);
        if (new_mode != mode_value) {
          mode_value = new_mode;
          showChoiceFeedback(mode_value);
        }
      }
      if (!potlock[2]) {
        const uint8_t new_quality = MapDiscreteHysteretic(pot[2], quality_value, pico_clouds::kNumQualities);
        if (new_quality != quality_value) {
          quality_value = new_quality;
          showChoiceFeedback(quality_value);
        }
      }
      if (freeze_value && !potlock[3]) {
        const uint8_t new_slot = MapDiscreteHysteretic(pot[3], slot_value, pico_clouds::kNumSlots);
        if (new_slot != slot_value) {
          slot_value = new_slot;
          pico_clouds::SaveSlot(slot_value);
          showChoiceFeedback(slot_value);
        }
      }
      break;
  }

  pushUiState();
  updateUiLed();
}

void setup() {
  Serial.begin(115200);

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

  LEDS.begin();
  setLedColor(GREEN);

  analogReadResolution(AD_BITS);
  for (uint8_t i = 0; i < NUMPOTS; ++i) {
    pot[i] = 0;
    potlock[i] = 0;
  }

  pico_clouds::Init();
  pushUiState();

  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK);
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setBuffers(4, 128, 0);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();
}

void loop() {
  serviceButton();
  serviceControls();
  updateUiLed();
}

void setup1() {
  delay(1000);
}

void loop1() {
  static int32_t input_block[pico_clouds::kBlockSize];
  static int16_t output_l[pico_clouds::kBlockSize];
  static int16_t output_r[pico_clouds::kBlockSize];

  for (size_t i = 0; i < pico_clouds::kBlockSize; ++i) {
    const int32_t left = i2s.read();
    i2s.read();
    input_block[i] = left;
  }

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1);
#endif

  pico_clouds::RenderBlock(input_block, output_l, output_r);

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0);
#endif

  for (size_t i = 0; i < pico_clouds::kBlockSize; ++i) {
    i2s.write(static_cast<int32_t>(output_l[i]) * 65536);
    i2s.write(static_cast<int32_t>(output_r[i]) * 65536);
  }
}
