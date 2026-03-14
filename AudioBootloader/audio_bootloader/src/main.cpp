#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "RP2350.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "config.h"
#include "fsk_codec.h"
#include "ws2812_driver.h"

extern "C" {
#include "uzlib.h"
}

namespace audio_bootloader {

namespace {

constexpr RgbColor kBlue = {0, 0, 48};
constexpr RgbColor kGreen = {0, 48, 0};
constexpr RgbColor kCyan = {0, 28, 28};
constexpr RgbColor kYellow = {32, 24, 0};
constexpr RgbColor kOrange = {48, 18, 0};
constexpr RgbColor kPink = {38, 6, 18};
constexpr RgbColor kMagenta = {34, 0, 28};
constexpr RgbColor kRed = {48, 0, 0};
constexpr RgbColor kWhite = {24, 24, 24};
constexpr RgbColor kOff = {0, 0, 0};

uint32_t millis_now() {
  return to_ms_since_boot(get_absolute_time());
}

bool button_pressed() {
  return gpio_get(kButtonPin) == 0;
}

bool valid_cpu_hz(uint32_t cpu_hz) {
  return cpu_hz == 0u ||
         (cpu_hz >= kMinCpuHz && cpu_hz <= kMaxCpuHz && (cpu_hz % 1000u) == 0u);
}

uint32_t effective_cpu_hz(uint32_t cpu_hz) {
  return cpu_hz == 0u ? kDefaultCpuHz : cpu_hz;
}

void apply_cpu_hz_best_effort(uint32_t cpu_hz) {
  if (!valid_cpu_hz(cpu_hz)) {
    return;
  }
  const uint32_t target_hz = effective_cpu_hz(cpu_hz);
  if (clock_get_hz(clk_sys) == target_hz) {
    return;
  }
  set_sys_clock_khz(target_hz / 1000u, true);
}

void configure_button_and_mux() {
  gpio_init(kButtonPin);
  gpio_set_dir(kButtonPin, GPIO_IN);
  gpio_pull_up(kButtonPin);

  gpio_init(kMuxPin);
  gpio_set_dir(kMuxPin, GPIO_OUT);
  gpio_put(kMuxPin, 1);
}

uint16_t sample_pot1_blocking() {
  adc_init();
  adc_gpio_init(kPotAdcPin);
  gpio_put(kMuxPin, 1);
  adc_select_input(kPotAdcInput);
  uint32_t acc = 0;
  for (int i = 0; i < 8; ++i) {
    acc += adc_read();
  }
  return static_cast<uint16_t>(acc / 8u);
}

uint16_t pot_to_gain_q8(uint16_t pot) {
  const uint32_t min_gain = 64u;   // 0.25x
  const uint32_t max_gain = 1024u; // 4.0x
  return static_cast<uint16_t>(min_gain + ((max_gain - min_gain) * pot) / 4095u);
}

struct AppVectors {
  uint32_t stack_pointer;
  uint32_t reset_handler;
};

uint32_t header_cpu_hz(const ImageHeader& header) {
  return header.reserved[kHeaderCpuHzIndex];
}

uint32_t metadata_cpu_hz(const MetadataPage& metadata) {
  return metadata.reserved[kMetadataCpuHzIndex];
}

struct SignalMetrics {
  bool level = false;
  uint16_t envelope = 0;
  uint16_t peak = 0;
  LedQuality quality = LedQuality::kLow;
};

class AudioCapture {
 public:
  void Init() {
    instance_ = this;
    ConfigureAudioAdc();

    dma_channel_ = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_channel_);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, DREQ_ADC);
    dma_channel_configure(
        dma_channel_,
        &cfg,
        buffers_[0].data(),
        &adc_hw->fifo,
        kAudioBlockSamples,
        false);

    dma_channel_set_irq0_enabled(dma_channel_, true);
    irq_set_exclusive_handler(DMA_IRQ_0, DmaIrqHandler);
    irq_set_enabled(DMA_IRQ_0, true);
  }

  void Start() {
    ready_mask_ = 0;
    active_fill_ = 0;
    overrun_ = false;
    ConfigureAudioAdc();
    adc_fifo_drain();
    adc_run(true);
    dma_channel_set_write_addr(dma_channel_, buffers_[0].data(), false);
    dma_channel_set_trans_count(dma_channel_, kAudioBlockSamples, true);
    running_ = true;
  }

