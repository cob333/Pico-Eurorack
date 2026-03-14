#pragma once

#include <cstddef>
#include <cstdint>

namespace audio_bootloader {

constexpr uint32_t kButtonPin = 2;
constexpr uint32_t kLedPin = 3;
constexpr uint32_t kMuxPin = 5;
constexpr uint32_t kAudioAdcPin = 26;
constexpr uint32_t kAudioAdcInput = 0;
constexpr uint32_t kPotAdcPin = 29;
constexpr uint32_t kPotAdcInput = 3;

constexpr uint32_t kFlashBase = 0x10000000u;
constexpr uint32_t kBootloaderOffset = 0x00000000u;
constexpr uint32_t kBootloaderSize = 0x00020000u;
constexpr uint32_t kAppSlotOffset = 0x00020000u;
constexpr uint32_t kAppSlotSize = 0x00200000u;
constexpr uint32_t kMetadataOffset = 0x00220000u;
constexpr uint32_t kMetadataSize = 0x00010000u;

constexpr uint32_t kAppBaseAddress = kFlashBase + kAppSlotOffset;
constexpr uint32_t kMetadataAddress = kFlashBase + kMetadataOffset;
constexpr uint32_t kAppVectorOffset = 0x00003000u;

constexpr uint32_t kFlashSectorSize = 4096u;
constexpr uint32_t kFlashPageSize = 256u;
constexpr uint32_t kPacketSize = 256u;
constexpr uint32_t kPacketDataWithCrc = kPacketSize + 4u;
constexpr uint32_t kBufferedPayloadLimit = 256u * 1024u;

constexpr uint32_t kAudioSampleRate = 48000u;
constexpr uint32_t kAudioBlockSamples = 256u;

constexpr uint32_t kZeroPeriod = 9u;
constexpr uint32_t kOnePeriod = 18u;
constexpr uint32_t kBlankPeriod = 54u;

constexpr uint32_t kZeroOneThreshold = 13u;
constexpr uint32_t kOneBlankThreshold = 36u;

constexpr uint32_t kPreambleBits = 32u;
constexpr uint32_t kMaxBlankSymbols = 500u;

constexpr uint32_t kImageHeaderMagic = 0x32485041u;  // "2HPA"
constexpr uint32_t kImageHeaderMagic2 = 0x5544424Cu; // "UDBL"
constexpr uint32_t kMetadataMagic = 0x3248504Du;     // "2HPM"

constexpr uint32_t kImageHeaderVersion = 1u;
constexpr uint32_t kMetadataVersion = 1u;
constexpr uint32_t kImageFlagCompressedZlib = 1u << 0;
constexpr uint32_t kDefaultCpuHz = 150000000u;
constexpr uint32_t kMinCpuHz = 50000000u;
constexpr uint32_t kMaxCpuHz = 300000000u;
constexpr uint32_t kHeaderPayloadSizeIndex = 0u;
constexpr uint32_t kHeaderEncodedSizeIndex = 1u;
constexpr uint32_t kHeaderCpuHzIndex = 2u;
constexpr uint32_t kMetadataCpuHzIndex = 0u;

constexpr uint32_t kWaitBlinkPeriodMs = 500u;
constexpr uint32_t kErrorBlinkPeriodMs = 250u;
constexpr uint32_t kInflateAlternatePeriodMs = 500u;
constexpr uint32_t kLedUpdatePeriodMs = 33u;
constexpr uint32_t kSuccessBlinkCount = 3u;
constexpr uint32_t kSuccessBlinkPeriodMs = 180u;

constexpr uint32_t kMinSignalEnvelope = 24u;
constexpr uint32_t kTargetEnvelope = 220u;
constexpr uint32_t kWarnEnvelope = 420u;
constexpr uint32_t kClipThreshold = 4000u;

constexpr uint32_t kPotReadPeriodMs = 200u;
constexpr uint32_t kButtonDebounceMs = 25u;
constexpr uint32_t kPotReadSettleUs = 120u;
constexpr uint32_t kPotReadBlankSymbols = 8u;

constexpr uint32_t kRamStart = 0x20000000u;
constexpr uint32_t kRamEnd = 0x20082000u;
constexpr uint32_t kFlashEnd = kFlashBase + 0x00400000u;

struct ImageHeader {
  uint32_t magic0;
  uint32_t magic1;
  uint32_t version;
  uint32_t header_size;
  uint32_t image_size;
  uint32_t image_crc32;
  uint32_t app_base;
  uint32_t vector_offset;
  uint32_t flags;
  uint32_t reserved[55];
};

static_assert(sizeof(ImageHeader) == kPacketSize, "Header must fit in one packet");

struct MetadataPage {
  uint32_t magic;
  uint32_t version;
  uint32_t image_size;
  uint32_t image_crc32;
  uint32_t app_base;
  uint32_t vector_offset;
  uint32_t flags;
  uint32_t checksum;
  uint32_t reserved[56];
};

static_assert(sizeof(MetadataPage) == kPacketSize, "Metadata page must be one flash page");

enum class BootState : uint8_t {
  kWaitingForAudio = 0,
  kReceiving,
  kSuccess,
  kError,
};

enum class ErrorReason : uint8_t {
  kNone = 0,
  kCaptureOverrun,
  kPacketSync,
  kPacketCrc,
  kHeader,
  kPayload,
  kFlashStage,
  kInflate,
  kImageVerify,
  kMetadata,
};

enum class LedQuality : uint8_t {
  kLow = 0,
  kGood,
  kHigh,
  kClip,
};

}  // namespace audio_bootloader
