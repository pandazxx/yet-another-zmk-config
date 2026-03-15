# field_test — Pin Layout

Board: **nice!nano v2** (nRF52840, pro_micro footprint)
Shield: `field_test` | 4 keys, 2×2 matrix, analog joystick

---

## Key Matrix (2×2, col2row)

```
               Col 0              Col 1
               pro_micro 0        pro_micro 1

Row 0  ┌──────────────────┬──────────────────┐
       │   key 0          │   key 1          │  pro_micro 4
Row 1  ├──────────────────┼──────────────────┤
       │   key 2          │   key 3          │  pro_micro 5
       └──────────────────┴──────────────────┘
```

| Key # | Row | Col | pro_micro row pin | pro_micro col pin | Default binding |
|-------|-----|-----|-------------------|-------------------|-----------------|
| 0     | 0   | 0   | 4                 | 0                 | `A`             |
| 1     | 0   | 1   | 4                 | 1                 | `B`             |
| 2     | 1   | 0   | 5                 | 0                 | Left click      |
| 3     | 1   | 1   | 5                 | 1                 | Right click     |

Diode direction: **col2row** (row pins active-high + pull-down, col pins active-high)

---

## Analog Joystick (ADC)

| Signal | pro_micro pin | nRF52840 pin | ADC channel | Notes        |
|--------|---------------|--------------|-------------|--------------|
| VRx    | 20            | P0.28        | AIN4        | X axis       |
| VRy    | 21            | P0.30        | AIN6        | Y axis       |
| SW     | 17            | —            | —           | Button (unused in firmware; wire to matrix if needed) |
| VCC    | VCC           | —            | —           |              |
| GND    | GND           | —            | —           |              |

ADC channels are defined in `boards/nice_nano_v2.overlay`.
Calibrate `in-min`, `in-max`, `in-deadzone` in `field_test.dtsi` for your module.

---

## All Pin Assignments at a Glance

| pro_micro pin | Function      |
|---------------|---------------|
| 0             | Col 0         |
| 1             | Col 1         |
| 4             | Row 0         |
| 5             | Row 1         |
| 17            | Joystick SW (unconnected in firmware) |
| 20            | Joystick VRx (AIN4 / P0.28)          |
| 21            | Joystick VRy (AIN6 / P0.30)          |

All other pins are free for future use.
