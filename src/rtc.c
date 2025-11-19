#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/timeutil.h>
#include <hal/nrf_power.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <hal/nrf_gpio.h>
#include <stdint.h>


LOG_MODULE_REGISTER(app_rtc, LOG_LEVEL_DBG);

int blink(int count, int duration);
int megablink(int countA, int countB, int spacing);

#define RV3028_NODE    DT_NODELABEL(rv3028)
static const struct device *rtc = DEVICE_DT_GET(RV3028_NODE);
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);

/* INT (~INT) GPIO from DTS */
static const struct gpio_dt_spec int_gpio = GPIO_DT_SPEC_GET(RV3028_NODE, int_gpios);

static void rv3028_alarm_cb(const struct device *dev, uint16_t id, void *user)
{
	ARG_UNUSED(dev); ARG_UNUSED(id); ARG_UNUSED(user);
}

/* minutes → next occurrence (HH:MM), day wildcarded */
int set_alarm_and_sleep(int minutes)
{
    int err;
    if (!device_is_ready(rtc)) {
        LOG_ERR("RTC is not ready");
        return -ENODEV;
    }

    if (!device_is_ready(int_gpio.port)) {
        LOG_ERR("RTC INT GPIO is not ready");
        return -ENODEV;
    }

    if (!device_is_ready(btn.port)) {
        LOG_ERR("BUTTON GPIO is not ready");
        return -ENODEV;
    }

    struct rtc_time now;
    err = rtc_get_time(rtc, &now);
    if (err) {
        LOG_ERR("RTC getting time failed: %d", err);
        return err;
    }

    int total = (now.tm_hour * 60 + now.tm_min + minutes) % (24 * 60);
    struct rtc_time at = {0};
    at.tm_hour = total / 60;
    at.tm_min  = total % 60;

    uint16_t mask = RTC_ALARM_TIME_MASK_HOUR | RTC_ALARM_TIME_MASK_MINUTE;
    err = rtc_alarm_set_time(rtc, 0, mask, &at);
    if (err) {
        LOG_ERR("RTC setting alarm time failed: %d", err);
        return err;
    }

    err = rtc_alarm_set_callback(rtc, 0, rv3028_alarm_cb, NULL);
    if (err) {
        LOG_ERR("RTC setting alarm callback failed: %d", err);
        return err;
    }

    // Configure RTC INT wake source
    gpio_pin_configure_dt(&int_gpio, GPIO_INPUT);
    nrf_gpio_cfg_sense_input(int_gpio.pin, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

    // Configure button wake source
    gpio_pin_configure_dt(&btn, GPIO_INPUT);
    nrf_gpio_cfg_sense_input(btn.pin, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_LOW);

    // Deep Sleep (System OFF)
    LOG_INF("Entering deep sleep (System OFF)");
    k_msleep(100); // Give logging subsystem time to transmit
    nrf_power_system_off(NRF_POWER);
    return 0;
}

int intinitialize_rtc(bool setTime)
{

    int err = device_init(rtc);
    if (err && err != -EALREADY) {
        LOG_ERR("RTC device failed to initialize: %d", err);
        return -ENODEV;
    }

    if (!device_is_ready(rtc)) {
        LOG_ERR("RTC is not ready");
        return -ENODEV;
    }

    struct rtc_time tm;
    err = rtc_get_time(rtc, &tm);

    if (err == -ENODATA || setTime) {
        LOG_INF("setting RTC time");
        /* RTC has invalid time (probably power loss). Set default. */
        struct rtc_time init = {
            .tm_year = 2025 - 1900,  /* years since 1900 */
            .tm_mon  = 0,             /* January = 0 */
            .tm_mday = 1,
            .tm_hour = 0,
            .tm_min  = 0,
            .tm_sec  = 0,
        };

        err = rtc_set_time(rtc, &init);
        if (err) {
            LOG_ERR("Failed to set RTC time: %d", err);
            return err;
        }
    } else if (err) {
        LOG_ERR("Failed to get RTC time: %d", err);
        return err;
    }

    return 0;
}

int get_rtc_unix_time(uint32_t* unix_timestamp) {
    if (!device_is_ready(rtc)) {
        LOG_ERR("RTC is not ready");
        return -ENODEV;
    }

    if (unix_timestamp) {
        char buf[sizeof(struct rtc_time)]; // anti-aliasing
        int err = rtc_get_time(rtc, (struct rtc_time*)buf);
        if (err) {
            LOG_ERR("Failed to get RTC time: %d", err);
            return err;
        }
        *unix_timestamp = timeutil_timegm64((struct tm*)buf);
    }

    return 0;
}