  void Stop() {
    running_ = false;
    adc_run(false);
    dma_channel_abort(dma_channel_);
    adc_fifo_drain();
  }

  bool GetNextBlock(const uint16_t*& block) {
    const uint32_t mask = ready_mask_;
    if (mask == 0) {
      return false;
    }
    const uint8_t index = (mask & 0x1u) ? 0u : 1u;
    ready_mask_ &= ~(1u << index);
    block = buffers_[index].data();
    return true;
  }

  bool overrun() const { return overrun_; }

 private:
  void ConfigureAudioAdc() {
    adc_init();
    adc_gpio_init(kAudioAdcPin);
    adc_select_input(kAudioAdcInput);
    adc_fifo_setup(
        true,
        true,
        1,
        false,
        false);
    adc_set_clkdiv(48000000.0f / static_cast<float>(kAudioSampleRate) - 1.0f);
  }

  static void DmaIrqHandler() {
    if (instance_ != nullptr) {
      instance_->OnDmaIrq();
    }
  }

  void OnDmaIrq() {
    dma_hw->ints0 = 1u << dma_channel_;
    if (ready_mask_ & (1u << active_fill_)) {
      overrun_ = true;
    }
    ready_mask_ |= (1u << active_fill_);
    active_fill_ ^= 1u;
    if (!running_) {
      return;
    }
    dma_channel_set_write_addr(dma_channel_, buffers_[active_fill_].data(), false);
    dma_channel_set_trans_count(dma_channel_, kAudioBlockSamples, true);
  }

  static inline AudioCapture* instance_ = nullptr;
  int dma_channel_ = -1;
  volatile uint32_t ready_mask_ = 0;
  volatile uint8_t active_fill_ = 0;
  volatile bool running_ = false;
  volatile bool overrun_ = false;
  alignas(4) std::array<std::array<uint16_t, kAudioBlockSamples>, 2> buffers_{};
};

class SignalConditioner {
 public:
  void Reset() {
    dc_q8_ = 2048 << 8;
    envelope_ = 0;
    peak_ = 0;
    level_ = false;
  }

  SignalMetrics Process(uint16_t sample, uint16_t gain_q8) {
    dc_q8_ += ((static_cast<int32_t>(sample) << 8) - dc_q8_) >> 6;
    const int32_t dc = dc_q8_ >> 8;
    const int32_t delta = static_cast<int32_t>(sample) - dc;
    const int32_t scaled_delta = (delta * static_cast<int32_t>(gain_q8)) >> 8;
    const uint16_t magnitude = static_cast<uint16_t>(std::min<int32_t>(std::abs(scaled_delta), 0x7fff));

    peak_ = std::max<uint16_t>(peak_ > 4 ? peak_ - 4 : 0, magnitude);
    envelope_ = std::max<uint16_t>(envelope_ > 1 ? envelope_ - 1 : 0, magnitude);

    const int32_t threshold = std::max<int32_t>(8, (static_cast<int32_t>(envelope_) * 15) / 100);

    if (scaled_delta > threshold) {
      level_ = true;
    } else if (scaled_delta < -threshold) {
      level_ = false;
    }

    LedQuality quality = LedQuality::kLow;
    if (magnitude >= kClipThreshold || envelope_ >= kWarnEnvelope * 2u) {
      quality = LedQuality::kClip;
    } else if (envelope_ >= kWarnEnvelope) {
      quality = LedQuality::kHigh;
    } else if (envelope_ >= kMinSignalEnvelope) {
      quality = LedQuality::kGood;
    }

    return {
        level_,
        envelope_,
        peak_,
        quality,
    };
  }

  uint16_t envelope() const { return envelope_; }

 private:
  int32_t dc_q8_ = 2048 << 8;
  uint16_t envelope_ = 0;
  uint16_t peak_ = 0;
  bool level_ = false;
};

class ImageWriter {
 public:
  enum class ConsumeResult : uint8_t {
    kOk = 0,
    kNeedsFlush,
    kError,
  };

  enum class Error : uint8_t {
    kNone = 0,
    kHeaderInvalid,
    kPayloadBounds,
    kIncomplete,
    kFlashStage,
    kInflateHeader,
    kInflateStream,
    kImageVerify,
    kMetadataWrite,
  };

