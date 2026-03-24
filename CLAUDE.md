# CLAUDE.md — yet-another-zmk-config

This file provides guidance to Claude Code when working with this ZMK keyboard
configuration repository.

## CI Verification — Required After Every Commit

After **every** `git push`, you must:

1. Wait for the GitHub Actions runs to appear:
   ```
   gh run list --branch <branch> --limit 3
   ```
2. Watch **both** workflows to completion:
   - `.github/workflows/build.yml` — ZMK firmware build (primary)
   - `.github/workflows/keymap-drawer.yaml` — SVG keymap render
   ```
   gh run watch <run-id>
   ```
3. If either workflow fails, read the logs, fix the root cause, push a new
   commit, and repeat from step 1. Do **not** leave a failing CI unresolved.

## Project Overview

ZMK firmware configuration for multiple keyboards, managed via GitHub Actions
CI (build.yaml) and a Docker-based local build (Makefile).

## Repository Structure

```
boards/shields/<name>/   Shield (PCB-level) definitions
  *.dtsi                 Device tree: matrix, kscan, display, joystick
  Kconfig.shield         Shield Kconfig symbols and choices
  Kconfig.defconfig      Default config values per shield
  *.zmk.yml             ZMK metadata (id, features, siblings)
  boards/               Board-specific DTS overrides (ADC, SPI, etc.)

config/
  <name>.conf            User-facing feature flags (BLE, display, pointing…)
  <name>.keymap          Layer definitions and key bindings

build.yaml               GitHub Actions build matrix
Makefile                 Docker-based local build entry point
config/west.yml          External module dependencies (ZMK core, displays…)
```

## Key Conventions

- Shield names use **underscores** (`mini_v36_joystick`).
- Split keyboards have `_left` / `_right` / `_left_peripheral` variants;
  unibody keyboards use a single shield with no suffix.
- Board-specific overlays live in `boards/shields/<name>/boards/`.
- The `pro_micro` GPIO abstraction is used for nRF52840 boards
  (nice_nano_v2, puchi_ble_v1).

## Supported Boards (nRF52840)

| Board                | Notes                              |
|----------------------|------------------------------------|
| `nice_nano_v2`       | Primary board                      |
| `puchi_ble_v1`       | Drop-in alternative to nice!nano   |
| `seeeduino_xiao_ble` | Used as USB dongle in split builds |

---

## Mini V36 Joystick Shield

**Location:** `boards/shields/mini_v36_joystick/`

### Design

- **Unibody** (single PCB, single MCU) — no split, no wireless halves
- **36 keys**: 5 columns × 3 rows + 3 thumb keys per side (18+18)
- **KLE reference:** https://gist.githubusercontent.com/pandazxx/9d115ad8b70645e662613269cbbc7cef/raw/bd9c9d05220dd763998746c2d5f217f38b9b55b5/layout.kbd.json
- Halves rotated ±7° (cosmetic, affects PCB layout only)
- Right-side bottom corner houses the joystick module

### Key Position Map

```
 0  1  2  3  4    5  6  7  8  9
10 11 12 13 14   15 16 17 18 19
20 21 22 23 24   25 26 27 28 29
         30 31 32   33 34 35
```

Positions **9, 19, 28, 29** are physically occupied by the joystick module
and are bound to `&none` in the keymap.

### GPIO Pin Assignments (pro_micro numbering)

| Function       | Pins                      |
|----------------|---------------------------|
| Matrix rows    | 4, 5, 6, 7                |
| Matrix columns | 0, 1, 2, 3, 8, 9, 10, 14, 15, 16 |
| Joystick       | 17, 18, 19, 20, 21 (see below) |
| I2C (OLED)     | 18 (SDA), 19 (SCL) — via `&pro_micro_i2c` |

### Pointing Device Support

Two interchangeable pointing device modules. Select exactly one via
`config/mini_v36_joystick.conf`.

#### Option A — Analog Joystick Module

```kconfig
CONFIG_MINI_V36_JOYSTICK_TYPE_ANALOG=y
```

Two-axis analog stick (e.g. PSP-style breakout) using ADC and one GPIO button.
Compatible with OLED display.

| Signal | Pro Micro pin | nRF52840     | ADC channel |
|--------|---------------|--------------|-------------|
| VRx    | 20            | P0.28 (AIN4) | 4           |
| VRy    | 21            | P0.30 (AIN6) | 6           |
| SW     | 17            | GPIO         | —           |

Calibrate `in-min`, `in-max`, `in-deadzone` in `mini_v36_joystick.dtsi` to
match your module's actual ADC range. Add `invert-input;` to the Y axis node
if vertical movement is reversed.

Auto-enabled Kconfig: `CONFIG_ADC=y`, `CONFIG_ANALOG_AXIS=y`, `CONFIG_INPUT=y`

#### Option B — 5-Way Tactile Navigation Switch

```kconfig
CONFIG_MINI_V36_JOYSTICK_TYPE_5WAY=y   # (default)
```

Five-direction switch with each direction on a dedicated GPIO.

| Direction | Pro Micro pin | Conflict         |
|-----------|---------------|------------------|
| UP        | 17            | —                |
| DOWN      | 18            | I2C SDA (OLED!)  |
| LEFT      | 19            | I2C SCL (OLED!)  |
| RIGHT     | 20            | —                |
| CENTER    | 21            | —                |

**Important:** Pins 18/19 conflict with I2C SDA/SCL used by the OLED display.
Set `CONFIG_ZMK_DISPLAY=n` when using the 5-way switch.

Auto-enabled Kconfig: `CONFIG_INPUT=y`, `CONFIG_GPIO_KEYS=y`

### Build Variants (`build.yaml`)

| Artifact name                     | Joystick | Display    |
|-----------------------------------|----------|------------|
| `mini_v36_joystick_5way`          | 5-way    | none       |
| `mini_v36_joystick_analog_oled`   | Analog   | nice!OLED  |
| `mini_v36_joystick_analog_view`   | Analog   | nice!view  |

### Adding a New Shield (general guide)

1. Create `boards/shields/<name>/` with `*.dtsi`, `Kconfig.*`, `*.zmk.yml`.
2. For split: add `_left`, `_right`, `_left_peripheral` overlay files and set
   `ZMK_SPLIT` + `ZMK_SPLIT_ROLE_CENTRAL` in `Kconfig.defconfig`.
3. For unibody: define all rows and columns in the single `*.dtsi`.
4. Add build entries to `build.yaml`.
