# This is an alternative set of sketches for the 2HPico by Rich Heslip, see the original skeches at: https://github.com/rheslip/2HPico-Sketches

What's the difference?
more color options; 
easy-to-use calibration sketches;
More function:
1. 16Step_Sequencer: 4 pages of step pitch editing (16 total steps), per-step ratchet (1-8 via long-press edit), and auto-reset to step 1 after clock timeout;
2. Motion_Recorder: record knob tweaks as looping CV motion on both outputs, with queued re-record (start next loop) and clock-timeout reset behavior;
3. Bassdrum: Uninified 909 and 808 bassdrum model;
4. Branches: Bernoulli gates inspired by Mutable Instruments Branches; Gate/Toggle primary modes, Trigger/Latch secondary modes, Pot1 sets probability, Pot2 sets output level, LED shows active side (Left=green, Right=red).
5. DualClock: Two independent clock source/dividers/multipliers with selectable division/multiplication factors, swing/randomization.
6. Modulation: Unified LFO/Envelope/ADSR.

Changes: 
1. 16Step_Sequencer now save the last edited page and step, so you can power cycle without losing your place in the sequence.
2. TripleOSC now has a mute function for each oscillator independently, and an easy-to-use calibration.



How to upload sketches to 2HPico:
See the video tutorial by at:
https://www.bilibili.com/video/BV1EZcJzuEpn/?spm_id_from=333.1387.homepage.video_card.click&vd_source=0430f3bb05088dc339796b9fe9bb5899


You must have Arduino 2.xx installed with the Pico board support package https://github.com/earlephilhower/arduino-pico

Select board type as Raspberry Pi Pico or Raspberry Pico Pico 2 depending on what board you used when building the module. Some sketches require overclocking - check the readme files.

Dependencies:

2HPico library included in this repository - install it in your Arduino/Libraries directory

Adafruit Neopixel library

Some sketches use rheslip‘s fork of ElectroSmith's DaisySP library https://github.com/rheslip/DaisySP_Teensy