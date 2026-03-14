#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "config.h"

namespace audio_bootloader {

enum class PacketState : uint8_t {
  kSyncing = 0,
  kDecoding,
  kOk,
  kErrorSync,
  kErrorCrc,
  kEndOfTransmission,
};

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t size);
uint32_t crc32_finish(uint32_t crc);
uint32_t crc32_calculate(const uint8_t* data, size_t size);

class FskDemodulator {
 public:
  void Init(uint32_t blank_threshold, uint32_t one_threshold);
  void Reset();
  void PushSample(bool sample);
  bool HasSymbol() const;
  uint8_t PopSymbol();

 private:
  std::array<uint8_t, 128> symbols_{};
  size_t read_index_ = 0;
  size_t write_index_ = 0;
  uint32_t blank_threshold_ = 0;
  uint32_t one_threshold_ = 0;
  uint32_t duration_ = 0;
  uint8_t swallow_ = 4;
  bool previous_sample_ = false;
};

class PacketDecoder {
 public:
  void Init();
  void Reset();
  PacketState ProcessSymbol(uint8_t symbol);
  const uint8_t* packet_data() const { return packet_.data(); }
  uint32_t packet_count() const { return packet_count_; }

 private:
  void ParseSyncHeader(uint8_t symbol);

  PacketState state_ = PacketState::kSyncing;
  uint8_t expected_symbols_ = 0xff;
  uint8_t preamble_remaining_ = kPreambleBits;
  uint16_t sync_blank_size_ = 0;
  uint8_t symbol_count_ = 0;
  uint16_t packet_size_ = 0;
  uint32_t packet_count_ = 0;
  std::array<uint8_t, kPacketDataWithCrc> packet_{};
};

}  // namespace audio_bootloader
