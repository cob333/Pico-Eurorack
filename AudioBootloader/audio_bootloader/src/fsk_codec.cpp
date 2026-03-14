#include "fsk_codec.h"

namespace audio_bootloader {

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return crc;
}

uint32_t crc32_finish(uint32_t crc) {
  return ~crc;
}

uint32_t crc32_calculate(const uint8_t* data, size_t size) {
  return crc32_finish(crc32_update(0xffffffffu, data, size));
}

void FskDemodulator::Init(uint32_t blank_threshold, uint32_t one_threshold) {
  blank_threshold_ = blank_threshold;
  one_threshold_ = one_threshold;
  Reset();
}

void FskDemodulator::Reset() {
  read_index_ = 0;
  write_index_ = 0;
  duration_ = 0;
  swallow_ = 4;
  previous_sample_ = false;
}

void FskDemodulator::PushSample(bool sample) {
  if (sample == previous_sample_) {
    ++duration_;
    return;
  }

  previous_sample_ = sample;
  uint8_t symbol = 0;
  if (duration_ >= blank_threshold_) {
    symbol = 2;
  } else if (duration_ >= one_threshold_) {
    symbol = 1;
  } else {
    symbol = 0;
  }

  if (swallow_ > 0) {
    symbol = 2;
    --swallow_;
  }

  const size_t next = (write_index_ + 1) % symbols_.size();
  if (next != read_index_) {
    symbols_[write_index_] = symbol;
    write_index_ = next;
  }
  duration_ = 0;
}

bool FskDemodulator::HasSymbol() const {
  return read_index_ != write_index_;
}

uint8_t FskDemodulator::PopSymbol() {
  const uint8_t symbol = symbols_[read_index_];
  read_index_ = (read_index_ + 1) % symbols_.size();
  return symbol;
}

void PacketDecoder::Init() {
  packet_count_ = 0;
  Reset();
}

void PacketDecoder::Reset() {
  state_ = PacketState::kSyncing;
  expected_symbols_ = 0xff;
  preamble_remaining_ = kPreambleBits;
  sync_blank_size_ = 0;
  symbol_count_ = 0;
  packet_size_ = 0;
  packet_.fill(0);
}

void PacketDecoder::ParseSyncHeader(uint8_t symbol) {
  if (((1u << symbol) & expected_symbols_) == 0) {
    state_ = PacketState::kErrorSync;
    return;
  }

  switch (symbol) {
    case 2:
      ++sync_blank_size_;
      if (sync_blank_size_ >= kMaxBlankSymbols && packet_count_ > 0) {
        state_ = PacketState::kEndOfTransmission;
        return;
      }
      expected_symbols_ = (1u << 0) | (1u << 1) | (1u << 2);
      preamble_remaining_ = kPreambleBits;
      break;
    case 1:
      expected_symbols_ = (1u << 0);
      if (preamble_remaining_ > 0) {
        --preamble_remaining_;
      }
      break;
    case 0:
      expected_symbols_ = (1u << 1);
      if (preamble_remaining_ > 0) {
        --preamble_remaining_;
      }
      break;
  }

  if (preamble_remaining_ == 0) {
    state_ = PacketState::kDecoding;
    packet_size_ = 0;
    symbol_count_ = 0;
    packet_.fill(0);
  }
}

PacketState PacketDecoder::ProcessSymbol(uint8_t symbol) {
  switch (state_) {
    case PacketState::kSyncing:
      ParseSyncHeader(symbol);
      break;
    case PacketState::kDecoding:
      if (symbol == 2) {
        state_ = PacketState::kErrorSync;
        break;
      }
      packet_[packet_size_] |= symbol;
      ++symbol_count_;
      if (symbol_count_ == 8) {
        symbol_count_ = 0;
        ++packet_size_;
        if (packet_size_ == kPacketDataWithCrc) {
          const uint32_t actual_crc = crc32_calculate(packet_.data(), kPacketSize);
          const uint32_t expected_crc =
              (static_cast<uint32_t>(packet_[kPacketSize + 0]) << 24) |
              (static_cast<uint32_t>(packet_[kPacketSize + 1]) << 16) |
              (static_cast<uint32_t>(packet_[kPacketSize + 2]) << 8) |
              (static_cast<uint32_t>(packet_[kPacketSize + 3]) << 0);
          state_ = (actual_crc == expected_crc) ? PacketState::kOk : PacketState::kErrorCrc;
          ++packet_count_;
        } else {
          packet_[packet_size_] = 0;
        }
      } else {
        packet_[packet_size_] <<= 1;
      }
      break;
    case PacketState::kOk:
    case PacketState::kErrorSync:
    case PacketState::kErrorCrc:
    case PacketState::kEndOfTransmission:
      break;
  }
  return state_;
}

}  // namespace audio_bootloader
