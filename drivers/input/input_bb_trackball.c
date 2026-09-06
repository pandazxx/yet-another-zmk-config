/*
 * BlackBerry trackball breakout input driver.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_bb_trackball

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(bb_trackball, CONFIG_INPUT_LOG_LEVEL);

/*
 * The probe is deliberately late and repeating: at init it would land long
 * before the USB console has enumerated, and its output would be dropped.
 * Repeating also makes it usable as a wiggle test on a suspect joint.
 */
#define BB_TRACKBALL_PROBE_PERIOD_S 10

enum bb_trackball_dir {
    BB_DIR_UP,
    BB_DIR_DOWN,
    BB_DIR_LEFT,
    BB_DIR_RIGHT,
    BB_DIR_COUNT,
};

struct bb_trackball_config {
    struct gpio_dt_spec dirs[BB_DIR_COUNT];
    struct gpio_dt_spec btn;
    uint16_t report_interval_ms;
    bool invert_x;
    bool invert_y;
    bool swap_xy;
};

struct bb_trackball_dir_data {
    const struct device *dev;
    struct gpio_callback cb;
    uint8_t dir;
};

struct bb_trackball_data {
    const struct device *dev;
    atomic_t counts[BB_DIR_COUNT];
    struct bb_trackball_dir_data dirs[BB_DIR_COUNT];
    struct gpio_callback btn_cb;
    struct k_work_delayable report_work;
    struct k_work_delayable probe_work;
    struct k_work btn_work;
};

static void bb_trackball_dir_isr(const struct device *port, struct gpio_callback *cb,
                                 uint32_t pins) {
    ARG_UNUSED(port);
    ARG_UNUSED(pins);

    struct bb_trackball_dir_data *dir_data = CONTAINER_OF(cb, struct bb_trackball_dir_data, cb);
    const struct device *dev = dir_data->dev;
    const struct bb_trackball_config *config = dev->config;
    struct bb_trackball_data *data = dev->data;

    atomic_inc(&data->counts[dir_data->dir]);

    /* No-op if a report is already pending, so the counts simply keep piling up. */
    k_work_schedule(&data->report_work, K_MSEC(config->report_interval_ms));
}

static void bb_trackball_report_work(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct bb_trackball_data *data = CONTAINER_OF(dwork, struct bb_trackball_data, report_work);
    const struct device *dev = data->dev;
    const struct bb_trackball_config *config = dev->config;

    int32_t up = atomic_clear(&data->counts[BB_DIR_UP]);
    int32_t down = atomic_clear(&data->counts[BB_DIR_DOWN]);
    int32_t left = atomic_clear(&data->counts[BB_DIR_LEFT]);
    int32_t right = atomic_clear(&data->counts[BB_DIR_RIGHT]);

    /* HID mouse coordinates grow right and *down*. */
    int32_t x = right - left;
    int32_t y = down - up;

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

    if (up != 0 || down != 0 || left != 0 || right != 0) {
        LOG_DBG("pulses up=%d down=%d left=%d right=%d -> x=%d y=%d (levels %d%d%d%d)", up, down,
                left, right, x, y, gpio_pin_get_dt(&config->dirs[BB_DIR_UP]),
                gpio_pin_get_dt(&config->dirs[BB_DIR_DOWN]),
                gpio_pin_get_dt(&config->dirs[BB_DIR_LEFT]),
                gpio_pin_get_dt(&config->dirs[BB_DIR_RIGHT]));
    }

    if (x == 0 && y == 0) {
        return;
    }

    if (x != 0) {
        input_report_rel(dev, INPUT_REL_X, x, y == 0, K_FOREVER);
    }
    if (y != 0) {
        input_report_rel(dev, INPUT_REL_Y, y, true, K_FOREVER);
    }
}

static void bb_trackball_btn_isr(const struct device *port, struct gpio_callback *cb,
                                 uint32_t pins) {
    ARG_UNUSED(port);
    ARG_UNUSED(pins);

    struct bb_trackball_data *data = CONTAINER_OF(cb, struct bb_trackball_data, btn_cb);

    k_work_submit(&data->btn_work);
}

static void bb_trackball_btn_work(struct k_work *work) {
    struct bb_trackball_data *data = CONTAINER_OF(work, struct bb_trackball_data, btn_work);
    const struct device *dev = data->dev;
    const struct bb_trackball_config *config = dev->config;

    int pressed = gpio_pin_get_dt(&config->btn);
    if (pressed < 0) {
        LOG_ERR("failed to read the trackball button (%d)", pressed);
        return;
    }

    input_report_key(dev, INPUT_BTN_0, pressed, true, K_FOREVER);
}

/*
 * Tell a driven line apart from a floating one: bias the pin high, then low,
 * and see whether anything on the other end overrules us. A pin that follows
 * the bias is high impedance - nothing is driving it - while a pin that reads
 * the same under both biases is being held by the module.
 */
