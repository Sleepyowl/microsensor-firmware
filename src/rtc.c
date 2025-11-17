#include <zephyr/device.h>
#include <zephyr/devicetree.h>
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


int blink(int count, int duration);
int megablink(int countA, int countB, int spacing);

#define I2C_NODE       DT_NODELABEL(i2c0)
#define RV3028_NODE    DT_CHILD(I2C_NODE, rv3028_52)

static const struct device *i2c0 = DEVICE_DT_GET(I2C_NODE);
static const struct device *rtc = DEVICE_DT_GET_ONE(microcrystal_rv3028);
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

//DEVICE_DT_GET(RV3028_NODE);

/* INT (~INT) GPIO from DTS */
#define RV3028_INT_NODE  DT_GPIO_CTLR(RV3028_NODE, int_gpios)
#define RV3028_INT_PIN   DT_GPIO_PIN(RV3028_NODE, int_gpios)
#define RV3028_INT_FLAGS DT_GPIO_FLAGS(RV3028_NODE, int_gpios)
static const struct device *const int_gpio = DEVICE_DT_GET(RV3028_INT_NODE);

/* Dummy cb: required so driver sets AIE and wires the IRQ */
static void rv3028_alarm_cb(const struct device *dev, uint16_t id, void *user)
{
	ARG_UNUSED(dev); ARG_UNUSED(id); ARG_UNUSED(user);
}

/* minutes → next occurrence (HH:MM), day wildcarded */
int set_alarm_and_sleep(int minutes)
{
    if (!device_is_ready(rtc)) {
        megablink(6, 3, 400);
        return -ENODEV;
    }
    if (!device_is_ready(int_gpio)) {
        megablink(7, 3, 400);
        return -ENODEV;
    }

    struct rtc_time now;
    if (rtc_get_time(rtc, &now)) {
        megablink(2, 3, 400);
        return -EIO;
    }

    int total = (now.tm_hour * 60 + now.tm_min + minutes) % (24 * 60);
    struct rtc_time at = {0};
    at.tm_hour = total / 60;
    at.tm_min  = total % 60;

    uint16_t mask = RTC_ALARM_TIME_MASK_HOUR | RTC_ALARM_TIME_MASK_MINUTE;
    if (rtc_alarm_set_time(rtc, 0, mask, &at)) {
        megablink(3, 3, 400);
        return -EIO;
    }

    if (rtc_alarm_set_callback(rtc, 0, rv3028_alarm_cb, NULL)) {
        megablink(4, 3, 400);
        return -EIO;
    }

    /* RTC interrupt line */
    gpio_pin_configure(int_gpio, RV3028_INT_PIN, GPIO_INPUT | GPIO_PULL_UP);
    nrf_gpio_cfg_sense_input(RV3028_INT_PIN,
                             NRF_GPIO_PIN_PULLUP,
                             NRF_GPIO_PIN_SENSE_LOW);

    // button wake
    if (device_is_ready(btn.port)) {
        gpio_pin_configure_dt(&btn, GPIO_INPUT);
        nrf_gpio_cfg_sense_input(btn.pin,
                                 NRF_GPIO_PIN_NOPULL,
                                 NRF_GPIO_PIN_SENSE_LOW);
    }

    // power off
    k_msleep(100);
    nrf_power_system_off(NRF_POWER);
    return 0;
}

int intinitialize_rtc(bool setTime)
{

    int ret = device_init(rtc);
    if (ret && ret != -EALREADY) {
        megablink(1,15,500);
        return -ENODEV;
    }

    if (!device_is_ready(rtc)) {
        megablink(3,5,500);
        return -ENODEV;
    }

    struct rtc_time tm;
    ret = rtc_get_time(rtc, &tm);

    if (ret == -ENODATA || setTime) {
        /* RTC has invalid time (probably power loss). Set default. */
        struct rtc_time init = {
            .tm_year = 2025 - 1900,  /* years since 1900 */
            .tm_mon  = 0,             /* January = 0 */
            .tm_mday = 1,
            .tm_hour = 0,
            .tm_min  = 0,
            .tm_sec  = 0,
        };

        ret = rtc_set_time(rtc, &init);
        if (ret) {
            return ret;
        }
    } else if (ret) {
        megablink(5,5,500);
        return ret;
    }

    return 0;
}

int get_rtc_unix_time(uint32_t* unix_timestamp) {
    if (unix_timestamp) {
        char buf[sizeof(struct rtc_time)]; // anti-aliasing
        int ret = rtc_get_time(rtc, (struct rtc_time*)buf);
        if (ret) {
            return ret;
        }
        *unix_timestamp = timeutil_timegm64((struct tm*)buf);
    }

    return 0;
}