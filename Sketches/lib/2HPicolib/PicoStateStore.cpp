// Copyright 2026 Wenhao Yang

#include "PicoStateStore.h"

#include <string.h>

#include "PicoBootConfig.h"
#include "hardware/flash.h"

namespace {

constexpr uint32_t kXipBase = 0x10000000u;
constexpr uint32_t kRecordMagic = 0x50535452u;  // "PSTR"
constexpr uint32_t kCommitMagic = 0x434d4954u;  // "CMIT"
constexpr uint16_t kFormatVersion = 1u;
constexpr uint32_t kPageBytes = 256u;
constexpr uint32_t kSectorBytes = 4096u;
constexpr uint8_t kRecordsPerSector = kSectorBytes / (2u * kPageBytes);

#pragma pack(push, 1)
struct RecordHeader {
  uint32_t magic;
  uint16_t format_version;
  uint16_t header_size;
  uint32_t app_key;
  uint16_t schema_version;
  uint16_t payload_size;
  uint32_t sequence;
  uint32_t checksum;
  uint32_t reserved[2];
};

struct RecordPage {
  RecordHeader header;
  uint8_t payload[PicoStateStore::kMaxPayloadBytes];
};

struct CommitPage {
  uint32_t magic;
  uint32_t app_key;
  uint32_t sequence;
  uint32_t checksum;
  uint8_t reserved[kPageBytes - 16u];
};
#pragma pack(pop)

static_assert(sizeof(RecordHeader) == 32, "state record header must be 32 bytes");
static_assert(sizeof(RecordPage) == kPageBytes, "state record must fill one flash page");
static_assert(sizeof(CommitPage) == kPageBytes, "commit marker must fill one flash page");
static_assert(PICO_BOOT_STATE_SLOT_BYTES >= 2u * kSectorBytes,
              "each state slot must contain two flash sectors");

uint32_t fnvUpdate(uint32_t hash, const void *data, size_t length) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  for (size_t i = 0; i < length; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t recordChecksum(uint32_t app_key,
                        uint16_t schema_version,
                        uint16_t payload_size,
                        uint32_t sequence,
                        const void *payload) {
  uint32_t hash = 2166136261u;
  hash = fnvUpdate(hash, &app_key, sizeof(app_key));
  hash = fnvUpdate(hash, &schema_version, sizeof(schema_version));
  hash = fnvUpdate(hash, &payload_size, sizeof(payload_size));
  hash = fnvUpdate(hash, &sequence, sizeof(sequence));
  return fnvUpdate(hash, payload, payload_size);
}

bool pageErased(const void *address) {
  const uint32_t *words = static_cast<const uint32_t *>(address);
  for (size_t i = 0; i < kPageBytes / sizeof(uint32_t); ++i) {
    if (words[i] != 0xffffffffu) return false;
  }
  return true;
}

bool sequenceNewer(uint32_t candidate, uint32_t current) {
  return static_cast<int32_t>(candidate - current) > 0;
}

void flashProgramPage(uint32_t flash_offset, const void *page) {
  noInterrupts();
  rp2040.idleOtherCore();
  flash_range_program(flash_offset, static_cast<const uint8_t *>(page), kPageBytes);
  rp2040.resumeOtherCore();
  interrupts();
}

void flashEraseSector(uint32_t flash_offset) {
  noInterrupts();
  rp2040.idleOtherCore();
  flash_range_erase(flash_offset, kSectorBytes);
  rp2040.resumeOtherCore();
  interrupts();
}

}  // namespace

bool PicoStateStore::begin(uint32_t app_key,
                           uint16_t schema_version,
                           uint32_t save_interval_ms,
                           int slot) {
  if (app_key == 0 || schema_version == 0) return false;

  if (slot < 0) {
    PicoBootConfig config;
    PicoBootLoadConfig(&config);
    slot = config.active_app;
  }
  if (slot < 0 || slot >= PICO_BOOT_CONFIG_MAX_APPS) return false;

  app_key_ = app_key;
  schema_version_ = schema_version;
  slot_ = static_cast<uint8_t>(slot);
  save_interval_ms_ = save_interval_ms ? save_interval_ms : 10000u;
  last_checkpoint_ms_ = millis();
  tracking_ = false;
  has_persisted_state_ = false;
  dirty_ = false;
  ready_ = true;
  return true;
}

uint32_t PicoStateStore::sectorOffset(uint8_t sector) const {
  return PICO_BOOT_STATE_OFFSET +
         (static_cast<uint32_t>(slot_) * PICO_BOOT_STATE_SLOT_BYTES) +
         (static_cast<uint32_t>(sector) * kSectorBytes);
}

uint32_t PicoStateStore::payloadHash(const void *payload, size_t payload_size) const {
  return fnvUpdate(2166136261u, payload, payload_size);
}

bool PicoStateStore::scan(size_t payload_size, ScanResult *result) const {
  if (!ready_ || !result || payload_size == 0 || payload_size > kMaxPayloadBytes) return false;

  memset(result, 0, sizeof(*result));
  result->free_record[0] = -1;
  result->free_record[1] = -1;

  for (uint8_t sector = 0; sector < 2; ++sector) {
    const uint32_t base = sectorOffset(sector);
    for (uint8_t record = 0; record < kRecordsPerSector; ++record) {
      const uint32_t record_offset = base + (static_cast<uint32_t>(record) * 2u * kPageBytes);
      const RecordPage *data = reinterpret_cast<const RecordPage *>(kXipBase + record_offset);
      const CommitPage *commit = reinterpret_cast<const CommitPage *>(kXipBase + record_offset + kPageBytes);

      if (pageErased(data) && pageErased(commit)) {
        if (result->free_record[sector] < 0) result->free_record[sector] = record;
        continue;
      }

      const RecordHeader &header = data->header;
      if (header.magic != kRecordMagic || header.format_version != kFormatVersion ||
          header.header_size != sizeof(RecordHeader) || header.app_key != app_key_ ||
          header.schema_version != schema_version_ || header.payload_size != payload_size) {
        continue;
      }
      if (commit->magic != kCommitMagic || commit->app_key != app_key_ ||
          commit->sequence != header.sequence || commit->checksum != header.checksum) {
        continue;
      }
      if (header.checksum != recordChecksum(header.app_key,
                                            header.schema_version,
                                            header.payload_size,
                                            header.sequence,
                                            data->payload)) {
        continue;
      }
      if (!result->found || sequenceNewer(header.sequence, result->sequence)) {
        result->found = true;
        result->sector = sector;
        result->record = record;
        result->sequence = header.sequence;
      }
    }
  }
  return true;
}

bool PicoStateStore::load(void *payload, size_t payload_size) {
  if (!payload) return false;
  ScanResult result;
  if (!scan(payload_size, &result) || !result.found) return false;

  const uint32_t offset = sectorOffset(result.sector) +
                          (static_cast<uint32_t>(result.record) * 2u * kPageBytes);
  const RecordPage *record = reinterpret_cast<const RecordPage *>(kXipBase + offset);
  memcpy(payload, record->payload, payload_size);
  observed_hash_ = payloadHash(payload, payload_size);
  persisted_hash_ = observed_hash_;
  tracking_ = true;
  has_persisted_state_ = true;
  dirty_ = false;
  return true;
}

bool PicoStateStore::eraseSector(uint8_t sector) {
  if (sector >= 2) return false;
  flashEraseSector(sectorOffset(sector));
  return true;
}

bool PicoStateStore::programRecord(uint8_t sector,
                                   uint8_t record,
                                   uint32_t sequence,
                                   const void *payload,
                                   size_t payload_size) {
  if (sector >= 2 || record >= kRecordsPerSector || !payload ||
      payload_size == 0 || payload_size > kMaxPayloadBytes) {
    return false;
  }

  RecordPage data;
  memset(&data, 0xff, sizeof(data));
  data.header.magic = kRecordMagic;
  data.header.format_version = kFormatVersion;
  data.header.header_size = sizeof(RecordHeader);
  data.header.app_key = app_key_;
  data.header.schema_version = schema_version_;
  data.header.payload_size = static_cast<uint16_t>(payload_size);
  data.header.sequence = sequence;
  memcpy(data.payload, payload, payload_size);
  data.header.checksum = recordChecksum(app_key_,
                                        schema_version_,
                                        data.header.payload_size,
                                        sequence,
                                        data.payload);

  CommitPage commit;
  memset(&commit, 0xff, sizeof(commit));
  commit.magic = kCommitMagic;
  commit.app_key = app_key_;
  commit.sequence = sequence;
  commit.checksum = data.header.checksum;

  const uint32_t offset = sectorOffset(sector) +
                          (static_cast<uint32_t>(record) * 2u * kPageBytes);
  flashProgramPage(offset, &data);
  flashProgramPage(offset + kPageBytes, &commit);  // Atomic visibility point.
  return true;
}

bool PicoStateStore::save(const void *payload, size_t payload_size) {
  if (!payload) return false;
  ScanResult result;
  if (!scan(payload_size, &result)) return false;

  uint8_t target_sector = 0;
  int8_t target_record = -1;
  if (result.found && result.free_record[result.sector] >= 0) {
    target_sector = result.sector;
    target_record = result.free_record[target_sector];
  } else if (!result.found && result.free_record[0] >= 0) {
    target_record = result.free_record[0];
  } else if (!result.found && result.free_record[1] >= 0) {
    target_sector = 1;
    target_record = result.free_record[1];
  } else {
    target_sector = result.found ? static_cast<uint8_t>(1u - result.sector) : 0u;
    if (!eraseSector(target_sector)) return false;
    target_record = 0;
  }

  uint32_t sequence = result.found ? result.sequence + 1u : 1u;
  if (sequence == 0) sequence = 1;
  if (!programRecord(target_sector,
                     static_cast<uint8_t>(target_record),
                     sequence,
                     payload,
                     payload_size)) {
    return false;
  }

  ScanResult verify;
  if (!scan(payload_size, &verify) || !verify.found || verify.sequence != sequence) {
    return false;
  }
  const uint32_t verify_offset = sectorOffset(verify.sector) +
                                 (static_cast<uint32_t>(verify.record) * 2u * kPageBytes);
  const RecordPage *written = reinterpret_cast<const RecordPage *>(kXipBase + verify_offset);
  if (memcmp(written->payload, payload, payload_size) != 0) return false;

  observed_hash_ = payloadHash(payload, payload_size);
  persisted_hash_ = observed_hash_;
  tracking_ = true;
  has_persisted_state_ = true;
  dirty_ = false;
  last_checkpoint_ms_ = millis();
  return true;
}

void PicoStateStore::markDirty(uint32_t now_ms) {
  if (!ready_) return;
  (void)now_ms;
  dirty_ = true;
}

bool PicoStateStore::service(const void *payload, size_t payload_size, uint32_t now_ms) {
  if (!ready_ || !payload || payload_size == 0 || payload_size > kMaxPayloadBytes) return false;

  const uint32_t hash = payloadHash(payload, payload_size);
  if (!tracking_) {
    observed_hash_ = hash;
    tracking_ = true;
    // A new app/slot has no committed baseline yet. Persist the first complete
    // snapshot at the next 10-second checkpoint.
    if (!has_persisted_state_) dirty_ = true;
    return false;
  }
  if (hash != observed_hash_) {
    observed_hash_ = hash;
    markDirty(now_ms);
  }
  if (static_cast<uint32_t>(now_ms - last_checkpoint_ms_) < save_interval_ms_) return false;
  last_checkpoint_ms_ = now_ms;
  if (!dirty_) return false;
  if (has_persisted_state_ && hash == persisted_hash_) {
    dirty_ = false;
    return false;
  }
  return save(payload, payload_size);
}
