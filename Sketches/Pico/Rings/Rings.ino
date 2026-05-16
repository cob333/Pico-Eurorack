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
// -----------------------------------------------------------------------------
//
// Mutable Instruments Rings port for 2HPico.
// This host keeps only the trigger + pitch CV workflow used by 2HPico and
// drops unused excitation/audio CV scanning from the reference sketches.
// Contributor: Wenhao Yang
//
// top jack - trigger input
// middle jack - pitch CV input

// pot 4 - frequency
//
// page 2 parameters - Violet LED
// Pot 1 - Position
// pot 2 - slide
// pot 3 - Polyphony setting
// pot 4 - Resonator type
//
// Short click: switch parameter page.
// Hold 3 seconds: save the current settings to flash.
//
// Requires a Pico 2 / RP2350 board. 44.1kHz is used here to leave headroom for
// the heaviest Rings models while keeping 4-voice modes available.

#include "2HPico.h"
#include <I2S.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "pico/multicore.h"

#define MONITOR_CPU1
#define SAMPLERATE 44100

#include "Rings.h"

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
I2S DAC(OUTPUT);

bool trigger = false;
bool button = false;

#define NUMUISTATES 2
enum UIstates { SET1, SET2 };
uint8_t UIstate = SET1;

constexpr uint32_t kControlUpdateMs = 10;
#define CV_VOLT_DEFAULT 582.52f
#define EEPROM_BYTES 256
#define RINGS_STORE_MAGIC 0x52494E47u // "RING"
#define RINGS_STORE_VERSION 3u
#define LEGACY_RINGS_STORE_VERSION 2u
#define CV_VOLT_MIN 100.0f
#define CV_VOLT_MAX 2000.0f
constexpr uint32_t kSaveHoldMs = 3000;
constexpr uint16_t kFeedbackBlinkMs = 120;
constexpr uint32_t kPolyFeedbackMs = 2000;
constexpr float kPitchCvBaseSlewTimeSeconds = 0.001f;
constexpr float kPitchCvMaxSlewTimeSeconds = 0.300f;
constexpr float kPitchCvDeadbandSemitones = 0.05f;

uint32_t buttontimer = 0;
uint32_t buttonpress_start = 0;
uint32_t trigtimer = 0;
uint32_t parameterupdate = 0;
bool buttonhold_handled = false;
bool poly_feedback_active = false;
uint32_t poly_feedback_until = 0;
float smoothed_cv_note = 0.0f;
uint32_t pitch_cv_update_us = 0;
bool pitch_cv_filter_initialized = false;

float structure_value = pico_rings::kDefaultStructure;
float damping_value = pico_rings::kDefaultDamping;
float brightness_value = pico_rings::kDefaultBrightness;
float base_note = pico_rings::kDefaultNote;
float position_value = pico_rings::kDefaultPosition;
float slide_value = 0.0f;
uint8_t polyphony_setting = 1;
uint8_t resonator_type = rings::RESONATOR_MODEL_MODAL;
float cvVolt = CV_VOLT_DEFAULT;

struct RingsStore {
  uint32_t magic;
  uint16_t version;
  uint8_t ui_state;
  uint8_t reserved0;
  float structure_value;
  float damping_value;
  float brightness_value;
  float base_note;
  float position_value;
  float slide_value;
  float cvVolt;
  uint8_t polyphony_setting;
  uint8_t resonator_type;
  uint8_t reserved1;
  uint8_t reserved2;
  uint32_t checksum;
};

struct LegacyRingsStore {
  uint32_t magic;
  uint16_t version;
  uint8_t ui_state;
  uint8_t reserved0;
  float structure_value;
  float damping_value;
  float brightness_value;
  float base_note;
  float position_value;
  float cvVolt;
  uint8_t polyphony_setting;
  uint8_t resonator_type;
  uint8_t reserved1;
  uint8_t reserved2;
  uint32_t checksum;
};

static uint32_t ringsStoreChecksum(const RingsStore &data) {
  uint32_t hash = 2166136261u;
  const uint8_t *raw = reinterpret_cast<const uint8_t*>(&data);
  for (uint32_t i = 0; i < (uint32_t)(sizeof(RingsStore) - sizeof(uint32_t)); ++i) {
    hash ^= raw[i];
    hash *= 16777619u;
  }
  return hash;
}

static uint32_t legacyRingsStoreChecksum(const LegacyRingsStore &data) {
  uint32_t hash = 2166136261u;
  const uint8_t *raw = reinterpret_cast<const uint8_t*>(&data);
  for (uint32_t i = 0; i < (uint32_t)(sizeof(LegacyRingsStore) - sizeof(uint32_t)); ++i) {
    hash ^= raw[i];
    hash *= 16777619u;
  }
  return hash;
}

