# Pico-Eurorack

An RP2350 firmware repository for 2HP Pico / PicoFX Eurorack modules, combining:

- `Sketches/Pico`: standard 2HPico module sketches
- `Sketches/PicoFX`: DSP / effects sketches
- `Sketches/Test`: calibration and hardware self-test sketches
- `Sketches/lib`: Arduino libraries bundled with this repository
- `AudioBootloader`: a bootloader and toolchain for updating firmware over a 3.5 mm audio cable

This repository builds on the ideas from Rich Heslip's `2HPico-Sketches` and `2HPico-DSP-Sketches`. 
see the original skeches at:
- https://github.com/rheslip/2HPico-Sketches
- https://github.com/rheslip/2HPico-DSP-Sketches
Much appreciated Rich's pioneering work, and this repository is meant to be a complementary expansion that keeps standard modules, DSP modules, test sketches, and the audio update workflow in one unified directory layout that is easier to maintain, compile, and distribute.

## What Is Different From The Upstream Repositories

- More modules and expanded functionality
- A unified `Pico` / `PicoFX` / `Test` / `lib` directory structure
- An independent audio-script flashing workflow

## If You Only Want To Flash A Sketch

### 1. First Step

Hold the `BOOT` button on the RP2350, then connect the USB cable to your computer to enter bootloader mode.

### 2. Second Step

You should now see a removable drive named `RP2350` or `RPI-RP2`. Copy `AudioBootloader/build/audio_bootloader.uf2` onto that drive.

### 3. Third Step

Unplug the USB cable, reinstall the module into your Eurorack case, and power it up again. The module will enter bootloader mode and the LED will blink blue.

### 4. Fourth Step

Use a 3.5 mm audio cable to connect your computer to the module's top jack. Play the `.wav` file for the sketch you want to flash. While it is playing, adjust your computer volume and, if needed, fine-tune with `Pot1` so that the LED stays mostly green and avoids blue / orange / red.

### 5. Fifth Step

If flashing succeeds, the LED will blink green three times and automatically jump to the new application. If flashing fails, press the module button and try again.

### 6. Sixth Step

Once `audio_bootloader.uf2` has been installed, you can flash new sketches directly over the audio cable. Hold the module button during power-up to enter update mode, then play the new `.wav` file. No USB connection is required.

# If You Want To Compile And Customize Sketches, Continue Reading Below

## Repository Layout

| Path | Description |
| --- | --- |
| `Sketches/Pico` | Standard 2HPico modules: clocks, sequencers, modulation sources, synth voices, and more |
| `Sketches/PicoFX` | PicoFX / DSP effects: Delay, Reverb, Chorus, Granular, etc. |
| `Sketches/Test` | Test utilities such as `Calibration` and `Button` |
| `Sketches/lib` | Bundled libraries: `2HPicolib`, `STMLIB`, `PLAITS`, `6opFM` |
| `AudioBootloader/audio_bootloader` | RP2350 bootloader firmware built with pico-sdk + CMake |
| `AudioBootloader/tools/audio_bootloader` | Tools that compile Arduino sketches into a fixed app-slot image and generate a playable `.wav` |

## Hardware And Software Requirements

### Hardware

- Verified primarily against RP2350 / Raspberry Pi Pico 2 targets
- Intended for 2HP Pico / PicoFX style Eurorack hardware
- If a sketch requires the middle jack to be switched to CV input, CV output, or stereo output, check that sketch's `README.md` first

### Software

- Arduino IDE 2.x, or `arduino-cli`
- Arduino-Pico core: `rp2040:rp2040`
- `Adafruit NeoPixel`
- DaisySP-based sketches require DaisySP to be installed separately: `https://github.com/rheslip/DaisySP_Teensy`

### Libraries Already Included In This Repository

`Sketches/lib` already contains:

- `2HPicolib`
- `STMLIB`
- `PLAITS`
- `6opFM`

If you use Arduino IDE, the simplest option is to copy or symlink these directories into your sketchbook `libraries/` folder.  
If you use `arduino-cli`, you can pass `--libraries Sketches/lib` directly in the compile command.

## Quick Start

### 1. Install The Core And Basic Dependencies

