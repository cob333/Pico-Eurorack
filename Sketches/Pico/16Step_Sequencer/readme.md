# 2HPico 16 step sequencer

**Dependencies:**

Adafruit Neopixel library

2HPicolib support library in this repository


**Usage:**

 top jack - trigger input
 
 middle jack - gate output
 
 bottom jack - pitch CV output

 page 1 parameters - Red LED
 
 Pot 1 - step 1 pitch - fully ccw is silent
 
 pot 2 - step 2 pitch - fully ccw is silent
 
 pot 3 - step 3 pitch - fully ccw is silent
 
 pot 4 - step 4 pitch - fully ccw is silent

 page 2 parameters - Violet LED
 
 Pot 1 - step 5 pitch - fully ccw is silent
 
 pot 2 - step 6 pitch - fully ccw is silent
 
 pot 3 - step 7 pitch - fully ccw is silent
 
 pot 4 - step 8 pitch - fully ccw is silent

 page 3 parameters - Blue LED
 
 Pot 1 - step 9 pitch - fully ccw is silent
 
 pot 2 - step 10 pitch - fully ccw is silent
 
 pot 3 - step 11 pitch - fully ccw is silent
 
 pot 4 - step 12 pitch - fully ccw is silent
 
 page 4 parameters - Aqua LED
 
 Pot 1 - step 13 pitch - fully ccw is silent
 
 pot 2 - step 14 pitch - fully ccw is silent
 
 pot 3 - step 15 pitch - fully ccw is silent
 
 pot 4 - step 16 pitch - fully ccw is silent
 
 page 5 parameters GREEN LED
 
 Pot 1 - scale - fully ccw major pentatonic. 9-12 o'clock major, 12-3 o'clock minor pentatonic, full cw minor
 
 Pot 2 - clock divider - 1 to 8
 
 Pot 3 - steps - fully ccw is 1 step, fully cw is 16 steps
  
 Pot 4 - overall pitch

**Notes:**

 Short press button to change pages. Hold button for 0.5s to enter ratchet edit mode on pages 1-4.
 
 While holding the button, turn a step pot to set ratchet count (1-8 repeats for that step).
 
 LED stays solid at the current page color while holding the button.
 
 Step 1 flashes Orange on each cycle start (even if gate is off).
