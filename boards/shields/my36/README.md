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
  revolution per axis, which is why the listener multiplies by 16:
  `input-processors = <&zip_xy_scaler 16 1>;`. Change the first parameter.
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