static void sanitizeRuntimeState() {
  if (UIstate >= NUMUISTATES) {
    UIstate = SET1;
  }

  if (!isfinite(structure_value)) structure_value = pico_rings::kDefaultStructure;
  if (!isfinite(damping_value)) damping_value = pico_rings::kDefaultDamping;
  if (!isfinite(brightness_value)) brightness_value = pico_rings::kDefaultBrightness;
  if (!isfinite(base_note)) base_note = pico_rings::kDefaultNote;
  if (!isfinite(position_value)) position_value = pico_rings::kDefaultPosition;
  if (!isfinite(slide_value)) slide_value = 0.0f;
  if (!isfinite(cvVolt)) cvVolt = CV_VOLT_DEFAULT;

  structure_value = constrain(structure_value, 0.0f, 1.0f);
  damping_value = constrain(damping_value, 0.0f, 1.0f);
  brightness_value = constrain(brightness_value, 0.0f, 1.0f);
  base_note = constrain(base_note, 12.0f, 84.0f);
  position_value = constrain(position_value, 0.0f, 1.0f);
  slide_value = constrain(slide_value, 0.0f, 1.0f);
  cvVolt = constrain(cvVolt, CV_VOLT_MIN, CV_VOLT_MAX);
  if (polyphony_setting <= 1) {
    polyphony_setting = 1;
  } else if (polyphony_setting == 2) {
    polyphony_setting = 2;
  } else {
    polyphony_setting = 4;
  }
  if (resonator_type >= pico_rings::kNumModels) {
    resonator_type = pico_rings::kNumModels - 1;
  }
}

static bool validateStore(const RingsStore &data) {
  if (data.magic != RINGS_STORE_MAGIC) return false;
  if (data.version != RINGS_STORE_VERSION) return false;
  if (data.ui_state >= NUMUISTATES) return false;
  if (!isfinite(data.structure_value) || (data.structure_value < 0.0f) || (data.structure_value > 1.0f)) return false;
  if (!isfinite(data.damping_value) || (data.damping_value < 0.0f) || (data.damping_value > 1.0f)) return false;
  if (!isfinite(data.brightness_value) || (data.brightness_value < 0.0f) || (data.brightness_value > 1.0f)) return false;
  if (!isfinite(data.base_note) || (data.base_note < 12.0f) || (data.base_note > 84.0f)) return false;
  if (!isfinite(data.position_value) || (data.position_value < 0.0f) || (data.position_value > 1.0f)) return false;
  if (!isfinite(data.slide_value) || (data.slide_value < 0.0f) || (data.slide_value > 1.0f)) return false;
  if (!isfinite(data.cvVolt) || (data.cvVolt < CV_VOLT_MIN) || (data.cvVolt > CV_VOLT_MAX)) return false;
  if ((data.polyphony_setting != 1) &&
      (data.polyphony_setting != 2) &&
      (data.polyphony_setting != 4)) return false;
  if (data.resonator_type >= pico_rings::kNumModels) return false;
  if (data.checksum != ringsStoreChecksum(data)) return false;
  return true;
}

static bool validateLegacyRingsStore(const LegacyRingsStore &data) {
  if (data.magic != RINGS_STORE_MAGIC) return false;
  if (data.version != LEGACY_RINGS_STORE_VERSION) return false;
  if (data.ui_state >= NUMUISTATES) return false;
  if (!isfinite(data.structure_value) || (data.structure_value < 0.0f) || (data.structure_value > 1.0f)) return false;
  if (!isfinite(data.damping_value) || (data.damping_value < 0.0f) || (data.damping_value > 1.0f)) return false;
  if (!isfinite(data.brightness_value) || (data.brightness_value < 0.0f) || (data.brightness_value > 1.0f)) return false;
  if (!isfinite(data.base_note) || (data.base_note < 12.0f) || (data.base_note > 84.0f)) return false;
  if (!isfinite(data.position_value) || (data.position_value < 0.0f) || (data.position_value > 1.0f)) return false;
  if (!isfinite(data.cvVolt) || (data.cvVolt < CV_VOLT_MIN) || (data.cvVolt > CV_VOLT_MAX)) return false;
  if ((data.polyphony_setting != 1) &&
      (data.polyphony_setting != 2) &&
      (data.polyphony_setting != 4)) return false;
  if (data.resonator_type >= pico_rings::kNumModels) return false;
  if (data.checksum != legacyRingsStoreChecksum(data)) return false;
  return true;
}

