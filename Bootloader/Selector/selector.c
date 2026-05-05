// Copyright 2026 Wenhao Yang
//
// Author: Wenhao Yang
// Contributor: Wenhao Yang
//
// Minimal Pico-Eurorack bootloader and app selector.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#if defined(__arm__)
#include "hardware/structs/scb.h"
#endif

#include "selector_config.h"
#include "ws2812.pio.h"

#define PIN_BUTTON 2u
#define PIN_LED 3u
#define XIP_BASE_ADDR 0x10000000u
#define SRAM_BASE_ADDR 0x20000000u
#define SRAM_END_ADDR 0x20080000u
#define APP_VECTOR_WORDS 2u

typedef void (*AppEntry)(void);

static PIO led_pio = pio0;
static uint led_sm = 0;
static bool led_ready = false;

static uint32_t fnv1a32(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    hash ^= p[i];
    hash *= 16777619u;
  }
  return hash;
}

static uint32_t config_crc(const PicoSelectorConfig *cfg) {
  return fnv1a32(cfg, offsetof(PicoSelectorConfig, crc32));
}

static bool config_valid(const PicoSelectorConfig *cfg) {
  if (cfg->magic != PICO_SELECTOR_MAGIC) return false;
  if (cfg->version != PICO_SELECTOR_VERSION) return false;
  if (cfg->record_size != sizeof(PicoSelectorConfig)) return false;
  if (cfg->app_count == 0 || cfg->app_count > PICO_SELECTOR_MAX_APPS) return false;
  if (cfg->active_app >= cfg->app_count) return false;
  if (cfg->crc32 != config_crc(cfg)) return false;
  return true;
}

static void config_defaults(PicoSelectorConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->magic = PICO_SELECTOR_MAGIC;
  cfg->version = PICO_SELECTOR_VERSION;
  cfg->record_size = sizeof(PicoSelectorConfig);
  cfg->sequence = 1;
  cfg->active_app = 0;
  cfg->app_count = 4;
  cfg->calibration.cv1_counts_per_volt = 580.6f;
  cfg->calibration.cv2_counts_per_volt = 580.6f;
  cfg->calibration.cvout_counts_per_volt = 5456.0f;

#if PICO_SELECTOR_TARGET_RP2350
  const uint32_t target_flag = PICO_SELECTOR_FLAG_RP2350;
#else
  const uint32_t target_flag = PICO_SELECTOR_FLAG_RP2040;
#endif

  for (uint8_t i = 0; i < cfg->app_count; ++i) {
    cfg->apps[i].flash_offset = PICO_SELECTOR_FIRST_APP_OFFSET +
                                ((uint32_t)i * PICO_SELECTOR_DEFAULT_SLOT_BYTES);
    cfg->apps[i].max_size = PICO_SELECTOR_DEFAULT_SLOT_BYTES;
    cfg->apps[i].flags = PICO_SELECTOR_FLAG_VALID | target_flag;
    cfg->apps[i].app_id = i + 1u;
    cfg->apps[i].vector_offset = PICO_SELECTOR_ARDUINO_VECTOR_OFFSET;
  }
  cfg->crc32 = config_crc(cfg);
}

static const PicoSelectorConfig *config_at(uint32_t flash_offset) {
  return (const PicoSelectorConfig *)(XIP_BASE_ADDR + flash_offset);
}

static void config_load(PicoSelectorConfig *cfg) {
  const PicoSelectorConfig *a = config_at(PICO_SELECTOR_CONFIG_A_OFFSET);
  const PicoSelectorConfig *b = config_at(PICO_SELECTOR_CONFIG_B_OFFSET);
  const bool av = config_valid(a);
  const bool bv = config_valid(b);

  if (av && (!bv || a->sequence >= b->sequence)) {
    memcpy(cfg, a, sizeof(*cfg));
  } else if (bv) {
    memcpy(cfg, b, sizeof(*cfg));
  } else {
    config_defaults(cfg);
  }
}

