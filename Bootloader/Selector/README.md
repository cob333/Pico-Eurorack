# Pico-Eurorack Bootloader Selector

First-pass Pico SDK bootloader for the Bootloader/Apps layout.

## Flash Layout

- `0x10000000..0x1003dfff`: bootloader image
- `0x1003e000`: selector config copy A
- `0x1003f000`: selector config copy B
- `0x10040000`: first app slot
- default slot size: `512KB`
- default Arduino-Pico vector offset inside each slot: `0x3000`

The selector stores the active app, slot manifest, and shared calibration values
in the two config sectors. Future app builds must be linked at their slot
address and must not use the bootloader/config sectors.

## Controls

- normal power-up: boot the saved active app
- hold the front-panel button during power-up: enter selector
- short press in selector: next app
- long press in selector: save and boot selected app

## Build

The CMake project defaults to the local Arduino-Pico SDK path:

```sh
cmake -S Bootloader/Selector -B /tmp/pico-selector-build -G Ninja -DPICO_BOARD=pico2
cmake --build /tmp/pico-selector-build
```

The UF2 appears at:

```text
/tmp/pico-selector-build/pico_selector.uf2
```

For RP2040 targets, build with `-DPICO_BOARD=pico`. The app slots must be built
for the same chip family as the selector.

## Slot Build And Package

Compile Arduino-Pico sketches for fixed slots with:

```sh
python3 Bootloader/Tools/pico_boot_apps.py slot-build Sketches/Pico/TripleOSC \
  --slot 0 \
  --fqbn 'rp2040:rp2040:rpipico2:flash=4194304_0,arch=arm,freq=150' \
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
  --target rp2350
```