static void copyStateToStore(RingsStore &data) {
  sanitizeRuntimeState();
  data.magic = RINGS_STORE_MAGIC;
  data.version = RINGS_STORE_VERSION;
  data.ui_state = UIstate;
  data.reserved0 = 0;
  data.structure_value = structure_value;
  data.damping_value = damping_value;
  data.brightness_value = brightness_value;
  data.base_note = base_note;
  data.position_value = position_value;
  data.slide_value = slide_value;
  data.cvVolt = cvVolt;
  data.polyphony_setting = polyphony_setting;
  data.resonator_type = resonator_type;
  data.reserved1 = 0;
  data.reserved2 = 0;
}

static bool loadStateFromFlash() {
  RingsStore data;
  EEPROM.get(0, data);
  if (validateStore(data)) {
    UIstate = data.ui_state;
    structure_value = data.structure_value;
    damping_value = data.damping_value;
    brightness_value = data.brightness_value;
    base_note = data.base_note;
    position_value = data.position_value;
    slide_value = data.slide_value;
    cvVolt = data.cvVolt;
    polyphony_setting = data.polyphony_setting;
    resonator_type = data.resonator_type;
    sanitizeRuntimeState();
    return true;
  }

  LegacyRingsStore legacy_data;
  EEPROM.get(0, legacy_data);
  if (!validateLegacyRingsStore(legacy_data)) return false;

  UIstate = legacy_data.ui_state;
  structure_value = legacy_data.structure_value;
  damping_value = legacy_data.damping_value;
  brightness_value = legacy_data.brightness_value;
  base_note = legacy_data.base_note;
  position_value = legacy_data.position_value;
  slide_value = 0.0f;
  cvVolt = legacy_data.cvVolt;
  polyphony_setting = legacy_data.polyphony_setting;
  resonator_type = legacy_data.resonator_type;
  sanitizeRuntimeState();
  return true;
}

static bool saveStateToFlash() {
  RingsStore data;
  copyStateToStore(data);
  data.checksum = ringsStoreChecksum(data);

  EEPROM.put(0, data);
  return EEPROM.commit();
}

static void setLedColor(uint32_t color) {
  LEDS.setPixelColor(0, color);
  LEDS.show();
}

static uint32_t polyphonyLedColor(uint8_t polyphony) {
  switch (polyphony) {
    case 1:
      return GREEN;
    case 2:
      return ORANGE;
    case 4:
    default:
      return RED;
  }
}

static uint32_t pageLedColor() {
  return UIstate == SET2 ? VIOLET : AQUA;
}

static void setPageLed() {
  setLedColor(pageLedColor());
}

static void updateUiLed();

static void blinkLed(uint32_t color, uint8_t times, uint16_t onMs, uint16_t offMs) {
  for (uint8_t i = 0; i < times; ++i) {
    setLedColor(color);
    delay(onMs);
    setLedColor(0);
    delay(offMs);
  }
}

static void blinkFeedback(uint32_t color, uint8_t times) {
  blinkLed(color, times, kFeedbackBlinkMs, kFeedbackBlinkMs);
  updateUiLed();
}

static uint8_t mapPolyphonySetting(uint16_t value) {
  const uint32_t scaled = static_cast<uint32_t>(value) * 3u;
  const uint8_t slot = static_cast<uint8_t>(scaled / AD_RANGE);
  switch (slot >= 3 ? 2 : slot) {
    case 0:
      return 1;
    case 1:
      return 2;
    case 2:
    default:
      return 4;
  }
}

static void startPolyFeedback(uint8_t polyphony) {
  poly_feedback_active = true;
  poly_feedback_until = millis() + kPolyFeedbackMs;
  setLedColor(polyphonyLedColor(polyphony));
}

static float pitchCvSlewTimeSeconds() {
  const float shaped = slide_value * slide_value;
  return kPitchCvBaseSlewTimeSeconds +
         (kPitchCvMaxSlewTimeSeconds - kPitchCvBaseSlewTimeSeconds) * shaped;
}

static void updateUiLed() {
  if (UIstate != SET2) {
    poly_feedback_active = false;
    setPageLed();
    return;
  }

  if (poly_feedback_active) {
    const int32_t remaining = static_cast<int32_t>(poly_feedback_until - millis());
    if (remaining > 0) {
      setLedColor(polyphonyLedColor(polyphony_setting));
      return;
    }
    poly_feedback_active = false;
  }

  setPageLed();
}

inline uint8_t MapDiscrete(uint16_t value, uint8_t num_values) {
  uint32_t scaled = static_cast<uint32_t>(value) * num_values;
  uint8_t result = static_cast<uint8_t>(scaled / AD_RANGE);
  if (result >= num_values) {
    result = num_values - 1;
  }
  return result;
}

