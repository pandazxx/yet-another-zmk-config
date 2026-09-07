# my_36 — test shield

3x3 test matrix on a nice!nano v2, plus a BlackBerry trackball breakout used
as a pointing device.

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

## How it works

`zmk,input-bb-trackball` (see `drivers/input/input_bb_trackball.c` at the repo
root) puts a both-edge interrupt on each of the four direction pins, counts the
pulses, and every `report-interval-ms` emits the accumulated
`right - left` / `down - up` as `INPUT_REL_X` / `INPUT_REL_Y`. `BTN` is
reported as `INPUT_BTN_0`, which the input listener turns into a left click.

## Tuning

- **Too slow / too fast.** The ball only produces ~9 transitions per full
  revolution per axis, which is why the listener multiplies by 64:
  `input-processors = <&zip_xy_scaler 64 1>;`. Change the first parameter.
  It doubles as the cursor's step size in pixels, so there is a trade-off:
  higher is faster but moves the cursor in coarser jumps. 32–128 is the
  usable range with this sensor.
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
broken out on a nice!nano, so the two axes have to share those three pads.
These overlap the trackball's pins, which is why the two are separate shields
rather than one board carrying both.

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
