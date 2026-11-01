# my_36 — 36-key trackball keyboard

A 36-key keyboard (three rows of five plus three thumb keys per hand) on a
nice!nano v2, with a BlackBerry trackball. The two test variants,
`my_36_js` and `my_36_duo`, are unchanged and still use the old 3x3 test
matrix (`my_36_test.dtsi`).

| Shield      | Matrix | Pointing device      | Artifact        |
| ----------- | ------ | -------------------- | --------------- |
| `my_36`     | 6x6    | BlackBerry trackball | `xx_36.uf2`     |
| `my_36_js`  | 3x3    | analog joystick      | `xx_36_js.uf2`  |
| `my_36_duo` | 3x3    | both at once         | `xx_36_duo.uf2` |

Each has a matching `*_debug.uf2` that logs to a USB serial console.

## Pins

6 rows x 6 columns is exactly 36 positions: 12 GPIOs for the matrix, 4 for the
trackball, and `D2`/`D3` (the I2C pins) left free for an OLED. That is all 18
header pins; nothing else is spare. The ball click is not wired (the thumb keys
carry `&mkp LCLK` / `&mkp RCLK` instead).

```
              ┌──── USB ─────┐
      row 0 ──┤ D1       RAW │
      row 1 ──┤ D0       GND ├── ball GND
              │ GND      RST │
              │ GND      VCC ├── ball VCC
      (I2C) ──┤ D2       D21 ├── ball UP
      (I2C) ──┤ D3       D20 ├── ball DWN
      col 0 ──┤ D4       D19 ├── ball LFT
      col 1 ──┤ D5       D18 ├── ball RHT
      col 2 ──┤ D6       D15 ├── row 3
      col 3 ──┤ D7       D14 ├── row 2
      col 4 ──┤ D8       D16 ├── row 4
      col 5 ──┤ D9       D10 ├── row 5
              └──────────────┘
```

### Key wiring

Diodes run column to row (`col2row`). Each half is a 3x6 block of the matrix:
the left hand is rows 0-2, the right hand rows 3-5, columns 0-4 are the five
finger columns and column 5 is the three thumb keys.

| Position (row,col in the layout picture) | Matrix row, col       |
| ---------------------------------------- | --------------------- |
| left fingers `0,0`-`2,4`                 | row 0-2, col 0-4      |
| left thumbs `3,2` `3,3` `3,4`            | row 0, 1, 2 / col 5   |
| right fingers `0,5`-`2,9`                | row 3-5, col 0-4      |
| right thumbs `3,5` `3,6` `3,7`           | row 3, 4, 5 / col 5   |

Bindings in `my_36.keymap` are in picture order: the three rows of ten, then
the six thumb keys. To wire differently, only the `map` in `my_36.dtsi` needs
to change. The keymap in the repo is a placeholder.

## Trackball wiring

The breakout has 11 pads. Only 6 are needed:

| Pad   | nice!nano | Notes                                      |
| ----- | --------- | ------------------------------------------ |
| `VCC` | `VCC`     | 2.5–5.25 V, so the nice!nano's 3.3 V is ok |
| `GND` | `GND`     |                                            |
| `UP`  | `D21`     |                                            |
| `DWN` | `D20`     |                                            |
| `LFT` | `D19`     |                                            |
| `RHT` | `D18`     |                                            |

`BTN` and `BLU` / `RED` / `GRN` / `WHT` stay unconnected (the colour pads drive
the LEDs inside the clear ball; tie one through a resistor to `VCC` if you want
it lit).

`D10` and `D16` are the nRF52840's NFC pins and are matrix rows here;
`my_36.dtsi` and `my_36_test.dtsi` set `nfct-pins-as-gpios`, which is a one-way UICR change.

## How it works

`zmk,input-bb-trackball` (see `drivers/input/input_bb_trackball.c` at the repo
root) puts a both-edge interrupt on each of the four direction pins, counts the
pulses, and every `report-interval-ms` emits the accumulated
`right - left` / `down - up` as `INPUT_REL_X` / `INPUT_REL_Y`. If a `btn-gpios`
is given, it is reported as `INPUT_BTN_0`, a left click; this keyboard leaves it out.

### Acceleration

The ball resolves only ~9 pulses per revolution per axis, which is coarse
enough that no single step size works: big enough to cross a screen is far too
big to land on a button. So the step is not fixed — it comes from how fast the
ball is turning.

The spacing between pulses is the speed measure. It maps linearly onto a
multiplier:

```
 pulse spacing   >= accel-slow-interval-ms  ->  accel-min-multiplier   (8 px)
                 <= accel-fast-interval-ms  ->  accel-max-multiplier (128 px)
                           in between       ->  linear ramp
```

The estimate is smoothed so the multiplier doesn't jitter between adjacent
reports, and it resets to the slow end after a pause — a correction made after
lifting your thumb starts fine rather than inheriting the speed the last
movement ended at.

