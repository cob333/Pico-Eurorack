# Pico-Eurorack

[中文](README_CN.md) | English

![Pico_Icon](./images/Pico_Icon.jpg)

Arduino sketches for `2HPico` and `2HPico DSP` Eurorack modules; source code lives in `Sketches/`.
This repository also includes a friendly Web client for browsing module descriptions, editing sampler content, selecting up to 6 Bootloader app slots, and generating firmware; 
Click the link to use online: https://cob333.github.io/Pico-Eurorack/Client/

This is an alternative set of sketches for the 2HPico and 2HPico DSP modules by rich hesslip. See the original sketches at: 
https://github.com/rheslip/2HPico-DSP-Sketches; 
https://github.com/rheslip/2HPico-Sketches;

What's the difference?
easy to use Web-based client;
more sketches;
more color options; 
Unified file structure for both Pico and PicoFX sketches;

## Web Client
1. Online Usage: https://cob333.github.io/Pico-Eurorack/Client/
2. Local Usage: The client lives in `Client/`. Its backend calls `Bootloader/Tools/pico_boot_apps.py` to compile the selected sketches, then packages the Bootloader selector and app slots into one `.uf2` file.
  From the repository root, then open the client in your browser at: http://127.0.0.1:8765/

```sh
python3 Client/server.py
```

If port `8765` is already in use, the backend automatically tries the next ports and prints the actual URL in the terminal.

### Workflow

1. Choose `Pico` or `PicoFX` in the upper-right corner.
2. Browse functions in `App Catalog`; each card shows the current firmware size estimate.
3. Drag functions into the Slots area, or remove functions from Slots.
4. The capacity bar shows `used / 3.5MiB`. When the selected apps exceed the storage limit, `Generate` turns gray and cannot be clicked; when the size is valid, the button turns green.
5. After clicking `Generate`, the page shows a build progress bar. Click the cross icon on the right side to cancel the build.
6. When the build finishes, the browser immediately downloads the generated `.uf2`.

### Sampler Apps

`GridsSampler` and `OneshotSampler` include sample editing entry points:

- `GridsSampler`: uploaded WAV files are added to the current sample library. It does not split samples into Banks; the backend reads the library from `Samples/Samples.h` or `Samples/samples.h`.
- `OneshotSampler`: supports uploading WAV files to Banks, listing Banks, and deleting Banks.

The current default `GridsSampler` library uses the `TR-808` samples from `OneshotSampler`; `OneshotSampler` keeps `TR-606`, `TR-808`, and `TR-909` as default Banks.

### Requirements

Firmware generation requires the local machine to have:

- `python3`
- `arduino-cli`
- Arduino-Pico core, for example `rp2040:rp2040`
- Pico SDK / CMake for building the Bootloader selector
- This repository's `Sketches/lib` and the required Arduino libraries

If the selector UF2 is missing, the backend tries to build it automatically from `Bootloader/Selector`.

## Pico

![Pico](./images/Pico.jpg)

1. 16Step_Sequencer: a 16-step CV/Gate sequencer with per-step ratchets, scale quantization, clock division, length, and overall pitch;
2. Branches: a Bernoulli gate inspired by "Mutable Instruments Branches", routes one trigger input to two mutually exclusive outputs with probability；
3. Braids:  macro-oscillator inspired by "Mutable Instruments Braids" offers multiple  models with ADSR control;
4. DRUMS: multi-model drum voice that icludes 808 kick, 909 kick, 808 snare, 909 snare, and hi-hat engines;
5. DejaVu: a semi-random sequencer with a "DejaVu" memory control that captures and re-injects note/gate patterns to balance repetition and variation;
6. DualClock: a dual clock generator/divider with tap tempo, swing, random timing, and linked clock ratios;
7. Grids_Sampler: a drum machine that combines "Mutable Instruments Grids" rhythm generation with four-channel sample playback;
8. Rings: a resonator voice based on "Mutable Instruments Rings" with trigger and pitch CV input, plus 6 models for 4-polyphony at maximum;
9. Modulation: a combined ADSR and LFO modulation source that switches between envelope mode and multi-waveform syncable LFO mode;
10. Moogvoice: a monophonic subtractive synth voice with three oscillators, a Moog-style ladder filter, ADSR envelope, and LFO modulation;
11. Motion_Recorder: a dual-channel CV motion recorder that loops knob movements, synchronizes to an external clock, and supports queued re-recording;
12. Oneshot_Sampler: a one-shot sampler with V/Oct pitch control, envelope shaping, tone control, sample randomization, and reverse playback;
13. Plaits: macro-oscillator inspired by "Mutable Instruments Plaits";
14. PlaitsFM: 6OP-FM-oscillator inspired by "Mutable Instruments Plaits";
15. TripleOSC: a compact three-oscillator voice with shared waveform selection, independent tuning, FM input, and per-oscillator mute/calibration support;

## PicoFX

![PicoFX](./images/PicoFX.jpg)

1. Chorus: a stereo chorus effect with delay, feedback, LFO rate/depth, wet/dry mix, and output level control;
2. Delay: a delay effect with feedback, wet/dry mix, output level, and either free delay time or external-clock sync plus ping-pong mode;
3. Flanger: a stereo flanger with delay, feedback, LFO depth/rate, stereo width, and multiple modulation waveforms;
4. Pitchshifter: a pitch shifter with adjustable delay window, transposition, random modulation, and wet/dry mix;
5. Reverb: a stereo reverb with controllable decay time, low-pass damping, wet/dry mix, and output level;
6. Granular: a granular processor that chops incoming audio into grains with adjustable size, density, pitch, and wet/dry mix;
7. Ladderfilter: a Moog ladder low-pass filter effect with resonance, base cutoff, 1V/Oct cutoff CV scaling, and output level control;
8. Bitcrush: a mono bitcrusher with 1-bit to 16-bit resolution control, 1x to 20x downsampling, CV-controlled wet/dry mix, and output level;
9. Panner: a stereo spreader/autopanner that moves mono input across the stereo field with adjustable width, rate, LFO shape, and output level;
10. Sidechain: a trigger-ducking sidechain effect with adjustable attack, decay, curve, and output level;
11. Spectral Smash: a spectral effect that captures a short-time spectrum and keeps warping, blurring, smearing, and remixing the frozen frame into torn evolving textures;
12. BeatBreaker: a clock-sliced beat breaker that captures recent beat buffers and, on each beat, can probabilistically replay, reverse, and retrigger them in 2x to 8x subdivisions;

## Test
1. Button: a simple hardware test that cycles the front-panel RGB LED color each time the button is pressed;
2. Calibration: a DAC calibration helper that outputs fixed reference voltages on both CV outputs for tuning Pico;

## Credits
- Rich Hesslip for the original 2HPico and 2HPico DSP libraries and sketches;
- Mutable Instruments for the original modules that inspired many of these sketches;
- The open-source Arduino and TinyUSB communities for the tools that make this possible;
- SYNSO for the DRUMS sketch;
- You for checking this out and supporting open-source Eurorack development~