```bash
arduino-cli core install rp2040:rp2040
arduino-cli lib install "Adafruit NeoPixel"
```

`I2S` is provided by Arduino-Pico and does not need a separate install.  
`DaisySP` is not bundled in this repository and must be installed separately.

### 2. Select The Board

Recommended board:

```text
Raspberry Pi Pico 2
```

Matching `arduino-cli` FQBN:

```text
rp2040:rp2040:rpipico2
```

### 3. Compile Examples

The following commands have been verified against the current repository structure:

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipico2 --libraries Sketches/lib Sketches/Test/Button

arduino-cli compile --fqbn rp2040:rp2040:rpipico2 --libraries Sketches/lib Sketches/Pico/Branches

arduino-cli compile --fqbn rp2040:rp2040:rpipico2:freq=150,opt=Small --libraries Sketches/lib Sketches/PicoFX/DaisySP_Delay

arduino-cli compile --fqbn rp2040:rp2040:rpipico2:freq=250,opt=Small --libraries Sketches/lib Sketches/Pico/Plaits
```

If you use Arduino IDE:

1. Open the target sketch directory.
2. Select `Raspberry Pi Pico 2`.
3. Set the CPU frequency required by the sketch, such as `150 MHz` or `250 MHz`.
4. Install any missing libraries and upload.

## Common Usage Conventions

- The four knobs are often multiplexed across multiple parameter pages
- The button is usually used for page changes, mode changes, or special actions
- The RGB LED usually shows the current page, mode, or state
- The top / middle / bottom jacks change function depending on the sketch

The header comments in each sketch are the most accurate source for panel mapping.

## Sketch Overview

### `Sketches/Pico`

| Sketch | Function | Notes |
| --- | --- | --- |
| `16Step_Sequencer` | 16-step CV / Gate sequencer | 4 pages of step editing, good for quantized melodic sequences |
| `Branches` | Bernoulli gate / toggle module | Inspired by Mutable Instruments Branches |
| `DejaVu` | Random sequencer with a "theme recall" concept | More experimental |
| `Drums` | 4 DaisySP drum models | Analog BD / Synth BD / HiHat / Synth Snare |
| `DualClock` | Dual clock generator / divider | Supports external clock, tap tempo, swing, and randomization |
| `Grids_Sampler` | Grids-style drum sequencer + sample playback | 4 channels |
| `Modal` | Physical-modeling modal voice | More demanding on CPU frequency |
| `Modulation` | LFO + ADSR modulation source | Dual-mode sketch |
| `Moogvoice` | Three-oscillator synth voice | Includes a Moog-style lowpass filter, ADSR, and LFO |
| `Motion_Recorder` | Dual-channel CV recorder / player | Up to 16 steps |
| `Plaits` | Reduced Mutable Instruments Plaits port | `250 MHz` recommended |
| `PlaitsFM` | 6opFM / DX7 patch-bank voice | 8 banks, 32 patches per bank |
| `TripleOSC` | Basic three-oscillator voice | Useful for calibration and basic sound experiments |
| `Turing_Machine(WIP)` | Turing Machine style random step sequencer | Still work in progress |

### `Sketches/PicoFX`

| Sketch | Function | Notes |
| --- | --- | --- |
| `DaisySP_Chorus` | Chorus | Light CPU load |
| `DaisySP_Delay` | Delay / Ping-Pong Delay | Light CPU load |
| `DaisySP_Flanger` | Flanger | Needs a higher CPU frequency |
| `DaisySP_Pitchshifter` | Pitch Shifter | Light CPU load |
| `DaisySP_Reverb` | Reverb | High memory usage |
| `Granular` | Granular effect | CPU intensive |
| `Ladderfilter` | Moog Ladder Filter | Can use the middle jack as cutoff CV |

### `Sketches/Test`

| Sketch | Function | Notes |
| --- | --- | --- |
| `Calibration` | DAC calibration helper | Cycles through `0V / +1V / +2V / +4V / -1V` |
| `Button` | Button + RGB LED self-test | Useful for quickly checking panel interaction after power-up |

## Frequency And Performance Recommendations

Different sketches have very different sample-rate and CPU requirements. The source comments already document the expected range, but these are good starting points:

| Recommended Frequency | Typical Sketches |
| --- | --- |
| `150 MHz` | `Branches`, `Button`, `Calibration`, `DaisySP_Chorus`, `DaisySP_Delay`, `DaisySP_Pitchshifter`, `DaisySP_Reverb`, `Ladderfilter` |
| `200-250 MHz` | `Modal`, `Moogvoice` |
| `250 MHz` | `Plaits`, `PlaitsFM`, `DaisySP_Flanger`, `Granular` |

If you hear crackling, stuttering, or unstable parameter behavior, check these first:

- The board frequency matches the sketch comments
- Your DaisySP / STMLIB / PLAITS versions match the expected versions
- The middle-jack jumper mode is set correctly

## Calibration Notes

The `V/Oct` and CV calibration constants are not centralized in one file. Many sketches define values such as:

- `CVOUT_VOLT`
- `CVIN_VOLT`
- `CV_VOLT`

Recommended workflow:

1. Flash `Sketches/Test/Calibration` first.
2. Use a multimeter to confirm that the middle / bottom outputs hit `0V / +1V / +2V / +4V / -1V`.
3. Then return to the target sketch and fine-tune `CVOUT_VOLT` or `CVIN_VOLT` as needed.

Calibration matters especially for pitch-tracking sketches such as `Plaits`, `PlaitsFM`, `Modal`, `TripleOSC`, and `Moogvoice`.

## Audio Bootloader

The `AudioBootloader` directory contains an RP2350 audio bootloader. It lets you flash the bootloader once, then update future sketches over a 3.5 mm audio cable using a generated `.wav`.

### Build The Bootloader

The following CMake workflow has been verified:

```bash
cmake -S AudioBootloader/audio_bootloader -B build/audio_bootloader
cmake --build build/audio_bootloader -j4
```

Artifacts are generated at:

- `build/audio_bootloader/audio_bootloader.uf2`
- `build/audio_bootloader/audio_bootloader.bin`

### Generate A Playable Update WAV

The current `make_audio_wav.py` in this repository works directly with sketches under `Sketches`:

```bash
python3 AudioBootloader/tools/audio_bootloader/make_audio_wav.py \
  Sketches/Test/Button/Button.ino \
  --output build/Button_boot.wav