This lives in an **input processor**, not the driver — `zmk,input-processor-
acceleration` in `drivers/input_processor/`. ZMK's model is that drivers report
raw physical events and processors transform the stream on its way to HID, and
acceleration is a transformation, not a property of the sensor. Upstream has no
acceleration processor (not in v0.3.0, not on `main`), so this one is ours.

Keeping it at that seam buys three things the in-driver version could not do:
the joystick can reuse it, it chains with other processors, and a listener's
per-layer child nodes can swap the curve — hold a layer for a precision mode,
release for normal acceleration.

```dts
zip_accel: zip_accel {
    compatible = "zmk,input-processor-acceleration";
    #input-processor-cells = <2>;
    codes = <INPUT_REL_X INPUT_REL_Y>;
    unit-divisor = <4>;    // = the trackball's smoothing-steps
    slow-speed = <12>;     // pulses/second
    fast-speed = <50>;
};

trackball_listener {
    device = <&trackball>;
    input-processors = <&zip_accel 8 128>;   // min px/pulse, max px/pulse
};
```

Speed is in units per second, where a unit is whatever the device reports —
pulses for the trackball, pixels for the joystick, which already emits a
velocity. That is why the thresholds are not transferable between the two: the
trackball idles around 10/s and spins at 50/s, while the joystick's slowest
useful deflection is already 100/s. Give each device its own node.

Each code is timed separately, because X and Y arrive as separate events within
the same millisecond and a shared clock would read the second one as infinitely
fast.

Speed is the movement seen over a trailing `speed-window-ms` (150 ms), not the
gap between the last two events. A slow ball gives one pulse per report and the
gap wanders by 2x from mechanical and timer noise, which used to show up as the
step size flipping between 8 px and 16 px on a steady roll. Everything below
`slow-speed` stays on the flat fine step, so keep it above the pulse rate of a
slow, deliberate roll.

### Smoothing

The ball gives ~9 pulses a revolution, so one pulse is one 8 px lump.
`smoothing-steps` on the trackball spreads each pulse over that many report
intervals (4 x 10 ms = a 40 ms glide, largest step ~2 px at the fine end). The
driver then reports quarter-pulses, so `unit-divisor` on the processor must be
set to the same number; the fractions are carried, never rounded, so distance
travelled is unchanged. Cost: up to `smoothing-steps x report-interval-ms` of
extra latency on the end of a movement. Set both to 1 to turn it off.

There is no `zip_xy_scaler` on the listener any more: a flat multiplier stacked
on the curve would just undo it.

## Tuning

- **Too coarse when aiming.** Lower the first cell of `&zip_accel` — it is
  the finest step the pointer can take, so it sets your precision floor.
- **Too slow when crossing the screen.** Raise the second cell.
- **Acceleration kicks in too eagerly / too late.** Move `slow-speed` and
  `fast-speed` on the `zip_accel` node. Widening the gap between them makes
  the ramp more gradual.
- **Want a flat step instead.** Drop `&zip_accel` from `input-processors`
  and use `<&zip_xy_scaler 64 1>`, restoring `#include
  <input/processors.dtsi>` at the top of the overlay.
- **An axis moves the wrong way.** Add `invert-x;` or `invert-y;` to the
  `trackball` node.
- **X and Y are swapped** (module mounted rotated): add `swap-xy;`.
- **Cursor drifts while the ball is still.** The direction pins are probably
  floating; add `GPIO_PULL_DOWN` to the four `*-gpios` flags.
- **Movement feels chunky.** Raise `smoothing-steps` (and `unit-divisor` with
  it), or lower `report-interval-ms`.
- **Cursor feels laggy at the end of a flick.** Lower `smoothing-steps`.
- **Step size still wobbles on a slow roll.** Raise `slow-speed` or
  `speed-window-ms`.
- **Debugging.** Uncomment `CONFIG_INPUT_LOG_LEVEL_DBG=y` in `my_36.conf` and
  read the deltas over the USB CDC console (the build already uses the
  `studio-rpc-usb-uart` snippet).

Right/middle click are not wired to the ball; bind them in the keymap with
`&mkp RCLK` / `&mkp MCLK` (needs `#include <dt-bindings/zmk/pointing.h>`).

---

# my_36_js — the analog joystick variant

Same 3x3 test matrix (shared via `my_36_test.dtsi`), but with a two-potentiometer
joystick instead of the trackball, and mouse buttons on the bottom keymap row.

Build it as `xx_36_js.uf2`; `xx_36_js_debug.uf2` is the same thing with the raw
millivolt readings on a USB serial console.

## Joystick wiring

| Module | nice!nano | Notes                                   |
| ------ | --------- | --------------------------------------- |
| `GND`  | `GND`     |                                         |
| `+5V`  | `VCC`     | 3.3 V — the ADC range assumes this rail |
| `VRx`  | `D20`     | AIN5 / P0.29                            |
| `VRy`  | `D21`     | AIN7 / P0.31                            |
| `SW`   | `D18`     | stick press, active low                 |

Only `P0.02` (D19), `P0.29` (D20) and `P0.31` (D21) are both ADC-capable and
broken out on a nice!nano, so the two axes have to come from those three pads.
They overlap the trackball's pins here, which is what `my_36_duo` below exists
to resolve.