static void bb_trackball_probe_pin(const struct gpio_dt_spec *spec, const char *name) {
    int with_pull_up = -1;
    int with_pull_down = -1;

    if (gpio_pin_configure(spec->port, spec->pin, GPIO_INPUT | GPIO_PULL_UP) == 0) {
        k_busy_wait(200);
        with_pull_up = gpio_pin_get_raw(spec->port, spec->pin);
    }

    if (gpio_pin_configure(spec->port, spec->pin, GPIO_INPUT | GPIO_PULL_DOWN) == 0) {
        k_busy_wait(200);
        with_pull_down = gpio_pin_get_raw(spec->port, spec->pin);
    }

    /* Back to whatever the devicetree asked for. */
    gpio_pin_configure_dt(spec, GPIO_INPUT);

    LOG_INF("%s pin: pull-up reads %d, pull-down reads %d -> %s", name, with_pull_up,
            with_pull_down,
            (with_pull_up == with_pull_down) ? "driven by the module" : "floating (nothing driving it)");
}

/*
 * Interrupts are already armed by the time this runs, so drop them for the
 * duration of the probe and put them straight back.
 */
static void bb_trackball_probe_work(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct bb_trackball_data *data = CONTAINER_OF(dwork, struct bb_trackball_data, probe_work);
    const struct bb_trackball_config *config = data->dev->config;
    static const char *const dir_names[BB_DIR_COUNT] = {"up", "down", "left", "right"};

    for (uint8_t i = 0; i < BB_DIR_COUNT; i++) {
        const struct gpio_dt_spec *spec = &config->dirs[i];

        gpio_pin_interrupt_configure_dt(spec, GPIO_INT_DISABLE);
        bb_trackball_probe_pin(spec, dir_names[i]);
        gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_BOTH);
    }

    k_work_schedule(&data->probe_work, K_SECONDS(BB_TRACKBALL_PROBE_PERIOD_S));
}

static int bb_trackball_init_pin(const struct device *dev, const struct gpio_dt_spec *spec,
                                 struct gpio_callback *cb, gpio_callback_handler_t handler) {
    int ret;

    if (!gpio_is_ready_dt(spec)) {
        LOG_ERR("GPIO port %s is not ready", spec->port ? spec->port->name : "(null)");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(spec, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("failed to configure pin %d (%d)", spec->pin, ret);
        return ret;
    }

    gpio_init_callback(cb, handler, BIT(spec->pin));

    ret = gpio_add_callback(spec->port, cb);
    if (ret < 0) {
        LOG_ERR("failed to add callback for pin %d (%d)", spec->pin, ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_BOTH);
    if (ret < 0) {
        LOG_ERR("failed to enable interrupts on pin %d (%d)", spec->pin, ret);
        return ret;
    }

    return 0;
}

static int bb_trackball_init(const struct device *dev) {
    const struct bb_trackball_config *config = dev->config;
    struct bb_trackball_data *data = dev->data;
    int ret;

    data->dev = dev;
    k_work_init_delayable(&data->report_work, bb_trackball_report_work);
    k_work_init_delayable(&data->probe_work, bb_trackball_probe_work);
    k_work_init(&data->btn_work, bb_trackball_btn_work);

    for (uint8_t i = 0; i < BB_DIR_COUNT; i++) {
        data->dirs[i].dev = dev;
        data->dirs[i].dir = i;


        ret = bb_trackball_init_pin(dev, &config->dirs[i], &data->dirs[i].cb,
                                    bb_trackball_dir_isr);
        if (ret < 0) {
            return ret;
        }
    }

    if (config->btn.port != NULL) {
        ret = bb_trackball_init_pin(dev, &config->btn, &data->btn_cb, bb_trackball_btn_isr);
        if (ret < 0) {
            return ret;
        }
    }

    k_work_schedule(&data->probe_work, K_SECONDS(BB_TRACKBALL_PROBE_PERIOD_S));

    return 0;
}

#define BB_TRACKBALL_INST(n)                                                                       \
    static struct bb_trackball_data bb_trackball_data_##n;                                         \
                                                                                                   \
    static const struct bb_trackball_config bb_trackball_config_##n = {                            \
        .dirs =                                                                                    \
            {                                                                                      \
                [BB_DIR_UP] = GPIO_DT_SPEC_INST_GET(n, up_gpios),                                  \
                [BB_DIR_DOWN] = GPIO_DT_SPEC_INST_GET(n, down_gpios),                              \
                [BB_DIR_LEFT] = GPIO_DT_SPEC_INST_GET(n, left_gpios),                              \
                [BB_DIR_RIGHT] = GPIO_DT_SPEC_INST_GET(n, right_gpios),                            \
            },                                                                                     \
        .btn = GPIO_DT_SPEC_INST_GET_OR(n, btn_gpios, {0}),                                        \
        .report_interval_ms = DT_INST_PROP(n, report_interval_ms),                                 \
        .invert_x = DT_INST_PROP(n, invert_x),                                                     \
        .invert_y = DT_INST_PROP(n, invert_y),                                                     \
        .swap_xy = DT_INST_PROP(n, swap_xy),                                                       \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(n, bb_trackball_init, NULL, &bb_trackball_data_##n,                      \
                          &bb_trackball_config_##n, POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,       \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(BB_TRACKBALL_INST)
