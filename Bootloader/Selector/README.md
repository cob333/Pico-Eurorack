# Pico-Eurorack Bootloader Selector

First-pass Pico SDK bootloader for the Bootloader/Apps layout.

## Flash Layout

- `0x10000000..0x1003dfff`: bootloader image
- `0x1003e000`: selector config copy A
- `0x1003f000`: selector config copy B
- `0x10040000`: first app slot
- packaged app slots are dynamic; each slot records its own flash offset and
  max size in the manifest config
- legacy fixed slot size fallback: `512KB`
- default manifest app count: `6`
- default Arduino-Pico vector offset inside each slot: `0x3000`

The selector stores the active app, slot manifest, CPU frequency, and shared
calibration values in the two config sectors. Future app builds must be linked
at their manifest flash address and must not use the bootloader/config sectors.

## Controls

- normal power-up: boot the saved active app
- hold the front-panel button during power-up: enter selector
- keep holding the front-panel button for 3 seconds during power-up: enter global calibration
- short press in selector: next app
- long press in selector: save and boot selected app

## Global Calibration

Calibration runs inside the selector and is implemented under
`Bootloader/Calibration`, not in `Sketches/Test/Calibration`. The shared flash
config layout and persistence live in `Bootloader/boot_config.h` and
`Bootloader/boot_config.c`.

1. Hold the button for 3 seconds during power-up.
2. For input calibration, patch 0V, 1V, 2V, 3V, 4V, then 5V to the CV inputs.
   The LED color changes for each voltage and never uses red. Press the button
   once after each voltage is stable.
3. The selector blinks green 3 times after successful input calibration.
4. Patch the CV output back to CV input 1, then press the button once. The
   selector outputs 0V through 5V and uses the input calibration to calculate
   `cvout_counts_per_volt`.
5. The selector writes the updated calibration into the active global config
   sector and blinks green 3 times.

Slot-loaded apps can read the values through
`Sketches/lib/2HPicolib/PicoBootConfig.h`. `2HPico.h` loads the boot calibration
at app startup and exposes `picoBootCalibration` plus helper getters.

## Build

The CMake project defaults to the local Arduino-Pico SDK path:

```sh
cmake -S Bootloader/Selector -B /tmp/pico-selector-build -G Ninja -DPICO_BOARD=pico2
cmake --build /tmp/pico-selector-build
```

The UF2 appears at:

```text
/tmp/pico-selector-build/Pico_Firmware.uf2
```

For RP2040 targets, build with `-DPICO_BOARD=pico`. The app slots must be built
for the same chip family as the selector.

## Slot Build And Package

Compile Arduino-Pico sketches for a dynamic slot by choosing a slot offset and
slot size for that app:

```sh
python3 Bootloader/Tools/pico_boot_apps.py slot-build Sketches/Pico/TripleOSC \
  --slot 0 \
  --slot-offset 0x40000 \
  --slot-size 0x1a000 \
  --fqbn 'rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=250' \
  --build-path /tmp/pico-bootapps-tripleosc-slot0 \
  --library Sketches/lib \
  --library "$HOME/Documents/Arduino/libraries"
```

Then combine selector, apps, and manifest config sectors:

```sh
python3 Bootloader/Tools/pico_boot_apps.py package \
  --selector /tmp/pico-selector-build/pico_selector.uf2 \
  --app /tmp/pico-bootapps-tripleosc-slot0/TripleOSC.ino.uf2 \
  --output /tmp/pico-bootapps-bundle.uf2 \
  --target rp2350 \
  --layout-json /tmp/pico-bootapps-layout.json
```

By default, `package` reads each app UF2's linked flash address and writes that
actual address plus the measured app size into the manifest, rounded to a 4KB
sector with 16KB spare space. Use `--fixed-slots` only for older uniform slot
bundles.