static void flash_write_sector(uint32_t flash_offset, const PicoSelectorConfig *cfg) {
  uint8_t sector[FLASH_SECTOR_SIZE];
  memset(sector, 0xff, sizeof(sector));
  memcpy(sector, cfg, sizeof(*cfg));

  const uint32_t ints = save_and_disable_interrupts();
  multicore_reset_core1();
  flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
  flash_range_program(flash_offset, sector, FLASH_SECTOR_SIZE);
  restore_interrupts(ints);
}

static void config_save(PicoSelectorConfig *cfg) {
  cfg->magic = PICO_SELECTOR_MAGIC;
  cfg->version = PICO_SELECTOR_VERSION;
  cfg->record_size = sizeof(PicoSelectorConfig);
  cfg->sequence++;
  cfg->crc32 = 0;
  cfg->crc32 = config_crc(cfg);

  const PicoSelectorConfig *a = config_at(PICO_SELECTOR_CONFIG_A_OFFSET);
  const PicoSelectorConfig *b = config_at(PICO_SELECTOR_CONFIG_B_OFFSET);
  const bool av = config_valid(a);
  const bool bv = config_valid(b);
  const bool write_a = !av || (bv && b->sequence >= a->sequence);
  flash_write_sector(write_a ? PICO_SELECTOR_CONFIG_A_OFFSET : PICO_SELECTOR_CONFIG_B_OFFSET, cfg);
}

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8);
}

static uint32_t app_color(uint8_t app) {
  static const uint32_t colors[] = {
      0x180000u, 0x181000u, 0x181800u, 0x001800u,
      0x001818u, 0x000018u, 0x100018u, 0x181818u,
  };
  return colors[app & 7u];
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

static bool app_vector_valid(const PicoSelectorAppSlot *slot) {
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

static void jump_to_app(const PicoSelectorAppSlot *slot) {
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

static bool select_app(PicoSelectorConfig *cfg) {
  uint8_t selected = cfg->active_app;
  uint32_t pressed_ms = 0;
  uint32_t idle_ms = 0;

  led_blink(rgb(0, 0, 24), 2, 80, 80);
  led_set(app_color(selected));

  while (true) {
    sleep_ms(10);
    if (button_pressed()) {
      pressed_ms += 10;
      idle_ms = 0;
      if (pressed_ms >= PICO_SELECTOR_HOLD_CONFIRM_MS) {
        cfg->active_app = selected;
        config_save(cfg);
        led_blink(app_color(selected), 3, 120, 80);
        wait_button_release(2000);
        return true;
      }
    } else {
      if (pressed_ms > 30 && pressed_ms < PICO_SELECTOR_HOLD_CONFIRM_MS) {
        selected++;
        if (selected >= cfg->app_count) selected = 0;
        led_set(app_color(selected));
      }
      pressed_ms = 0;
      idle_ms += 10;
      if (idle_ms > 15000) return false;
    }
  }
}

int main(void) {
  button_init();
  led_init();

  PicoSelectorConfig cfg;
  config_load(&cfg);

  sleep_ms(30);
  bool enter_selector = false;
  if (button_pressed()) {
    uint32_t held_ms = 0;
    while (button_pressed() && held_ms < PICO_SELECTOR_HOLD_SELECT_MS) {
      led_set(rgb(0, 0, (held_ms / 80u) & 0x18u));
      sleep_ms(10);
      held_ms += 10;
    }
    enter_selector = held_ms >= PICO_SELECTOR_HOLD_SELECT_MS;
  } else {
    sleep_ms(PICO_SELECTOR_BOOT_GRACE_MS);
  }

  if (enter_selector) {
    (void)select_app(&cfg);
  }

  if (cfg.active_app < cfg.app_count) {
    const PicoSelectorAppSlot *slot = &cfg.apps[cfg.active_app];
    if ((slot->flags & PICO_SELECTOR_FLAG_VALID) && app_vector_valid(slot)) {
      led_blink(app_color(cfg.active_app), 1, 80, 30);
      jump_to_app(slot);
    }
  }

  led_blink(rgb(24, 0, 0), 6, 120, 120);
  watchdog_reboot(0, 0, 100);
  while (true) tight_loop_contents();
}
