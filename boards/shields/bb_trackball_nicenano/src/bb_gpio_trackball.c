#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/pointing_event.h>

LOG_MODULE_REGISTER(bb_gpio_trackball, LOG_LEVEL_INF);

struct bb_cfg {
    struct gpio_dt_spec up;
    struct gpio_dt_spec down;
    struct gpio_dt_spec left;
    struct gpio_dt_spec right;
    struct gpio_dt_spec click;
    int move_step;
};

struct bb_data {
    int8_t dx;
    int8_t dy;
    struct gpio_callback up_cb, down_cb, left_cb, right_cb, click_cb;
    bool last_click;
};

static const struct bb_cfg *cfg(const struct device *dev) { return dev->config; }
static struct bb_data *data(const struct device *dev) { return dev->data; }

/* --- helpers --- */

static void pulse_y(struct bb_data *d, int dir, int step) {
    d->dy += dir * step;
}

static void pulse_x(struct bb_data *d, int dir, int step) {
    d->dx += dir * step;
}

/* --- ISRs --- */

#define MAKE_ISR(name, action) \
    static void name(const struct device *port, struct gpio_callback *cb, uint32_t pins) { \
        ARG_UNUSED(port); \
        ARG_UNUSED(pins); \
        const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(blackberry_trackball)); \
        struct bb_data *d = data(dev); \
        const struct bb_cfg *c = cfg(dev); \
        action; \
    }

MAKE_ISR(up_isr,    pulse_y(d, +1, c->move_step));
MAKE_ISR(down_isr,  pulse_y(d, -1, c->move_step));
MAKE_ISR(left_isr,  pulse_x(d, -1, c->move_step));
MAKE_ISR(right_isr, pulse_x(d, +1, c->move_step));

/* Click ISR: emit press + release */
static void click_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(port);
    ARG_UNUSED(pins);

    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(blackberry_trackball));
    struct bb_data *d = data(dev);

    /* Emit click press */
    struct zmk_pointing_event evt_down = {
        .dx = 0,
        .dy = 0,
        .buttons = ZMK_MOUSE_BUTTON_LEFT,
    };
    ZMK_EVENT_RAISE(new_zmk_pointing_event(&evt_down));

    /* Emit click release */
    struct zmk_pointing_event evt_up = {
        .dx = 0,
        .dy = 0,
        .buttons = 0,
    };
    ZMK_EVENT_RAISE(new_zmk_pointing_event(&evt_up));
}

/* --- periodic reporting --- */

static void report_work(struct k_work *work) {
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(blackberry_trackball));
    struct bb_data *d = data(dev);

    if (d->dx || d->dy) {
        struct zmk_pointing_event evt = {
            .dx = d->dx,
            .dy = d->dy,
            .buttons = 0,
        };
        ZMK_EVENT_RAISE(new_zmk_pointing_event(&evt));

        d->dx = 0;
        d->dy = 0;
    }
}

K_WORK_DEFINE(bb_report, report_work);

static void timer_fn(struct k_timer *t) {
    ARG_UNUSED(t);
    k_work_submit(&bb_report);
}

K_TIMER_DEFINE(bb_timer, timer_fn, NULL);

/* --- init --- */

static int bb_init(const struct device *dev) {
    const struct bb_cfg *c = cfg(dev);
    struct bb_data *d = data(dev);

    const struct gpio_dt_spec *pins[] = {
        &c->up, &c->down, &c->left, &c->right, &c->click
    };

    struct gpio_callback *cbs[] = {
        &d->up_cb, &d->down_cb, &d->left_cb, &d->right_cb, &d->click_cb
    };

    gpio_callback_handler_t handlers[] = {
        up_isr, down_isr, left_isr, right_isr, click_isr
    };

    for (int i = 0; i < 5; i++) {
        if (!device_is_ready(pins[i]->port)) {
            return -ENODEV;
        }

        int ret = gpio_pin_configure_dt(pins[i], GPIO_INPUT);
        if (ret < 0) {
            return ret;
        }

        gpio_init_callback(cbs[i], handlers[i], BIT(pins[i]->pin));
        ret = gpio_add_callback(pins[i]->port, cbs[i]);
        if (ret < 0) {
            return ret;
        }

        ret = gpio_pin_interrupt_configure_dt(pins[i], GPIO_INT_EDGE_FALLING);
        if (ret < 0) {
            return ret;
        }
    }

    k_timer_start(&bb_timer, K_MSEC(5), K_MSEC(5));
    return 0;
}

#define BB_INIT(inst) \
    static const struct bb_cfg bb_cfg_##inst = { \
        .up = GPIO_DT_SPEC_INST_GET(inst, up_gpios), \
        .down = GPIO_DT_SPEC_INST_GET(inst, down_gpios), \
        .left = GPIO_DT_SPEC_INST_GET(inst, left_gpios), \
        .right = GPIO_DT_SPEC_INST_GET(inst, right_gpios), \
        .click = GPIO_DT_SPEC_INST_GET(inst, click_gpios), \
        .move_step = DT_INST_PROP(inst, move_step), \
    }; \
    static struct bb_data bb_data_##inst; \
    DEVICE_DT_INST_DEFINE(inst, bb_init, NULL, &bb_data_##inst, &bb_cfg_##inst, \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);

DT_INST_FOREACH_STATUS_OKAY(BB_INIT);