  bool Begin(const ImageHeader& header) {
    error_ = Error::kNone;
    if (header.magic0 != kImageHeaderMagic || header.magic1 != kImageHeaderMagic2) {
      return Fail(Error::kHeaderInvalid);
    }
    if (header.version != kImageHeaderVersion || header.header_size != sizeof(ImageHeader)) {
      return Fail(Error::kHeaderInvalid);
    }
    if (header.app_base != kAppBaseAddress || header.vector_offset != kAppVectorOffset) {
      return Fail(Error::kHeaderInvalid);
    }
    if (header.image_size == 0 || header.image_size > kAppSlotSize) {
      return Fail(Error::kHeaderInvalid);
    }
    if ((header.image_size % kFlashPageSize) != 0) {
      return Fail(Error::kHeaderInvalid);
    }
    if ((header.flags & ~kImageFlagCompressedZlib) != 0) {
      return Fail(Error::kHeaderInvalid);
    }
    if (!valid_cpu_hz(header_cpu_hz(header))) {
      return Fail(Error::kHeaderInvalid);
    }

    header_ = header;
    if (PayloadSize() == 0 || (PayloadSize() % kFlashPageSize) != 0) {
      return Fail(Error::kHeaderInvalid);
    }
    if (EncodedSize() == 0 || EncodedSize() > PayloadSize()) {
      return Fail(Error::kHeaderInvalid);
    }
    bytes_received_ = 0;
    sector_offset_ = 0;
    stage_used_ = 0;
    started_ = true;
    metadata_invalidated_ = false;
    buffered_mode_ = PayloadSize() <= kBufferedPayloadLimit;
    if (compressed() && !buffered_mode_) {
      return Fail(Error::kHeaderInvalid);
    }
    std::fill(stage_.begin(), stage_.end(), 0xff);
    return true;
  }

  ConsumeResult ConsumePacket(const uint8_t* packet) {
    if (!started_ || bytes_received_ >= PayloadSize()) {
      error_ = Error::kPayloadBounds;
      return ConsumeResult::kError;
    }

    if (buffered_mode_) {
      if ((bytes_received_ + kPacketSize) > payload_buffer_.size() ||
          (bytes_received_ + kPacketSize) > PayloadSize()) {
        error_ = Error::kPayloadBounds;
        return ConsumeResult::kError;
      }
      std::memcpy(payload_buffer_.data() + bytes_received_, packet, kPacketSize);
      bytes_received_ += kPacketSize;
      return ConsumeResult::kOk;
    }

    size_t remaining = kPacketSize;
    const uint8_t* data = packet;
    while (remaining > 0) {
      const size_t room = kFlashSectorSize - stage_used_;
      const size_t chunk = std::min(room, remaining);
      std::memcpy(stage_.data() + stage_used_, data, chunk);
      stage_used_ += chunk;
      bytes_received_ += chunk;
      data += chunk;
      remaining -= chunk;
      if (bytes_received_ > PayloadSize()) {
        error_ = Error::kPayloadBounds;
        return ConsumeResult::kError;
      }
    }
    return stage_used_ == kFlashSectorSize ? ConsumeResult::kNeedsFlush : ConsumeResult::kOk;
  }

  bool complete() const {
    return started_ && bytes_received_ == PayloadSize();
  }

  bool FlushFullSector() {
    if (stage_used_ != kFlashSectorSize) {
      return false;
    }
    return FlushSector(kFlashSectorSize);
  }

  bool Finalize() {
    if (!complete()) {
      return Fail(Error::kIncomplete);
    }
    if (buffered_mode_) {
      if (compressed()) {
        if (!ProgramCompressedBufferedPayload()) {
          return false;
        }
      } else if (!ProgramBufferedPayload()) {
        return false;
      }
    } else {
      if (stage_used_ > 0 && !FlushSector(stage_used_)) {
        return false;
      }
    }
    if (!VerifyWrittenImage()) {
      return Fail(Error::kImageVerify);
    }
    return WriteMetadata();
  }

  const ImageHeader& header() const { return header_; }
  Error error() const { return error_; }

 private:
  bool Fail(Error error) {
    error_ = error;
    return false;
  }

  bool compressed() const {
    return (header_.flags & kImageFlagCompressedZlib) != 0;
  }

  uint32_t PayloadSize() const {
    if (compressed()) {
      return header_.reserved[kHeaderPayloadSizeIndex];
    }
    return header_.image_size;
  }

