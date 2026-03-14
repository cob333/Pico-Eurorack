# Audio Bootloader
This repository includes a audio bootloader implementation for the Pico hardware.

How to use it:
1. Find the `audio_bootloader.uf2` at `Pico-AudioBootloader/build/audio_bootloader_toolchain/audio_bootloader.uf2`;
2. Hold `BOOTSEL` and plug the Pico into USB, then you'll see a new mass storage device named `RP2350`(or `RPI-RP2`);
3. Copy the `audio_bootloader.uf2` to the `RP2350` drive, then the bootloader will be flashed to the Pico and it will reboot into the bootloader mode;
4. Now put your Pico into the eurorack and power it on, plug in a 3.5mm cable from your device into the Pico top jack, and play the generated audio in https://github.com/cob333/Pico-Sketches;
5. Adjust the Volume in your device, and Pot1 on the pico as a trimmer, let the LED stay green during the signal present state, then the bootloader will receive the data and flash it to the Pico;
6. After the flashing is done, Pico will blink 3 times in green, if not, press the button to reset the bootloader and try again.
7. Once the `audio_bootloader.uf2` is successfully flashed, you can just power on the Pico while holding the button, and it will automatically enter the bootloader mode and wait for the audio signal to flash the new firmware.