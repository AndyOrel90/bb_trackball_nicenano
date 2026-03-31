#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zmk/pointing.h>
#include <zmk/event_manager.h>
#include <zmk/events/pointing_event.h>

LOG_MODULE_REGISTER(bb_gpio_trackball, LOG_LEVEL_INF);

struct bb_gpio_trackball_config {
    struct gpio_dt_spec up;
    struct gpio_dt_spec down;
    struct gpio_dt_spec left;
    struct gpio_dt_spec right;
    struct gpio_dt_spec click;
    uint16_t debounce_ms;
    uint16_t poll_ms;
};

struct bb_gpio_trackball_data {
    int last_up;
    int last_down;
    int last_left;
    int last_right;
    int last_click;
};

static int bb_gpio_trackball_init(const struct device *dev) {
    const struct bb_gpio_trackball_config *cfg = dev->config;

    gpio_pin_configure_dt(&cfg->up, GPIO_INPUT);
    gpio_pin_configure_dt(&cfg->down, GPIO_INPUT);
    gpio_pin_configure_dt(&cfg->left, GPIO_INPUT);
    gpio_pin_configure_dt(&cfg->right, GPIO_INPUT);
    gpio_pin_configure_dt(&cfg->click, GPIO_INPUT);

    return 0;
}

static void bb_gpio_trackball_poll(struct k_work *work) {
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(bb_trackball));
    const struct bb_gpio_trackball_config *cfg = dev->config;
    struct bb_gpio_trackball_data *data = dev->data;

    int up = gpio_pin_get_dt(&cfg->up);
    int down = gpio_pin_get_dt(&cfg->down);
    int left = gpio_pin_get_dt(&cfg->left);
    int right = gpio_pin_get_dt(&cfg->right);
    int click = gpio_pin_get_dt(&cfg->click);

    int dx = 0;
    int dy = 0;

    if (up == 0) dy += 1;
    if (down == 0) dy -= 1;
    if (left == 0) dx -= 1;
    if (right == 0) dx += 1;

    if (dx != 0 || dy != 0) {
        struct zmk_pointing_event evt = {
            .dx = dx,
            .dy = dy,
            .buttons = 0,
        };
        ZMK_EVENT_RAISE(new_zmk_pointing_event(&evt));
    }

    if (click == 0 && data->last_click != 0) {
        struct zmk_pointing_event evt = {
            .dx = 0,
            .dy = 0,
            .buttons = ZMK_MOUSE_BUTTON_LEFT,
        };
        ZMK_EVENT_RAISE(new_zmk_pointing_event(&evt));
    }

    data->last_click = click;

    k_work_schedule((struct k_work_delayable *)work, K_MSEC(cfg->poll_ms));
}

static struct k_work_delayable poll_work;

static int bb_gpio_trackball_start(const struct device *dev) {
    const struct bb_gpio_trackball_config *cfg = dev->config;
    k_work_init_delayable(&poll_work, bb_gpio_trackball_poll);
    k_work_schedule(&poll_work, K_MSEC(cfg->poll_ms));
    return 0;
}

static const struct bb_gpio_trackball_config bb_gpio_trackball_cfg = {
    .up = GPIO_DT_SPEC_GET(DT_NODELABEL(bb_trackball), up_gpios),
    .down = GPIO_DT_SPEC_GET(DT_NODELABEL(bb_trackball), down_gpios),
    .left = GPIO_DT_SPEC_GET(DT_NODELABEL(bb_trackball), left_gpios),
    .right = GPIO_DT_SPEC_GET(DT_NODELABEL(bb_trackball), right_gpios),
    .click = GPIO_DT_SPEC_GET(DT_NODELABEL(bb_trackball), click_gpios),
    .debounce_ms = DT_PROP(DT_NODELABEL(bb_trackball), debounce_ms),
    .poll_ms = DT_PROP(DT_NODELABEL(bb_trackball), poll_ms),
};

static struct bb_gpio_trackball_data bb_gpio_trackball_data;

DEVICE_DT_DEFINE(DT_NODELABEL(bb_trackball),
                 bb_gpio_trackball_init,
                 NULL,
                 &bb_gpio_trackball_data,
                 &bb_gpio_trackball_cfg,
                 POST_KERNEL,
                 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                 bb_gpio_trackball_start);