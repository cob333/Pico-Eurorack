// Copyright 2026 Wenhao Yang
//
// Author: Wenhao Yang
// Contributor: Wenhao Yang
//
// Minimal Pico-Eurorack bootloader and app selector.

#include <stdbool.h>
#include <stdint.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#if defined(__arm__)
#include "hardware/structs/scb.h"
#endif

#include "boot_config.h"
#include "calibration.h"
#include "ws2812.pio.h"

#define PIN_BUTTON 2u
#define PIN_LED 3u
#define XIP_BASE_ADDR 0x10000000u
#define SRAM_BASE_ADDR 0x20000000u
#define SRAM_END_ADDR 0x20082000u
#define HOLD_SELECT_MS 700u
#define HOLD_CONFIRM_MS 900u
#define HOLD_CALIBRATE_MS 3000u
#define BOOT_GRACE_MS 600u

typedef void (*AppEntry)(void);

static PIO led_pio = pio0;
static uint led_sm = 0;
static bool led_ready = false;

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8);
}

static uint32_t app_color(uint8_t app) {
  switch (app % 6u) {
    case 0: return rgb(24, 8, 0);
    case 1: return rgb(24, 24, 0);
    case 2: return rgb(0, 24, 8);
    case 3: return rgb(0, 24, 24);
    case 4: return rgb(0, 0, 24);
    default: return rgb(24, 0, 24);
  }
}

static void led_init(void) {
  const uint offset = pio_add_program(led_pio, &ws2812_program);
  ws2812_program_init(led_pio, led_sm, offset, PIN_LED, 800000.0f, false);
  led_ready = true;
}

static void led_set(uint32_t color) {
  if (!led_ready) return;
  pio_sm_put_blocking(led_pio, led_sm, color);
}

static void led_blink(uint32_t color, uint8_t times, uint32_t on_ms, uint32_t off_ms) {
  for (uint8_t i = 0; i < times; ++i) {
    led_set(color);
    sleep_ms(on_ms);
    led_set(0);
    sleep_ms(off_ms);
  }
}

static void button_init(void) {
  gpio_init(PIN_BUTTON);
  gpio_set_dir(PIN_BUTTON, GPIO_IN);
  gpio_pull_up(PIN_BUTTON);
}

static bool button_pressed(void) {
  return !gpio_get(PIN_BUTTON);
}

static bool wait_button_release(uint32_t timeout_ms) {
  const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  while (button_pressed()) {
    if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) return false;
    sleep_ms(5);
  }
  sleep_ms(30);
  return true;
}

static bool app_vector_valid(const PicoBootAppSlot *slot) {
  const uint32_t vector_addr = XIP_BASE_ADDR + slot->flash_offset + slot->vector_offset;
  const uint32_t *vector = (const uint32_t *)vector_addr;
  const uint32_t sp = vector[0];
  const uint32_t reset = vector[1];
  if (sp < SRAM_BASE_ADDR || sp > SRAM_END_ADDR) return false;
  if (slot->vector_offset >= slot->max_size) return false;
  if (reset < XIP_BASE_ADDR + slot->flash_offset) return false;
  if (reset >= XIP_BASE_ADDR + slot->flash_offset + slot->max_size) return false;
  if ((reset & 1u) == 0) return false;
  return true;
}

static bool slot_has_app(const PicoBootConfig *cfg, uint8_t app) {
  if (app >= cfg->app_count) return false;
  const PicoBootAppSlot *slot = &cfg->apps[app];
  return (slot->flags & PICO_BOOT_FLAG_VALID) && app_vector_valid(slot);
}

static uint32_t selector_slot_color(const PicoBootConfig *cfg, uint8_t app) {
  if (!slot_has_app(cfg, app)) return rgb(24, 24, 24);
  return app_color(app);
}