### Pinout

```
              ┌──── USB ─────┐
              │ D1       RAW │
              │ D0       GND ├── stick GND
              │ GND      RST │
              │ GND      VCC ├── stick +5V
      row 0 ──┤ D2       D21 ├── stick VRy
      row 1 ──┤ D3       D20 ├── stick VRx
      col 0 ──┤ D4       D19 │
      col 1 ──┤ D5       D18 ├── stick SW
              │ D6       D15 │
      row 2 ──┤ D7       D14 │
              │ D8       D16 ├── col 2
              │ D9       D10 │
              └──────────────┘
```

## How it works

`zmk,input-analog-stick` (`drivers/input/input_analog_stick.c`) samples both
axes at `sampling-hz`, subtracts the resting voltage, ignores anything inside
`deadzone-mv`, and reports the remainder as **relative** motion. So deflection
is a speed, not a position: push further, move faster.

Relative is not a stylistic choice. ZMK v0.3.0's input listener has an empty
`handle_abs_code`, so absolute axis events are silently discarded — an
`INPUT_EV_ABS` driver cannot move the cursor no matter how it is configured.
This is also why `zephyr,analog-axis` is not used here; it does not exist in
Zephyr 3.5 at all, which is what produces `undefined reference to
__device_dts_ord_N` at link time if you reference that compatible.

The centre voltage is measured over the first 16 samples at startup, so
**don't hold the stick while the board boots**. Pin it down with `centre-mv`
if you would rather not rely on that.

## Tuning

- **Cursor creeps when released.** Raise `deadzone-mv`. Speed is measured from
  the *edge* of the deadzone, so widening it doesn't introduce a jump.
- **Too slow / too fast.** Lower `scale-divisor` to speed up. Division
  leftovers carry between ticks, so even a tiny deflection still creeps
  instead of rounding to zero.
- **An axis is reversed.** Add `invert-x;` or `invert-y;`.
- **Axes swapped** (stick mounted rotated): add `swap-xy;`.
- **Battery drain.** The ADC runs continuously at `sampling-hz`. Lower it if
  idle current matters more than smoothness.
- **Calibration.** Flash `xx_36_js_debug.uf2` and read the console: startup
  logs `stick centred at x=… mV y=… mV`, and every movement logs its raw
  millivolts and the resulting delta.

---

# my_36_duo — both pointing devices

The trackball and the joystick on one board. ZMK creates one input listener
per device and they all feed the same HID mouse, so nothing special is needed
beyond finding enough pins.

Only the joystick is really constrained: its axes have to come from the three
ADC-capable pads. The trackball wants nothing but edge interrupts, so it moves
onto plain digital pins and gives up the analog ones.

### Pinout

```
              ┌──── USB ─────┐
              │ D1       RAW │
              │ D0       GND ├── both GND
              │ GND      RST │
              │ GND      VCC ├── both VCC
      row 0 ──┤ D2       D21 ├── stick VRy
      row 1 ──┤ D3       D20 ├── stick VRx
      col 0 ──┤ D4       D19 ├── stick SW
      col 1 ──┤ D5       D18 │
   ball LFT ──┤ D6       D15 ├── ball BTN
      row 2 ──┤ D7       D14 ├── ball RHT
   ball DWN ──┤ D8       D16 ├── col 2
    ball UP ──┤ D9       D10 │
              └──────────────┘
```

Twelve of the eighteen pads are used. `D0`, `D1`, `D10` and `D18` stay free.

### Wiring

| Trackball | nice!nano | Joystick | nice!nano    |
| --------- | --------- | -------- | ------------ |
| `UP`      | `D9`      | `VRx`    | `D20` (AIN5) |
| `DWN`     | `D8`      | `VRy`    | `D21` (AIN7) |
| `LFT`     | `D6`      | `SW`     | `D19`        |
| `RHT`     | `D14`     | `+5V`    | `VCC`        |
| `BTN`     | `D15`     | `GND`    | `GND`        |
| `VCC`     | `VCC`     |          |              |
| `GND`     | `GND`     |          |              |

The ball click stays a left click. The stick press is remapped to a right
click with `btn-code = <INPUT_BTN_1>`, because both drivers otherwise report
`INPUT_BTN_0` and the two buttons would collide.

### Gotchas

- **Phantom cursor movement.** If the trackball is not wired yet, its four
  direction pins float and pick up noise. Add `GPIO_PULL_DOWN` to the four
  `*-gpios` flags, or stay on `my_36_js` until it is wired.
- **SAADC channels are global.** ZMK's battery sensor owns channel 0, so the
  stick's axes start at channel 1. Sharing a slot makes one device silently
  read the other's input; the symptom is an axis frozen at the battery
  voltage, around 2716 mV.
- **NFC pins.** `D16` is `P0.10`, which the nRF52840 reserves for the NFC
  antenna by default. `my_36.dtsi` and `my_36_test.dtsi` set `nfct-pins-as-gpios` on `&uicr` so
  that matrix column works on a freshly erased chip. The switch is one-way.