```

If the target sketch needs a higher CPU frequency, encode it explicitly in the image metadata:

```bash
python3 AudioBootloader/tools/audio_bootloader/make_audio_wav.py \
  Sketches/Pico/Plaits/Plaits.ino \
  --cpu-hz 250000000 \
  --output build/Plaits_boot.wav
```

This tool will:

- relink the sketch into the fixed app slot
- optionally compress the image with zlib
- encode the CPU frequency into the image header
- generate an FSK `.wav` that can be played into the module

### Use The Bootloader On The Module

1. Hold `BOOTSEL` and drag `audio_bootloader.uf2` onto the `RPI-RP2` / `RP2350` USB drive.
2. Put the module back into the Eurorack case, then hold the front-panel button during power-up to enter bootloader mode.
3. Connect your playback device to the top jack with a 3.5 mm cable.
4. Play the generated `.wav`.
5. Use `Pot1` as the input gain trim and keep the LED mostly green, avoiding long periods of red or orange clipping.
6. On success, the LED blinks green three times and jumps to the new application. On failure, press the button to reset and try again.

### Developer Notes

- Bootloader flash region: `0x10000000 - 0x1001FFFF`
- Application slot starts at `0x10020000`
- The bootloader stores the most recently successful `cpu_hz` in metadata and restores it on later cold boots

## References

- Original 2HPico sketch collection: <https://github.com/rheslip/2HPico-Sketches>
- Original 2HPico DSP sketch collection: <https://github.com/rheslip/2HPico-DSP-Sketches>
- 2HPico Eurorack hardware: <https://github.com/rheslip/2HPico-Eurorack-Module-Hardware>
- Arduino upload tutorial video: <https://www.bilibili.com/video/BV1EZcJzuEpn/>

## License

The repository root uses the MIT License.  
The repository also contains code or ports derived from Rich Heslip, Mutable Instruments, STMLIB, PLAITS, 6opFM, DaisySP, and related projects, so also check the license headers and notices in the relevant subdirectories and source files.