static float smoothPitchCvNote(float target_cv_note) {
  const uint32_t now = micros();

  if (!pitch_cv_filter_initialized) {
    smoothed_cv_note = target_cv_note;
    pitch_cv_update_us = now;
    pitch_cv_filter_initialized = true;
    return smoothed_cv_note;
  }

  const float delta = target_cv_note - smoothed_cv_note;
  const uint32_t elapsed_us = now - pitch_cv_update_us;
  pitch_cv_update_us = now;

  if (fabsf(delta) <= kPitchCvDeadbandSemitones) {
    return smoothed_cv_note;
  }

  const float dt = static_cast<float>(elapsed_us) * 1.0e-6f;
  const float slew_time = pitchCvSlewTimeSeconds();
  const float alpha = constrain(dt / (slew_time + dt), 0.0f, 1.0f);
  smoothed_cv_note += delta * alpha;
  return smoothed_cv_note;
}

inline float SamplePitchNote() {
  const float cv = static_cast<float>(AD_RANGE - sampleCV2());
  const float cv_note = smoothPitchCvNote((cv / cvVolt) * 12.0f);
  return constrain(base_note + cv_note, pico_rings::kMinNote, pico_rings::kMaxNote);
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
          UIstate = SET1;
        }
        poly_feedback_active = false;
        lockpots();
        updateUiLed();
      }
    }
    return;
  }

  if (button && !buttonhold_handled && ((now - buttonpress_start) >= kSaveHoldMs)) {
    const bool saved = saveStateToFlash();
    buttonhold_handled = true;
    blinkFeedback(saved ? GREEN : RED, 3);
  }
}

inline void PushUiState(float note) {
  pico_rings::SetUiState(
      structure_value,
      damping_value,
      brightness_value,
      note,
      position_value,
      polyphony_setting,
      resonator_type);
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIGGER, INPUT_PULLUP);
  pinMode(CV2IN, INPUT);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(MUXCTL, OUTPUT);

#ifdef MONITOR_CPU1
  pinMode(CPU_USE, OUTPUT);
#endif

  analogReadResolution(AD_BITS);
  for (uint8_t i = 0; i < NUMPOTS; ++i) {
    pot[i] = 0;
    potlock[i] = 0;
  }

  EEPROM.begin(EEPROM_BYTES);
  if (loadStateFromFlash()) {
    lockpots();
  }

  LEDS.begin();
  setPageLed();

  pico_rings::Init();
  sanitizeRuntimeState();
  PushUiState(base_note);

  DAC.setBCLK(BCLK);
  DAC.setDATA(I2S_DATA);
  DAC.setBitsPerSample(16);
  DAC.setBuffers(1, 128, 0);
  DAC.setLSBJFormat();
  DAC.begin(SAMPLERATE);
}

void loop() {
  float current_note;

  serviceButton();

  if ((millis() - parameterupdate) > kControlUpdateMs) {
    parameterupdate = millis();
    samplepots();

    switch (UIstate) {
      case SET1:
        updateUiLed();
        if (!potlock[0]) {
          structure_value = mapf(pot[0], 0, AD_RANGE - 1, 0.0f, 1.0f);
        }
        if (!potlock[1]) {
          damping_value = mapf(pot[1], 0, AD_RANGE - 1, 0.0f, 1.0f);
        }
        if (!potlock[2]) {
          brightness_value = mapf(pot[2], 0, AD_RANGE - 1, 0.0f, 1.0f);
        }
        if (!potlock[3]) {
          base_note = mapf(pot[3], 0, AD_RANGE - 1, 12.0f, 84.0f);
        }
        break;

      case SET2:
        updateUiLed();
        if (!potlock[0]) {
          position_value = mapf(pot[0], 0, AD_RANGE - 1, 0.0f, 1.0f);
        }
        if (!potlock[1]) {
          slide_value = mapf(pot[1], 0, AD_RANGE - 1, 0.0f, 1.0f);
        }
        if (!potlock[2]) {
          const uint8_t new_polyphony = mapPolyphonySetting(pot[2]);
          if (new_polyphony != polyphony_setting) {
            polyphony_setting = new_polyphony;
            startPolyFeedback(polyphony_setting);
          }
        }
        if (!potlock[3]) {
          resonator_type = MapDiscrete(pot[3], pico_rings::kNumModels);
        }
        break;

      default:
        break;
    }

    updateUiLed();
  }

  current_note = SamplePitchNote();
  PushUiState(current_note);

  if (!digitalRead(TRIGGER)) {
    if (((millis() - trigtimer) > TRIG_DEBOUNCE) && !trigger) {
      trigger = true;
      pico_rings::QueueTrigger();
    }
  } else {
    trigtimer = millis();
    trigger = false;
  }
}

void setup1() {
  delay(1000);
}

void loop1() {
  const int16_t outsample = pico_rings::NextSample();

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 0);
#endif

  DAC.write(outsample);
  DAC.write(outsample);

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE, 1);
#endif
}
