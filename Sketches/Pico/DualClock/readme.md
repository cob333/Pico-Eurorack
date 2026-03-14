# 2HPico Dual Clock

Dual clock generator/divider for 2HPico hardware, with auto external/internal clock switching, swing, random timing, tap tempo, and clock-link mode.

**Dependencies:**

Adafruit Neopixel library

2HPicolib support library in this repository


**Usage:**

Top jack - external clock input

Middle jack - Clock 1 output

Bottom jack - Clock 2 output

Pot 1:

- Internal mode (no external clock): Clock 1 speed, 20-2000 BPM
- External mode: Clock 1 divider, 1 to 1/16

Pot 2:

- Internal mode (normal): Clock 2 speed, 20-2000 BPM
- Internal sync mode: Clock 2 ratio vs Clock 1, x8 ... x1 ... /8
- External mode: Clock 2 divider, 1 to 1/16

Pot 3 - Clock 1 swing amount (0-50%)

Pot 4 - Clock 2 random timing amount (0-100%)

Button:

- Short press (tap): Tap tempo for internal clock
- Long press 3s: Toggle internal sync mode


**Clock Modes:**

External clock has highest priority.

If external clock is present, both outputs follow external timing + divider settings.

If external clock is missing for about 1.5s, module returns to internal mode automatically.

In internal sync mode, Clock 1 is the master and Clock 2 is derived from Clock 1 by Pot 2 ratio.


**Timing Behavior:**

Swing (Pot 3) always affects Clock 1, in both internal and external modes.

Random timing (Pot 4) always affects Clock 2, in both internal and external modes.

Clock 2 random timing re-syncs every 16 clock events to avoid long-term drift.


**LEDs:**

Button held - White

External clock mode - Green

Internal sync mode - Orange

Internal normal mode - Red


**Notes:**

Tap tempo is used for internal clocking. After tapping, turning Pot 1/Pot 2 beyond hysteresis exits tap override for that channel.

At very high BPM, the output pulse width is clamped for reliable trigger behavior.
