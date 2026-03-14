# 2HPico Motion Recorder

**Dependencies:**

Adafruit Neopixel library

2HPicolib support library in this repository


**Usage:**

 top jack - clock input
 
 middle jack - CV out 1
 
 bottom jack - CV out 2

 Pot 1 - manual CV motion for channel 2 (Out2)
 
 Pot 2 - manual CV motion for channel 1 (Out1)

 Pot 3 - sequence length (1-16 steps)
 
 Pot 4 - motion smoothing (interpolation)

 Button - start a new recording. If pressed during playback, recording waits until the current loop ends.


**LEDs:**

 READY - solid Green (waiting for first clock / after recording finishes)
 
 PLAYBACK - Green flash each clock, step 1 flashes Yellow
 
 RECORDING - Red flash each clock, step 1 flashes Yellow
 
 IDLE - Blue flash each clock (queued recording waiting for loop end)


**Notes:**

 If no clock is received for 1s, the play/record step resets to step 1.
