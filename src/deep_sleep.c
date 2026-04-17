#include "deep_sleep.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

LOG_MODULE_REGISTER(app_dsleep, LOG_LEVEL_DBG);


static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);
static const struct gpio_dt_spec int_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(rv3028), int_gpios);

int enter_deep_sleep(void) {
    if (!device_is_ready(int_gpio.port)) {
        LOG_ERR("RTC INT GPIO is not ready");
        return -ENODEV;
    }

    if (!device_is_ready(btn.port)) {
        LOG_ERR("BUTTON GPIO is not ready");
        return -ENODEV;
    }

    // Configure RTC INT wake source
    gpio_pin_configure_dt(&int_gpio, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&int_gpio, GPIO_INT_LEVEL_ACTIVE);

    // Configure button wake source
    gpio_pin_configure_dt(&btn, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_LEVEL_ACTIVE);

    // Deep Sleep (System OFF)
    LOG_INF("Entering deep sleep (System OFF)");
    sys_poweroff();

    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}