  uint32_t EncodedSize() const {
    if (compressed()) {
      return header_.reserved[kHeaderEncodedSizeIndex];
    }
    return header_.image_size;
  }

  uint32_t CpuHz() const {
    return header_cpu_hz(header_);
  }

  bool ProgramBufferedPayload() {
    if (!metadata_invalidated_) {
      InvalidateMetadata();
      metadata_invalidated_ = true;
    }

    const uint32_t sector_count = (header_.image_size + kFlashSectorSize - 1u) / kFlashSectorSize;
    for (uint32_t sector = 0; sector < sector_count; ++sector) {
      const uint32_t flash_offset = kAppSlotOffset + (sector * kFlashSectorSize);
      const uint32_t erase_irq_state = save_and_disable_interrupts();
      flash_range_erase(flash_offset, kFlashSectorSize);
      restore_interrupts(erase_irq_state);

      const uint32_t source_offset = sector * kFlashSectorSize;
      const uint32_t remaining = header_.image_size - source_offset;
      const uint32_t program_bytes = std::min<uint32_t>(kFlashSectorSize, remaining);
      for (uint32_t offset = 0; offset < program_bytes; offset += kFlashPageSize) {
        const uint32_t page_irq_state = save_and_disable_interrupts();
        flash_range_program(
            flash_offset + offset,
            payload_buffer_.data() + source_offset + offset,
            kFlashPageSize);
        restore_interrupts(page_irq_state);
      }
    }
    return true;
  }

  bool ProgramCompressedBufferedPayload() {
    if (!metadata_invalidated_) {
      InvalidateMetadata();
      metadata_invalidated_ = true;
    }

    uzlib_init();
    std::fill(inflate_dict_.begin(), inflate_dict_.end(), 0);
    uzlib_uncomp uncomp{};
    uzlib_uncompress_init(&uncomp, inflate_dict_.data(), inflate_dict_.size());
    uncomp.source = payload_buffer_.data();
    uncomp.source_limit = payload_buffer_.data() + EncodedSize();
    uncomp.source_read_cb = nullptr;

    // uzlib returns the zlib window bits here, not TINF_OK, so any non-negative
    // value is a valid header parse.
    if (uzlib_zlib_parse_header(&uncomp) < 0) {
      return Fail(Error::kInflateHeader);
    }

    stage_used_ = 0;
    sector_offset_ = 0;
    std::fill(stage_.begin(), stage_.end(), 0xff);

    uint32_t remaining_output = header_.image_size;
    int res = TINF_OK;
    while (remaining_output > 0) {
      const uint32_t room = kFlashSectorSize - stage_used_;
      const uint32_t chunk = std::min<uint32_t>(room, remaining_output);
      uncomp.dest_start = stage_.data();
      uncomp.dest = stage_.data() + stage_used_;
      uncomp.dest_limit = uncomp.dest + chunk;
      res = uzlib_uncompress_chksum(&uncomp);
      if (res != TINF_OK && res != TINF_DONE) {
        return Fail(Error::kInflateStream);
      }
      const uint32_t produced = static_cast<uint32_t>(uncomp.dest - (stage_.data() + stage_used_));
      if (produced != chunk) {
        return Fail(Error::kInflateStream);
      }
      stage_used_ += produced;
      remaining_output -= produced;
      if (stage_used_ == kFlashSectorSize) {
        if (!FlushSector(kFlashSectorSize)) {
          return false;
        }
      }
    }

    if (stage_used_ > 0 && !FlushSector(stage_used_)) {
      return false;
    }

    if (res == TINF_OK) {
      // If the last inflate call filled the output window exactly, uzlib may
      // not surface TINF_DONE until we give it one more zero-output step to
      // consume the stream trailer and verify the checksum.
      uncomp.dest_start = stage_.data();
      uncomp.dest = stage_.data();
      uncomp.dest_limit = stage_.data();
      res = uzlib_uncompress_chksum(&uncomp);
    }

    if (res != TINF_DONE) {
      return Fail(Error::kInflateStream);
    }
    return true;
  }

