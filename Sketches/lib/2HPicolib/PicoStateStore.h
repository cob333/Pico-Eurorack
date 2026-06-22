// Copyright 2026 Wenhao Yang
//
// Power-loss-tolerant per-slot parameter storage for Pico-Eurorack apps.

#ifndef PICO_STATE_STORE_H_
#define PICO_STATE_STORE_H_

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class PicoStateStore {
 public:
  static constexpr size_t kMaxPayloadBytes = 224;

  // slot < 0 uses the active slot recorded by the bootloader.
  bool begin(uint32_t app_key,
             uint16_t schema_version,
             uint32_t save_interval_ms = 10000,
             int slot = -1);

  bool load(void *payload, size_t payload_size);
  bool save(const void *payload, size_t payload_size);

  // Call regularly from core 0. Every save_interval_ms the current payload is
  // checkpointed if it differs from the last committed payload.
  bool service(const void *payload, size_t payload_size, uint32_t now_ms = millis());
  void markDirty(uint32_t now_ms = millis());

  bool dirty() const { return dirty_; }
  uint8_t slot() const { return slot_; }

 private:
  struct ScanResult {
    bool found;
    uint8_t sector;
    uint8_t record;
    uint32_t sequence;
    int8_t free_record[2];
  };

  bool scan(size_t payload_size, ScanResult *result) const;
  bool programRecord(uint8_t sector,
                     uint8_t record,
                     uint32_t sequence,
                     const void *payload,
                     size_t payload_size);
  bool eraseSector(uint8_t sector);
  uint32_t payloadHash(const void *payload, size_t payload_size) const;
  uint32_t sectorOffset(uint8_t sector) const;

  uint32_t app_key_ = 0;
  uint16_t schema_version_ = 0;
  uint8_t slot_ = 0;
  uint32_t save_interval_ms_ = 10000;
  uint32_t observed_hash_ = 0;
  uint32_t persisted_hash_ = 0;
  uint32_t last_checkpoint_ms_ = 0;
  bool ready_ = false;
  bool tracking_ = false;
  bool has_persisted_state_ = false;
  bool dirty_ = false;
};

#endif
