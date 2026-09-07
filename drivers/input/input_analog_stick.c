/*
 * Analog (two potentiometer) joystick input driver.
 *
 * Reports relative motion, not an absolute axis position: deflection is read
 * as a velocity, which is what a pointing device needs and what ZMK's input
 * listener can actually consume. ZMK v0.3.0 discards INPUT_EV_ABS events.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_analog_stick

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(analog_stick, CONFIG_INPUT_LOG_LEVEL);

#if !defined(CONFIG_ADC_NRFX_SAADC)
#error The analog stick driver currently only knows how to set up the nRF SAADC
#endif

/* Samples averaged to find the resting position when centre-mv is not given. */
#define AS_CALIBRATION_SAMPLES 16

enum as_axis { AS_AXIS_X, AS_AXIS_Y, AS_AXIS_COUNT };

struct as_config {
    const struct device *adc;
    uint8_t inputs[AS_AXIS_COUNT];
    struct gpio_dt_spec btn;
    uint16_t sampling_hz;
    uint16_t btn_code;
    uint16_t deadzone_mv;
    uint16_t centre_mv;
    uint16_t scale_multiplier;
    uint16_t scale_divisor;
    bool invert_x;
    bool invert_y;
    bool swap_xy;
};

struct as_data {
    const struct device *dev;
    struct adc_channel_cfg channels[AS_AXIS_COUNT];
    struct adc_sequence sequence;
    int16_t raw[AS_AXIS_COUNT];

    int32_t centre_mv[AS_AXIS_COUNT];
    int32_t remainder[AS_AXIS_COUNT];
    uint8_t calibration_left;
    int32_t calibration_sum[AS_AXIS_COUNT];

    struct k_work_delayable sample_work;
    struct gpio_callback btn_cb;
    struct k_work btn_work;
};

/*
 * The cursor speed is proportional to how far the stick is pushed past the
 * deadzone. Division leftovers are carried into the next tick so that a small
 * steady deflection still creeps rather than rounding away to nothing.
 */
static int32_t as_axis_delta(const struct as_config *config, struct as_data *data,
                             enum as_axis axis, int32_t mv) {
    int32_t offset = mv - data->centre_mv[axis];

    if (offset > config->deadzone_mv) {
        offset -= config->deadzone_mv;
    } else if (offset < -(int32_t)config->deadzone_mv) {
        offset += config->deadzone_mv;
    } else {
        data->remainder[axis] = 0;
        return 0;
    }

    int32_t scaled = offset * config->scale_multiplier + data->remainder[axis];

    data->remainder[axis] = scaled % config->scale_divisor;

    return scaled / config->scale_divisor;
}

static void as_sample_work(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct as_data *data = CONTAINER_OF(dwork, struct as_data, sample_work);
    const struct device *dev = data->dev;
    const struct as_config *config = dev->config;
    int32_t mv[AS_AXIS_COUNT];

    k_work_schedule(&data->sample_work, K_MSEC(1000 / config->sampling_hz));

    int ret = adc_read(config->adc, &data->sequence);

    /* Calibration is a one-shot on the first conversion only. */
    data->sequence.calibrate = false;

    if (ret < 0) {
        LOG_ERR("failed to read the stick: %d", ret);
        return;
    }

    for (uint8_t i = 0; i < AS_AXIS_COUNT; i++) {
        mv[i] = data->raw[i];
        adc_raw_to_millivolts(adc_ref_internal(config->adc), data->channels[i].gain,
                              data->sequence.resolution, &mv[i]);
    }

    if (data->calibration_left > 0) {
        for (uint8_t i = 0; i < AS_AXIS_COUNT; i++) {
            data->calibration_sum[i] += mv[i];
        }

        if (--data->calibration_left == 0) {
            for (uint8_t i = 0; i < AS_AXIS_COUNT; i++) {
                data->centre_mv[i] = data->calibration_sum[i] / AS_CALIBRATION_SAMPLES;
            }
            LOG_INF("stick centred at x=%d mV y=%d mV (leave it alone while booting)",
                    data->centre_mv[AS_AXIS_X], data->centre_mv[AS_AXIS_Y]);
        }
        return;
    }

    int32_t x = as_axis_delta(config, data, AS_AXIS_X, mv[AS_AXIS_X]);
    int32_t y = as_axis_delta(config, data, AS_AXIS_Y, mv[AS_AXIS_Y]);

    if (config->swap_xy) {
        int32_t tmp = x;
        x = y;
        y = tmp;
    }
    if (config->invert_x) {
        x = -x;
    }
    if (config->invert_y) {
        y = -y;
    }

    if (x == 0 && y == 0) {
        return;
    }

    LOG_DBG("stick x=%d mV y=%d mV -> dx=%d dy=%d", mv[AS_AXIS_X], mv[AS_AXIS_Y], x, y);

    if (x != 0) {
        input_report_rel(dev, INPUT_REL_X, x, y == 0, K_FOREVER);
    }
    if (y != 0) {
        input_report_rel(dev, INPUT_REL_Y, y, true, K_FOREVER);
    }
}