  bool FlushSector(size_t program_bytes) {
    if ((program_bytes % kFlashPageSize) != 0 || program_bytes > kFlashSectorSize) {
      return Fail(Error::kFlashStage);
    }
    if (!metadata_invalidated_) {
      InvalidateMetadata();
      metadata_invalidated_ = true;
    }
    const uint32_t flash_offset = kAppSlotOffset + sector_offset_;
    const uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(flash_offset, kFlashSectorSize);
    restore_interrupts(irq_state);

    for (size_t offset = 0; offset < program_bytes; offset += kFlashPageSize) {
      const uint32_t page_irq_state = save_and_disable_interrupts();
      flash_range_program(
          flash_offset + static_cast<uint32_t>(offset),
          stage_.data() + offset,
          kFlashPageSize);
      restore_interrupts(page_irq_state);
    }

    sector_offset_ += kFlashSectorSize;
    stage_used_ = 0;
    std::fill(stage_.begin(), stage_.end(), 0xff);
    return true;
  }

  bool VerifyWrittenImage() const {
    const auto* flash_image = reinterpret_cast<const uint8_t*>(kAppBaseAddress);
    const uint32_t flash_crc = crc32_calculate(flash_image, header_.image_size);
    return flash_crc == header_.image_crc32;
  }

  void InvalidateMetadata() {
    const uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(kMetadataOffset, kFlashSectorSize);
    restore_interrupts(irq_state);
  }

  bool WriteMetadata() {
    MetadataPage metadata{};
    metadata.magic = kMetadataMagic;
    metadata.version = kMetadataVersion;
    metadata.image_size = header_.image_size;
    metadata.image_crc32 = header_.image_crc32;
    metadata.app_base = header_.app_base;
    metadata.vector_offset = header_.vector_offset;
    metadata.flags = header_.flags & ~kImageFlagCompressedZlib;
    metadata.reserved[kMetadataCpuHzIndex] = CpuHz();
    metadata.checksum = crc32_calculate(reinterpret_cast<const uint8_t*>(&metadata), offsetof(MetadataPage, checksum));

    const uint32_t erase_irq_state = save_and_disable_interrupts();
    flash_range_erase(kMetadataOffset, kFlashSectorSize);
    restore_interrupts(erase_irq_state);

    const uint32_t program_irq_state = save_and_disable_interrupts();
    flash_range_program(
        kMetadataOffset,
        reinterpret_cast<const uint8_t*>(&metadata),
        sizeof(MetadataPage));
    restore_interrupts(program_irq_state);

    MetadataPage written{};
    const auto* stored = reinterpret_cast<const MetadataPage*>(kMetadataAddress);
    std::memcpy(&written, stored, sizeof(MetadataPage));
    if (std::memcmp(&written, &metadata, sizeof(MetadataPage)) != 0) {
      return Fail(Error::kMetadataWrite);
    }
    return true;
  }

  bool started_ = false;
  bool buffered_mode_ = false;
  ImageHeader header_{};
  uint32_t bytes_received_ = 0;
  uint32_t sector_offset_ = 0;
  uint32_t stage_used_ = 0;
  bool metadata_invalidated_ = false;
  Error error_ = Error::kNone;
  std::array<uint8_t, kFlashSectorSize> stage_{};
  std::array<uint8_t, 32768> inflate_dict_{};
  std::array<uint8_t, kBufferedPayloadLimit> payload_buffer_{};
};

bool LoadMetadata(MetadataPage* metadata) {
  const auto* stored = reinterpret_cast<const MetadataPage*>(kMetadataAddress);
  std::memcpy(metadata, stored, sizeof(MetadataPage));
  if (metadata->magic != kMetadataMagic || metadata->version != kMetadataVersion) {
    return false;
  }
  const uint32_t expected = crc32_calculate(
      reinterpret_cast<const uint8_t*>(metadata),
      offsetof(MetadataPage, checksum));
  return expected == metadata->checksum;
}

bool ValidateVectors(const MetadataPage& metadata, AppVectors* vectors) {
  if (metadata.app_base != kAppBaseAddress || metadata.vector_offset != kAppVectorOffset) {
    return false;
  }
  if (metadata.image_size == 0 || metadata.image_size > kAppSlotSize) {
    return false;
  }
  if (!valid_cpu_hz(metadata_cpu_hz(metadata))) {
    return false;
  }
  const auto* table = reinterpret_cast<const AppVectors*>(metadata.app_base + metadata.vector_offset);
  std::memcpy(vectors, table, sizeof(AppVectors));
  if (vectors->stack_pointer < kRamStart || vectors->stack_pointer > kRamEnd) {
    return false;
  }
  if (vectors->reset_handler < metadata.app_base ||
      vectors->reset_handler >= (metadata.app_base + metadata.image_size)) {
    return false;
  }
  return true;
}

