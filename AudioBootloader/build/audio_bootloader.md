## Layout

- `bootloader/audio_bootloader/`: RP2350 bootloader firmware built with pico-sdk
- `tools/audio_bootloader/make_audio_wav.py`: Python 3 host tool that compiles a sketch for the fixed app slot and emits an FSK `.wav`
- `tools/audio_bootloader/render_linker.py`: helper that rewrites the Arduino-Pico linker script to target the app slot
- `tools/audio_bootloader/fsk_boundary_test.py`: offline timing-margin sweep for FSK profile tuning

## Fixed Flash Map

- Bootloader: `0x10000000 - 0x1001FFFF`
- Application slot: `0x10020000 - 0x1021FFFF`
- Metadata sector: `0x10220000 - 0x1022FFFF`

The app build keeps Arduino-Pico's normal section layout, including its `.ota` and `.partition` sections. The bootloader therefore jumps to the relocated application vector table at `APP_BASE + 0x3000`.

## Front Panel Behavior

- Hold `BUTTON1` while powering on to force bootloader mode
- Idle import mode: LED blinks blue
- Signal present: LED becomes a single-pixel VU indicator
- Low level: blue
- Good level: green
- High level: orange
- Clipping or too hot: red
- Success: blink green 3 times, then jump to the application
- Failure: blink a color-coded error until the button is pressed again
- Press the button after a failure to reset back to the initial blue waiting state before the next transfer

Error colors:

- Blue: ADC/DMA capture overrun
- Red: packet sync lost after a valid header
- Magenta: packet CRC failure after a valid header
- Yellow: invalid image header or payload length mismatch
- Orange: payload incomplete at end-of-transmission
- Pink: flash staging/alignment failure during sector programming
- Alternating red/blue every 0.5 s: zlib inflate or compressed-stream finalize failure
- Cyan: written app image CRC does not match the header
- White: metadata writeback or post-write metadata reload failure

`Pot1` is converted into a software gain for the FSK thresholding path. The bootloader samples it while idle, and also refreshes it during long receive-side blank gaps so level changes can still take effect mid-transfer without interrupting packet decoding.

## Build The Bootloader

The build expects a pico-sdk checkout. On this machine the `CMakeLists.txt` auto-detects the Arduino-Pico bundled SDK at:

`~/Library/Arduino15/packages/rp2040/hardware/rp2040/5.5.0/pico-sdk`

Example:

```bash
cmake -S bootloader/audio_bootloader -B build/audio_bootloader
cmake --build build/audio_bootloader
```

This generates `audio_bootloader.uf2` and `audio_bootloader.bin`.

## Generate An Audio Update WAV

Example:

```bash
python3 tools/audio_bootloader/make_audio_wav.py \
  Pico-Sketches/Test/Calibration/Calibration.ino \
  --output build/Calibration_boot.wav
```

The tool:

1. recompiles the sketch with a relocated flash origin at `0x10020000`
2. optionally compresses the aligned image with zlib when it reduces the payload size and still fits the bootloader's buffered receive path
3. resolves the target CPU frequency from `--cpu-hz` or the Arduino-Pico `freq=` FQBN option
4. wraps the image with a 256-byte boot image header, including the resolved `cpu_hz`
5. packetizes the payload in 256-byte blocks with packet CRC32
6. FSK-encodes the stream into a mono 16-bit PCM `.wav`

The current default FSK profile is `9 / 18 / 54` samples for `0 / 1 / blank`.

## CPU Frequency Metadata

The image header now carries an explicit `cpu_hz` field in addition to the app base, vector offset, image size, and CRC.

- If `--cpu-hz` is provided, the tool encodes that value into the `.wav` and also rewrites the Arduino-Pico `freq=` FQBN option so the compiled binary's `F_CPU` matches the encoded metadata
- If `--cpu-hz` is omitted, the tool derives `cpu_hz` from the `freq=` FQBN option, or falls back to the Pico 2 default `150000000`
- After a successful write, the bootloader persists `cpu_hz` into the metadata sector
- On later cold boots, the bootloader reloads that stored `cpu_hz` and applies it before jumping to the sketch

Example:

```bash
python3 tools/audio_bootloader/make_audio_wav.py \
  Pico-Sketches/Pico/Plaits/Plaits.ino \
  --cpu-hz 250000000 \
  --output build/Plaits_250_boot.wav
```
