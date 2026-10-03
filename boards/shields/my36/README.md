# my_36 — test shield

A 3x3 test matrix on a nice!nano v2 with a pointing device bolted on. Three
variants share the matrix (`my_36.dtsi`) and differ only in what they point
with:

| Shield      | Pointing device      | Artifact        |
| ----------- | -------------------- | --------------- |
| `my_36`     | BlackBerry trackball | `xx_36.uf2`     |
| `my_36_js`  | analog joystick      | `xx_36_js.uf2`  |
| `my_36_duo` | both at once         | `xx_36_duo.uf2` |

Each has a matching `*_debug.uf2` that logs to a USB serial console. Pads left
blank in the diagrams below are free for your own use.

## Trackball wiring

The breakout has 11 pads. Only 7 are needed:

| Pad   | nice!nano | Notes                                      |
| ----- | --------- | ------------------------------------------ |
| `VCC` | `VCC`     | 2.5–5.25 V, so the nice!nano's 3.3 V is ok |
| `GND` | `GND`     |                                            |
| `UP`  | `D21`     |                                            |
| `DWN` | `D20`     |                                            |
| `LFT` | `D19`     |                                            |
| `RHT` | `D18`     |                                            |
| `BTN` | `D15`     | ball click, active low                     |

`BLU` / `RED` / `GRN` / `WHT` drive the LEDs inside the clear ball. Leave them
unconnected, or tie one through a resistor to `VCC` if you want it lit.

`D21`–`D18` and `D15` are a contiguous run on the right-hand header, just below
`VCC`. They avoid `D10`/`D16` (the nRF52840 NFC pins) and the matrix pins
(`D2`, `D3`, `D7` rows; `D4`, `D5`, `D16` columns).

### Pinout

```
              ┌──── USB ─────┐
              │ D1       RAW │
              │ D0       GND ├── ball GND
              │ GND      RST │
              │ GND      VCC ├── ball VCC
      row 0 ──┤ D2       D21 ├── ball UP
      row 1 ──┤ D3       D20 ├── ball DWN
      col 0 ──┤ D4       D19 ├── ball LFT
      col 1 ──┤ D5       D18 ├── ball RHT
              │ D6       D15 ├── ball BTN
      row 2 ──┤ D7       D14 │
              │ D8       D16 ├── col 2
              │ D9       D10 │
              └──────────────┘
```

## How it works

`zmk,input-bb-trackball` (see `drivers/input/input_bb_trackball.c` at the repo
root) puts a both-edge interrupt on each of the four direction pins, counts the
pulses, and every `report-interval-ms` emits the accumulated
`right - left` / `down - up` as `INPUT_REL_X` / `INPUT_REL_Y`. `BTN` is
reported as `INPUT_BTN_0`, which the input listener turns into a left click.

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

Both multipliers default to 1, so the feature is inert unless a board sets
them. That is why there is no `zip_xy_scaler` on the listener any more: the
step size now comes from the driver, and stacking a flat scaler on top would
just undo the point of it.

## Tuning

- **Too coarse when aiming.** Lower `accel-min-multiplier`. This is the
  finest step the pointer can take, so it sets your precision floor.
- **Too slow when crossing the screen.** Raise `accel-max-multiplier`.
- **Acceleration kicks in too eagerly / too late.** Move the thresholds.
  `accel-fast-interval-ms` is the pulse spacing at which you reach full
  speed, `accel-slow-interval-ms` the spacing below which nothing
  accelerates. Widening the gap makes the ramp more gradual.
- **Want the old fixed step back.** Leave both multipliers at their default
  of 1 and put `input-processors = <&zip_xy_scaler 64 1>;` back on the
  listener.
- **An axis moves the wrong way.** Add `invert-x;` or `invert-y;` to the
  `trackball` node.
- **X and Y are swapped** (module mounted rotated): add `swap-xy;`.
- **Cursor drifts while the ball is still.** The direction pins are probably
  floating; add `GPIO_PULL_DOWN` to the four `*-gpios` flags.
- **Movement feels chunky.** Lower `report-interval-ms`.
- **Debugging.** Uncomment `CONFIG_INPUT_LOG_LEVEL_DBG=y` in `my_36.conf` and
  read the deltas over the USB CDC console (the build already uses the
  `studio-rpc-usb-uart` snippet).

Right/middle click are not wired to the ball; bind them in the keymap with
`&mkp RCLK` / `&mkp MCLK` (needs `#include <dt-bindings/zmk/pointing.h>`).

---

# my_36_js — the analog joystick variant

Same 3x3 matrix (shared via `my_36.dtsi`), but with a two-potentiometer
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
  antenna by default. `my_36.dtsi` sets `nfct-pins-as-gpios` on `&uicr` so
  that matrix column works on a freshly erased chip. The switch is one-way.