[[noreturn]] void JumpToApplication(const MetadataPage& metadata) {
  AppVectors vectors{};
  if (!ValidateVectors(metadata, &vectors)) {
    reset_usb_boot(0, 0);
    while (true) {
    }
  }

  apply_cpu_hz_best_effort(metadata_cpu_hz(metadata));

  __disable_irq();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;
  SCB->VTOR = metadata.app_base + metadata.vector_offset;
  __DSB();
  __ISB();

  using EntryFn = void (*)();
  __set_MSP(vectors.stack_pointer);
  const EntryFn entry = reinterpret_cast<EntryFn>(vectors.reset_handler);
  entry();
  while (true) {
  }
}

class BootloaderApp {
 public:
  void Run() {
    configure_button_and_mux();
    led_.Init(pio0, 0, kLedPin);

    MetadataPage metadata{};
    if (!button_pressed() && LoadMetadata(&metadata) && ValidateVectors(metadata, &app_vectors_)) {
      JumpToApplication(metadata);
    }

    capture_.Init();
    conditioner_.Reset();
    demod_.Init(kOneBlankThreshold, kZeroOneThreshold);
    packet_decoder_.Init();

    pot_gain_q8_ = pot_to_gain_q8(sample_pot1_blocking());
    capture_.Start();

    while (true) {
      PollButton();
      PollPotWhenIdle();
      PumpAudio();
      UpdateLed();

      if (state_ == BootState::kSuccess && success_blinks_done_ >= kSuccessBlinkCount * 2u) {
        MetadataPage done{};
        if (LoadMetadata(&done)) {
          JumpToApplication(done);
        }
        EnterError(ErrorReason::kMetadata);
      }

      tight_loop_contents();
    }
  }

 private:
  void PollButton() {
    const bool pressed = button_pressed();
    const uint32_t now = millis_now();
    if (pressed != last_button_raw_) {
      last_button_change_ms_ = now;
      last_button_raw_ = pressed;
    }
    if ((now - last_button_change_ms_) < kButtonDebounceMs) {
      return;
    }
    if (pressed && !button_latched_) {
      button_latched_ = true;
      if (state_ == BootState::kError) {
        RebootToWaitingState();
      }
    } else if (!pressed) {
      button_latched_ = false;
    }
  }

  void PollPotWhenIdle() {
    if (state_ == BootState::kReceiving) {
      return;
    }
    const uint32_t now = millis_now();
    if ((now - last_pot_sample_ms_) < kPotReadPeriodMs) {
      return;
    }
    if (conditioner_.envelope() >= kMinSignalEnvelope) {
      return;
    }
    last_pot_sample_ms_ = now;
    capture_.Stop();
    sleep_us(kPotReadSettleUs);
    pot_gain_q8_ = pot_to_gain_q8(sample_pot1_blocking());
    capture_.Start();
  }

  void MaybePollPotDuringReceive() {
    if (state_ != BootState::kReceiving) {
      return;
    }
    if (blank_symbol_run_ < kPotReadBlankSymbols || pot_sampled_this_blank_) {
      return;
    }
    const uint32_t now = millis_now();
    if ((now - last_pot_sample_ms_) < kPotReadPeriodMs) {
      return;
    }

    capture_.Stop();
    sleep_us(kPotReadSettleUs);
    pot_gain_q8_ = pot_to_gain_q8(sample_pot1_blocking());
    last_pot_sample_ms_ = now;
    capture_.Start();
    pot_sampled_this_blank_ = true;
  }

  void PumpAudio() {
    if (state_ == BootState::kError || state_ == BootState::kSuccess) {
      return;
    }
    const uint16_t* block = nullptr;
    while (capture_.GetNextBlock(block)) {
      ProcessBlock(block);
      if (state_ == BootState::kError || state_ == BootState::kSuccess) {
        return;
      }
      MaybePollPotDuringReceive();
    }
    if (capture_.overrun()) {
      EnterError(ErrorReason::kCaptureOverrun);
    }
  }

