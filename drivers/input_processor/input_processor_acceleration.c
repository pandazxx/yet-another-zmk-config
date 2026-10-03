/*
 * Speed-dependent pointer acceleration input processor.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_acceleration

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>

LOG_MODULE_REGISTER(zip_accel, CONFIG_INPUT_LOG_LEVEL);

/*
 * One processor node can be referenced by several listeners, and each needs
 * its own notion of how fast its device is moving. Listener indices are
 * assigned densely from zero, so a small array covers every realistic board.
 */
#define ACCEL_MAX_LISTENERS 4

struct accel_axis_state {
    uint32_t last_ms;
    uint32_t avg_speed;
};

struct accel_config {
    uint8_t type;
    uint32_t slow_speed;
    uint32_t fast_speed;
    size_t codes_len;
    const uint16_t *codes;
};

struct accel_data {
    struct accel_axis_state *axes; /* ACCEL_MAX_LISTENERS * codes_len */
};

static int accel_code_index(const struct accel_config *config, uint16_t code) {
    for (size_t i = 0; i < config->codes_len; i++) {
        if (config->codes[i] == code) {
            return (int)i;
        }
    }

    return -1;
}

static int accel_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                              uint32_t param2, struct zmk_input_processor_state *state) {
    const struct accel_config *config = dev->config;
    struct accel_data *data = dev->data;

    if (event->type != config->type || event->value == 0) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int code_index = accel_code_index(config, event->code);
    if (code_index < 0) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    uint32_t min_mult = MAX(param1, 1);
    uint32_t max_mult = MAX(param2, min_mult);

    /*
     * Timed per code rather than across the whole report: X and Y arrive as
     * separate events in the same millisecond, so a shared clock would read
     * the second one as infinitely fast.
     */
    size_t listener = state ? MIN(state->input_device_index, ACCEL_MAX_LISTENERS - 1) : 0;
    struct accel_axis_state *axis = &data->axes[listener * config->codes_len + code_index];

    uint32_t now = k_uptime_get_32();
    uint32_t elapsed = now - axis->last_ms;
    uint32_t magnitude = (event->value < 0) ? -event->value : event->value;

    axis->last_ms = now;

    /* Two events in the same millisecond can only mean fast. */
    uint32_t speed = elapsed ? (magnitude * 1000U) / elapsed : config->fast_speed;

    if (speed <= config->slow_speed || axis->avg_speed == 0) {
        /* Coming from rest: start at the fine end instead of inheriting the
         * speed the previous movement happened to finish at. */
        axis->avg_speed = speed;
    } else {
        /* Smoothed, or the multiplier jitters between adjacent reports. */
        axis->avg_speed = (axis->avg_speed + speed) / 2;
    }

    speed = axis->avg_speed;

    uint32_t mult;

    if (speed <= config->slow_speed) {
        mult = min_mult;
    } else if (speed >= config->fast_speed) {
        mult = max_mult;
    } else {
        mult = min_mult + ((max_mult - min_mult) * (speed - config->slow_speed)) /
                              (config->fast_speed - config->slow_speed);
    }

    LOG_DBG("code %u value %d at %u units/s -> x%u", (unsigned int)event->code, event->value,
            speed, mult);

    event->value *= (int32_t)mult;

    return ZMK_INPUT_PROC_CONTINUE;
}

static int accel_init(const struct device *dev) {
    const struct accel_config *config = dev->config;

    if (config->fast_speed <= config->slow_speed) {
        LOG_ERR("fast-speed must be greater than slow-speed");
        return -EINVAL;
    }

    return 0;
}

static struct zmk_input_processor_driver_api accel_driver_api = {
    .handle_event = accel_handle_event,
};

#define ACCEL_INST(n)                                                                              \
    static const uint16_t accel_codes_##n[] = DT_INST_PROP(n, codes);                              \
                                                                                                   \
    static struct accel_axis_state                                                                 \
        accel_axes_##n[ACCEL_MAX_LISTENERS * DT_INST_PROP_LEN(n, codes)];                          \
                                                                                                   \
    static struct accel_data accel_data_##n = {.axes = accel_axes_##n};                            \
                                                                                                   \
    static const struct accel_config accel_config_##n = {                                          \
        .type = DT_INST_PROP_OR(n, type, INPUT_EV_REL),                                            \
        .slow_speed = DT_INST_PROP(n, slow_speed),                                                 \
        .fast_speed = DT_INST_PROP(n, fast_speed),                                                 \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                                   \
        .codes = accel_codes_##n,                                                                  \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, accel_init, NULL, &accel_data_##n, &accel_config_##n, POST_KERNEL,    \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &accel_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ACCEL_INST)
