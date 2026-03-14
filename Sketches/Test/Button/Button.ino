// Copyright 2026 Rich Heslip
//
// Author: Wenhao Yang
//
// -----------------------------------------------------------------------------
//
// Simple button and RGB LED test for 2HPico (RP2350)
//
// Button:
// - Each press advances the LED color:
//   ORANGE -> YELLOW -> GREEN -> BLUE -> repeat
//
// LED:
// - Front panel RGB LED

#include <2HPico.h>
#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel LEDS(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);

static const uint32_t kColors[] = {ORANGE, YELLOW, GREEN, BLUE};
static uint8_t colorIndex = 3;
static bool buttonHeld = false;

static void showColor(uint32_t color) {
  LEDS.setPixelColor(0, color);
  LEDS.show();
}

void setup() {
  pinMode(BUTTON1, INPUT_PULLUP);

  LEDS.begin();
  LEDS.clear();
  LEDS.show();
}

void loop() {
  if (!digitalRead(BUTTON1)) {
    delay(DEBOUNCE);

    if (!digitalRead(BUTTON1) && !buttonHeld) {
      buttonHeld = true;
      colorIndex = (colorIndex + 1) % 4;
      showColor(kColors[colorIndex]);
    }
  } else {
    buttonHeld = false;
  }
}