  void ProcessBlock(const uint16_t* block) {
    for (uint32_t i = 0; i < kAudioBlockSamples; ++i) {
      metrics_ = conditioner_.Process(block[i], pot_gain_q8_);
      demod_.PushSample(metrics_.level);
      while (demod_.HasSymbol()) {
        const uint8_t symbol = demod_.PopSymbol();
        TrackReceiveBlank(symbol);
        const PacketState packet_state = packet_decoder_.ProcessSymbol(symbol);
        switch (packet_state) {
          case PacketState::kOk:
            if (!HandlePacket(packet_decoder_.packet_data())) {
              if (header_received_) {
                EnterError(MapWriterError(writer_.error()));
                return;
              }
              ResetDecoderForNextPacket();
              break;
            }
            packet_decoder_.Reset();
            break;
          case PacketState::kEndOfTransmission:
            capture_.Stop();
            if (!writer_.complete()) {
              EnterError(ErrorReason::kPayload);
            } else if (writer_.Finalize()) {
              EnterSuccess();
            } else {
              EnterError(MapWriterError(writer_.error()));
            }
            return;
          case PacketState::kErrorSync:
            if (header_received_) {
              EnterError(ErrorReason::kPacketSync);
              return;
            }
            ResetDecoderForNextPacket();
            break;
          case PacketState::kErrorCrc:
            if (header_received_) {
              EnterError(ErrorReason::kPacketCrc);
              return;
            }
            ResetDecoderForNextPacket();
            break;
          case PacketState::kSyncing:
          case PacketState::kDecoding:
            break;
        }
      }
    }
  }

  bool HandlePacket(const uint8_t* packet) {
    if (!header_received_) {
      ImageHeader header{};
      std::memcpy(&header, packet, sizeof(header));
      if (!writer_.Begin(header)) {
        return false;
      }
      header_received_ = true;
      state_ = BootState::kReceiving;
      return true;
    }
    const ImageWriter::ConsumeResult result = writer_.ConsumePacket(packet);
    switch (result) {
      case ImageWriter::ConsumeResult::kOk:
        return true;
      case ImageWriter::ConsumeResult::kNeedsFlush:
        return FlushSectorDuringGap();
      case ImageWriter::ConsumeResult::kError:
        return false;
    }
    return false;
  }

  void EnterSuccess() {
    state_ = BootState::kSuccess;
    capture_.Stop();
    error_reason_ = ErrorReason::kNone;
    success_phase_on_ = false;
    success_blinks_done_ = 0;
    last_success_toggle_ms_ = millis_now();
  }

  void EnterError(ErrorReason reason) {
    state_ = BootState::kError;
    capture_.Stop();
    error_reason_ = reason;
    error_on_ = false;
    last_error_toggle_ms_ = millis_now();
  }

  [[noreturn]] void RebootToWaitingState() {
    capture_.Stop();
    led_.Set(kBlue);
    sleep_ms(10);
    watchdog_reboot(0, 0, 1);
    while (true) {
      tight_loop_contents();
    }
  }

  bool FlushSectorDuringGap() {
    capture_.Stop();
    const bool ok = writer_.FlushFullSector();
    ResetDecoderForNextPacket();
    conditioner_.Reset();
    metrics_ = {};
    if (ok) {
      capture_.Start();
    }
    return ok;
  }

  void ResetDecoderForNextPacket() {
    demod_.Reset();
    packet_decoder_.Reset();
    blank_symbol_run_ = 0;
    pot_sampled_this_blank_ = false;
    if (!header_received_) {
      state_ = BootState::kWaitingForAudio;
    }
  }

  void TrackReceiveBlank(uint8_t symbol) {
    if (symbol == 2) {
      if (blank_symbol_run_ < 0xffffu) {
        ++blank_symbol_run_;
      }
      return;
    }
    blank_symbol_run_ = 0;
    pot_sampled_this_blank_ = false;
  }