static void jump_to_app(const PicoBootAppSlot *slot) {
  const uint32_t vector_addr = XIP_BASE_ADDR + slot->flash_offset + slot->vector_offset;
  const uint32_t *vector = (const uint32_t *)vector_addr;
  const uint32_t app_sp = vector[0];
  const uint32_t app_reset = vector[1];

  led_set(0);
  sleep_ms(2);
  __asm volatile("cpsid i");
  multicore_reset_core1();

#if defined(__arm__)
  scb_hw->vtor = vector_addr;
  __asm volatile("msr msp, %0\n"
                 "bx %1\n"
                 :
                 : "r"(app_sp), "r"(app_reset)
                 :);
#else
  (void)app_sp;
  ((AppEntry)app_reset)();
#endif

  while (true) tight_loop_contents();
}

static bool select_app(PicoBootConfig *cfg) {
  uint8_t selected = cfg->active_app;
  uint32_t pressed_ms = 0;
  uint32_t idle_ms = 0;

  led_blink(rgb(0, 0, 24), 2, 80, 80);
  led_set(selector_slot_color(cfg, selected));

  while (true) {
    sleep_ms(10);
    if (button_pressed()) {
      pressed_ms += 10;
      idle_ms = 0;
      if (pressed_ms >= HOLD_CONFIRM_MS) {
        cfg->active_app = selected;
        pico_boot_config_save(cfg);
        led_blink(selector_slot_color(cfg, selected), 3, 120, 80);
        wait_button_release(2000);
        return true;
      }
    } else {
      if (pressed_ms > 30 && pressed_ms < HOLD_CONFIRM_MS) {
        selected++;
        if (selected >= cfg->app_count) selected = 0;
        led_set(selector_slot_color(cfg, selected));
      }
      pressed_ms = 0;
      idle_ms += 10;
      if (idle_ms > 15000) return false;
    }
  }
}

static void apply_cpu_frequency(const PicoBootConfig *cfg) {
  if (cfg->cpu_hz >= 48000000u && cfg->cpu_hz <= 300000000u) {
    set_sys_clock_khz(cfg->cpu_hz / 1000u, true);
  }
}

int main(void) {
  button_init();
  led_init();

  PicoBootConfig cfg;
  pico_boot_config_load(&cfg);
  apply_cpu_frequency(&cfg);

  sleep_ms(30);
  bool enter_selector = false;
  bool enter_calibration = false;
  if (button_pressed()) {
    uint32_t held_ms = 0;
    while (button_pressed() && held_ms < HOLD_CALIBRATE_MS) {
      if (held_ms >= HOLD_SELECT_MS) {
        led_set(rgb(18, 18, 0));
      } else {
        led_set(rgb(0, 0, (held_ms / 80u) & 0x18u));
      }
      sleep_ms(10);
      held_ms += 10;
    }
    enter_calibration = held_ms >= HOLD_CALIBRATE_MS;
    enter_selector = !enter_calibration && held_ms >= HOLD_SELECT_MS;
  } else {
    sleep_ms(BOOT_GRACE_MS);
  }

  if (enter_calibration) {
    const PicoBootCalibrationUi ui = {
        .led_set = led_set,
        .led_blink = led_blink,
        .button_pressed = button_pressed,
        .wait_button_release = wait_button_release,
    };
    (void)wait_button_release(2000);
    (void)pico_boot_calibration_run(&cfg, &ui);
  }

  if (enter_selector) {
    (void)select_app(&cfg);
  }

  if (cfg.active_app < cfg.app_count) {
    const PicoBootAppSlot *slot = &cfg.apps[cfg.active_app];
    if ((slot->flags & PICO_BOOT_FLAG_VALID) && app_vector_valid(slot)) {
      led_blink(rgb(0, 24, 0), 3, 90, 70);
      jump_to_app(slot);
    }
  }

  led_blink(rgb(24, 0, 0), 6, 120, 120);
  watchdog_reboot(0, 0, 100);
  while (true) tight_loop_contents();
}
