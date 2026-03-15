# ZMK Analog Joystick Shield — Build Lessons

Lessons learned while creating the `field_test` shield (ZMK v0.3.0, Zephyr 3.5).

---

## 1. Gate analog DTS nodes behind a Kconfig `#if`

**Rule:** The `zephyr,analog-axis` driver is only compiled when the build
system's DTS compat scan sees the node. The scan runs on the
C-preprocessor-expanded DTS, so nodes inside `#if defined(CONFIG_FOO)`
blocks are only visible when `CONFIG_FOO` is defined.

Unconditional nodes — placed directly in the DTSI with no `#if` guard —
are **not** picked up by the scan, even though DTS compilation itself
succeeds. This causes a linker error at the very end of the build:

```
undefined reference to `__device_dts_ord_N'
```

**Fix:** Define a shield-specific Kconfig symbol with `default y` and wrap
the analog joystick + listener nodes in the corresponding `#if` block,
exactly mirroring how `mini_v36_joystick` works.

`Kconfig.shield`:
```kconfig
if SHIELD_FOO

config FOO_ANALOG
    bool
    default y

endif
```

`foo.dtsi`:
```c
#if defined(CONFIG_FOO_ANALOG)

    analog_joystick: analog_joystick {
        compatible = "zephyr,analog-axis";
        ...
    };

    joystick_listener: joystick_listener {
        compatible = "zmk,input-listener";
        device = <&analog_joystick>;
    };

#endif /* CONFIG_FOO_ANALOG */
```

---

## 2. Negative integers in DTS cell arrays need parentheses

**Rule:** Some DTC versions do not accept a bare unary minus in `< >` cell
arrays. Always write negative integers with parentheses.

```c
/* ✗ parse error on older DTC */
out-min = <-127>;

/* ✓ universally accepted */
out-min = <(-127)>;
```

Applies to `out-min`, `out-max`, and any other signed property in
`zephyr,analog-axis` child nodes.

---

## 3. `CONFIG_ANALOG_AXIS` is not a valid Kconfig symbol in ZMK v0.3.0

**Rule:** Do **not** set `CONFIG_ANALOG_AXIS=y` in `.conf` files or
`cmake-args`. The symbol does not exist in the Kconfig tree for this
version. Setting it aborts the build with:

```
warning: attempt to assign the value 'y' to the undefined symbol ANALOG_AXIS
error: Aborting due to Kconfig warnings
```

The driver is enabled implicitly through the DTS compat scan (see lesson 1).
The only Kconfig dependency to set explicitly is `CONFIG_ADC=y`.

---

## 4. `gpio-keys` nodes referencing `&pro_micro` fail outside `#if` guards

**Rule:** If a `gpio-keys` node that references a `&pro_micro` pin is
placed unconditionally in the DTSI (no `#if` guard), the DTS validator may
reject it with:

```
devicetree error: child specifier ... does not appear in <Property 'gpio-map' at '/connector'>
```

This does not affect nodes guarded by `#if defined(CONFIG_...)`, because
the validator skips them when the condition is false. Either gate the node
with a Kconfig `#if` or reference the underlying nRF52840 GPIO directly
(`&gpio0`, `&gpio1`) in the board overlay instead of `&pro_micro`.

---

## Reference: working mini_v36_joystick analog cmake-args

```yaml
cmake-args: -DCONFIG_MINI_V36_JOYSTICK_TYPE_ANALOG=y -DCONFIG_ADC=y
```

No other Kconfig symbols are needed. `ANALOG_AXIS`, `INPUT`, and
`ZMK_POINTING` are all auto-selected through the dependency chain.