static void as_btn_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(port);
    ARG_UNUSED(pins);

    struct as_data *data = CONTAINER_OF(cb, struct as_data, btn_cb);

    k_work_submit(&data->btn_work);
}

static void as_btn_work(struct k_work *work) {
    struct as_data *data = CONTAINER_OF(work, struct as_data, btn_work);
    const struct as_config *config = data->dev->config;

    int pressed = gpio_pin_get_dt(&config->btn);
    if (pressed < 0) {
        LOG_ERR("failed to read the stick button: %d", pressed);
        return;
    }

    input_report_key(data->dev, config->btn_code, pressed, true, K_FOREVER);
}

static int as_init(const struct device *dev) {
    const struct as_config *config = dev->config;
    struct as_data *data = dev->data;
    int ret;

    data->dev = dev;

    if (!device_is_ready(config->adc)) {
        LOG_ERR("ADC is not ready");
        return -ENODEV;
    }

    if (config->scale_divisor == 0) {
        LOG_ERR("scale-divisor must not be zero");
        return -EINVAL;
    }

    for (uint8_t i = 0; i < AS_AXIS_COUNT; i++) {
        data->channels[i] = (struct adc_channel_cfg){
            .gain = ADC_GAIN_1_6,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
            .channel_id = i,
            .input_positive = SAADC_CH_PSELP_PSELP_AnalogInput0 + config->inputs[i],
        };

        ret = adc_channel_setup(config->adc, &data->channels[i]);
        if (ret < 0) {
            LOG_ERR("failed to set up AIN%u: %d", config->inputs[i], ret);
            return ret;
        }
    }

    data->sequence = (struct adc_sequence){
        .channels = BIT(AS_AXIS_X) | BIT(AS_AXIS_Y),
        .buffer = data->raw,
        .buffer_size = sizeof(data->raw),
        .resolution = 12,
        .oversampling = 4,
        .calibrate = true,
    };

    if (config->centre_mv > 0) {
        data->centre_mv[AS_AXIS_X] = config->centre_mv;
        data->centre_mv[AS_AXIS_Y] = config->centre_mv;
    } else {
        data->calibration_left = AS_CALIBRATION_SAMPLES;
    }

    k_work_init_delayable(&data->sample_work, as_sample_work);

    if (config->btn.port != NULL) {
        if (!gpio_is_ready_dt(&config->btn)) {
            LOG_ERR("stick button GPIO is not ready");
            return -ENODEV;
        }

        ret = gpio_pin_configure_dt(&config->btn, GPIO_INPUT);
        if (ret < 0) {
            return ret;
        }

        k_work_init(&data->btn_work, as_btn_work);
        gpio_init_callback(&data->btn_cb, as_btn_isr, BIT(config->btn.pin));

        ret = gpio_add_callback(config->btn.port, &data->btn_cb);
        if (ret < 0) {
            return ret;
        }

        ret = gpio_pin_interrupt_configure_dt(&config->btn, GPIO_INT_EDGE_BOTH);
        if (ret < 0) {
            return ret;
        }
    }

    k_work_schedule(&data->sample_work, K_MSEC(1000 / config->sampling_hz));

    return 0;
}

#define AS_INST(n)                                                                                 \
    static struct as_data as_data_##n;                                                             \
                                                                                                   \
    static const struct as_config as_config_##n = {                                                \
        .adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(DT_DRV_INST(n))),                                 \
        .inputs =                                                                                  \
            {                                                                                      \
                [AS_AXIS_X] = DT_IO_CHANNELS_INPUT_BY_IDX(DT_DRV_INST(n), 0),                      \
                [AS_AXIS_Y] = DT_IO_CHANNELS_INPUT_BY_IDX(DT_DRV_INST(n), 1),                      \
            },                                                                                     \
        .btn = GPIO_DT_SPEC_INST_GET_OR(n, btn_gpios, {0}),                                        \
        .sampling_hz = DT_INST_PROP(n, sampling_hz),                                               \
        .btn_code = DT_INST_PROP_OR(n, btn_code, INPUT_BTN_0),                                               \
        .deadzone_mv = DT_INST_PROP(n, deadzone_mv),                                               \
        .centre_mv = DT_INST_PROP(n, centre_mv),                                                   \
        .scale_multiplier = DT_INST_PROP(n, scale_multiplier),                                     \
        .scale_divisor = DT_INST_PROP(n, scale_divisor),                                           \
        .invert_x = DT_INST_PROP(n, invert_x),                                                     \
        .invert_y = DT_INST_PROP(n, invert_y),                                                     \
        .swap_xy = DT_INST_PROP(n, swap_xy),                                                       \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, as_init, NULL, &as_data_##n, &as_config_##n, POST_KERNEL,             \
                          CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(AS_INST)
