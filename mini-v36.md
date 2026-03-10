# Mini V36 Joystick — Branch Summary (`feat/mini-v36`)

## New Keyboard Shield: `mini_v36_joystick`

A complete ZMK shield for a **36-key unibody keyboard** (nRF52840 / nice_nano_v2) with a pointing device module.

---

## New Files

### `boards/shields/mini_v36_joystick/` — the shield definition

| File | Purpose |
|------|---------|
| `mini_v36_joystick.dtsi` | Core DTS: 10×4 matrix, 36-key physical layout for ZMK Studio, kscan GPIO config, analog joystick (ADC) and 5-way switch (GPIO) under `#if` guards, SSD1306 OLED on I2C |
| `mini_v36_joystick.overlay` | Shield discovery entry — `#include`s the dtsi |
| `Kconfig.shield` | Defines `SHIELD_MINI_V36_JOYSTICK` + `choice MINI_V36_JOYSTICK_TYPE` (ANALOG vs 5WAY, default 5WAY) |
| `Kconfig.defconfig` | Sets keyboard BLE name `"Mini V36 Jstk"`, enables I2C/SSD1306 when `ZMK_DISPLAY=y` |
| `mini_v36_joystick.conf` | Shield-level Kconfig: no-op (driver deps live in user conf) |
| `mini_v36_joystick.zmk.yml` | Shield metadata: unibody (no siblings), features: keys, pointing, display, studio |
| `boards/nice_nano_v2.overlay` | Board-specific ADC channel definitions (AIN4/P0.28 for X, AIN6/P0.30 for Y), gated on `CONFIG_MINI_V36_JOYSTICK_TYPE_ANALOG` |

### `config/mini_v36_joystick.conf` — user configuration

- BLE enhancements, sleep, ZMK Studio, debounce tuning
- `CONFIG_ZMK_POINTING=y`
- Default: `CONFIG_MINI_V36_JOYSTICK_TYPE_5WAY=y`

### `config/mini_v36_joystick.keymap` — keymap

36-key keymap with 4 layers (Base QWERTY, Nav, Sym, Mouse), home-row mods, combos, and `mmv`/`msc`/`mkp` mouse bindings on Layer 3.

### `CLAUDE.md` — project documentation

Covers the keyboard layout, GPIO pin assignments, joystick type options, pin conflicts, and build variants.

---

## Modified Files

### `build.yaml` — 3 new build entries

| Artifact | Shield combo | Pointing | Display |
|----------|-------------|----------|---------|
| `mini_v36_joystick_5way` | `mini_v36_joystick` | 5-way switch | none |
| `mini_v36_joystick_analog_oled` | `mini_v36_joystick nice_oled` | Analog joystick | OLED (SSD1306) |
| `mini_v36_joystick_analog_view` | `mini_v36_joystick nice_view_adapter nice_epaper` | Analog joystick | nice!view e-paper |

---

## Key Design Decisions

- **Unibody** — single MCU, no split/central/peripheral
- **Dual pointing device support** — analog joystick (ADC) or 5-way nav switch (GPIO), selected at build time via Kconfig
- **Pin conflict** — 5-way switch uses pins 18/19 (I2C SDA/SCL), so OLED is incompatible with 5-way mode
- **ZMK Studio** — enabled via `snippet: studio-rpc-usb-uart` and `zmk,physical-layout` in DTS chosen

---

## GPIO Pin Assignments (pro_micro numbering)

| Function | Pins |
|----------|------|
| Matrix rows | 4, 5, 6, 7 |
| Matrix columns | 0, 1, 2, 3, 8, 9, 10, 14, 15, 16 |
| Analog joystick VRx | 20 (P0.28, AIN4) |
| Analog joystick VRy | 21 (P0.30, AIN6) |
| Analog joystick SW / 5-way UP | 17 |
| 5-way DOWN | 18 (conflicts with I2C SDA) |
| 5-way LEFT | 19 (conflicts with I2C SCL) |
| 5-way RIGHT | 20 |
| 5-way CENTER | 21 |
| I2C SDA (OLED) | 18 |
| I2C SCL (OLED) | 19 |

---

## Key Positions

```
 0  1  2  3  4    5  6  7  8  9
10 11 12 13 14   15 16 17 18 19
20 21 22 23 24   25 26 27 28 29
         30 31 32   33 34 35
```

Positions **9, 19, 28, 29** are physically occupied by the joystick module and bound to `&none` in the keymap.