  void UpdateLed() {
    const uint32_t now = millis_now();
    if ((now - last_led_update_ms_) < kLedUpdatePeriodMs) {
      return;
    }
    last_led_update_ms_ = now;

    switch (state_) {
      case BootState::kWaitingForAudio:
        if (metrics_.envelope < kMinSignalEnvelope) {
          const bool on = ((now / kWaitBlinkPeriodMs) & 1u) == 0u;
          led_.Set(on ? kBlue : kOff);
        } else {
          led_.Set(RenderVuColor(metrics_));
        }
        break;
      case BootState::kReceiving:
        led_.Set(RenderVuColor(metrics_));
        break;
      case BootState::kSuccess:
        if ((now - last_success_toggle_ms_) >= kSuccessBlinkPeriodMs) {
          last_success_toggle_ms_ = now;
          success_phase_on_ = !success_phase_on_;
          ++success_blinks_done_;
        }
        led_.Set(success_phase_on_ ? kGreen : kOff);
        break;
      case BootState::kError:
        if (error_reason_ == ErrorReason::kInflate) {
          const bool show_red = ((now / kInflateAlternatePeriodMs) & 1u) == 0u;
          led_.Set(show_red ? kRed : kBlue);
          break;
        }
        if ((now - last_error_toggle_ms_) >= kErrorBlinkPeriodMs) {
          last_error_toggle_ms_ = now;
          error_on_ = !error_on_;
        }
        led_.Set(error_on_ ? RenderErrorColor(error_reason_) : kOff);
        break;
    }
  }

  RgbColor RenderVuColor(const SignalMetrics& metrics) const {
    float brightness = static_cast<float>(std::min<uint32_t>(metrics.envelope, kTargetEnvelope)) /
                       static_cast<float>(kTargetEnvelope);
    brightness = std::clamp(brightness, 0.08f, 1.0f);

    switch (metrics.quality) {
      case LedQuality::kLow:
        return ScaleColor(kBlue, brightness);
      case LedQuality::kGood:
        return ScaleColor(kGreen, brightness);
      case LedQuality::kHigh:
        return ScaleColor(kOrange, brightness);
      case LedQuality::kClip:
        return ScaleColor(kRed, brightness);
    }
    return kOff;
  }

  static ErrorReason MapWriterError(ImageWriter::Error error) {
    switch (error) {
      case ImageWriter::Error::kHeaderInvalid:
        return ErrorReason::kHeader;
      case ImageWriter::Error::kPayloadBounds:
      case ImageWriter::Error::kIncomplete:
        return ErrorReason::kPayload;
      case ImageWriter::Error::kFlashStage:
        return ErrorReason::kFlashStage;
      case ImageWriter::Error::kInflateHeader:
      case ImageWriter::Error::kInflateStream:
        return ErrorReason::kInflate;
      case ImageWriter::Error::kImageVerify:
        return ErrorReason::kImageVerify;
      case ImageWriter::Error::kMetadataWrite:
        return ErrorReason::kMetadata;
      case ImageWriter::Error::kNone:
        return ErrorReason::kHeader;
    }
    return ErrorReason::kHeader;
  }

  static RgbColor RenderErrorColor(ErrorReason reason) {
    switch (reason) {
      case ErrorReason::kCaptureOverrun:
        return kBlue;
      case ErrorReason::kPacketSync:
        return kRed;
      case ErrorReason::kPacketCrc:
        return kMagenta;
      case ErrorReason::kHeader:
        return kYellow;
      case ErrorReason::kPayload:
        return kOrange;
      case ErrorReason::kFlashStage:
        return kPink;
      case ErrorReason::kInflate:
        return kRed;
      case ErrorReason::kImageVerify:
        return kCyan;
      case ErrorReason::kMetadata:
        return kWhite;
      case ErrorReason::kNone:
        return kRed;
    }
    return kRed;
  }

  Ws2812Driver led_{};
  AudioCapture capture_{};
  SignalConditioner conditioner_{};
  FskDemodulator demod_{};
  PacketDecoder packet_decoder_{};
  ImageWriter writer_{};

  BootState state_ = BootState::kWaitingForAudio;
  ErrorReason error_reason_ = ErrorReason::kNone;
  AppVectors app_vectors_{};
  SignalMetrics metrics_{};

  bool header_received_ = false;
  bool last_button_raw_ = false;
  bool button_latched_ = false;
  bool error_on_ = false;
  bool success_phase_on_ = false;
  uint16_t pot_gain_q8_ = 256;
  uint16_t blank_symbol_run_ = 0;
  uint32_t last_button_change_ms_ = 0;
  uint32_t last_pot_sample_ms_ = 0;
  uint32_t last_led_update_ms_ = 0;
  uint32_t last_error_toggle_ms_ = 0;
  uint32_t last_success_toggle_ms_ = 0;
  uint32_t success_blinks_done_ = 0;
  bool pot_sampled_this_blank_ = false;
};

}  // namespace

}  // namespace audio_bootloader

int main() {
  static audio_bootloader::BootloaderApp app;
  app.Run();
  return 0;
}
