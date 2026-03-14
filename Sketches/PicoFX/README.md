# This is an alternative set of sketches for the 2HPico DSP by Rich Heslip, see the original skeches at: https://github.com/rheslip/2HPico-DSP-Sketches

What's the difference?
more color options; 

How to upload sketches to PicoFX:
See the video tutorial by at:
https://www.bilibili.com/video/BV1EZcJzuEpn/?spm_id_from=333.1387.homepage.video_card.click&vd_source=0430f3bb05088dc339796b9fe9bb5899

Arduino Pico sketches for the 2HPico DSP Eurorack module https://github.com/rheslip/2HPico-Eurorack-Module-Hardware

You must have Arduino 2.xx installed with the Pico board support package https://github.com/earlephilhower/arduino-pico

The DSP version of 2HPico requires an RP2350 processor since most DSP apps are fairly compute intensive. The DaisySP library uses floating point calculations extensively and will run very poorly on an RP2040. Some sketches require overclocking - check the comments in the source code.

Dependencies:

2HPico library from https://github.com/rheslip/2HPico-Sketches/tree/main/lib - install it in your Arduino/Libraries directory

Adafruit Neopixel library

Some sketches use my fork of ElectroSmith's DaisySP library https://github.com/rheslip/DaisySP_Teensy
