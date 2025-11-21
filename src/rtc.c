#include "rtc.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/byteorder.h>
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

static void rv3028_alarm_cb(const struct device *dev, uint16_t id, void *user) {
	ARG_UNUSED(dev); ARG_UNUSED(id); ARG_UNUSED(user);
}

/* minutes → next occurrence (HH:MM), day wildcarded */
int set_alarm_and_sleep(int minutes) {
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

    // struct rtc_time now;
    // err = rtc_get_time(rtc, &now);
    // if (err) {
    //     LOG_ERR("RTC getting time failed: %d", err);
    //     return err;
    // }

    // int total = (now.tm_hour * 60 + now.tm_min + minutes) % (24 * 60);
    // struct rtc_time at = {0};
    // at.tm_hour = total / 60;
    // at.tm_min  = total % 60;

    // uint16_t mask = RTC_ALARM_TIME_MASK_HOUR | RTC_ALARM_TIME_MASK_MINUTE;
    // err = rtc_alarm_set_time(rtc, 0, mask, &at);
    // if (err) {
    //     LOG_ERR("RTC setting alarm time failed: %d", err);
    //     return err;
    // }

    // err = rtc_alarm_set_callback(rtc, 0, rv3028_alarm_cb, NULL);
    // if (err) {
    //     LOG_ERR("RTC setting alarm callback failed: %d", err);
    //     return err;
    // }

    // Configure RTC INT wake source
    gpio_pin_configure_dt(&int_gpio, GPIO_INPUT);
    nrf_gpio_cfg_sense_input(int_gpio.pin, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

    // Configure button wake source
    gpio_pin_configure_dt(&btn, GPIO_INPUT);
    nrf_gpio_cfg_sense_input(btn.pin, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_LOW);

    // Deep Sleep (System OFF)
    LOG_INF("Entering deep sleep (System OFF)");
    k_msleep(200); // Give logging subsystem time to transmit
    nrf_power_system_off(NRF_POWER);
    return 0;
}

int rv3028_clear_tf(const struct device *dev);
int intinitialize_rtc(bool setTime) {

    int err = device_init(rtc);
    if (err && err != -EALREADY) {
        LOG_ERR("RTC device failed to initialize: %d", err);
        return -ENODEV;
    }

    if (!device_is_ready(rtc)) {
        LOG_ERR("RTC is not ready");
        return -ENODEV;
    }

    err = rv3028_clear_tf(rtc);
    if(err) {
        LOG_WRN("Failed to clear TF bit: %d", err);
        return -EIO;
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

BUILD_ASSERT(sizeof(struct rtc_time) >= sizeof(struct tm),
             "struct rtc_time must be at least as large as struct tm");

int set_rtc_unix_time(uint32_t unix_timestamp) {
    if (!device_is_ready(rtc))
        return -ENODEV;

    char buf[sizeof(struct rtc_time)];
    memset(buf, 0, sizeof(buf));

    struct tm *tm_ptr = (struct tm *)buf;
    struct rtc_time *rtc_ptr = (struct rtc_time *)buf;

    time_t ts = unix_timestamp;
    if (gmtime_r(&ts, tm_ptr) == NULL) {
        return -EINVAL;
    }

    return rtc_set_time(rtc, rtc_ptr);
}

int print_rtc_time() {
    if (!device_is_ready(rtc))
        return -ENODEV;

    struct rtc_time tm;
    int err = rtc_get_time(rtc, &tm);
    if (err) {
        LOG_ERR("Failed to get RTC time: %d", err);
        return err;
    }
    LOG_INF("RTC: %04d-%02d-%02d %02d:%02d:%02d", 
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec
    );
}


int get_rtc_cts_time(struct cts_current_time *cts)
{
    if (!device_is_ready(rtc))
        return -ENODEV;

    struct rtc_time rt;
    int err = rtc_get_time(rtc, &rt);
    if (err)
        return err;

    /* Convert Zephyr rtc_time → CTS (LE) */
    uint16_t year_le = sys_cpu_to_le16((uint16_t)(rt.tm_year + 1900));
    memcpy(&cts->year, &year_le, sizeof(year_le));

    cts->month  = (uint8_t)(rt.tm_mon + 1);             /* 0–11 → 1–12 */
    cts->day    = (uint8_t)rt.tm_mday;
    cts->hours  = (uint8_t)rt.tm_hour;
    cts->minutes = (uint8_t)rt.tm_min;
    cts->seconds = (uint8_t)rt.tm_sec;

    /* 0=Sun…6=Sat → CTS 1=Mon…7=Sun */
    cts->day_of_week = (rt.tm_wday == 0) ? 7 : rt.tm_wday;

    cts->fractions256 = 0;
    cts->adjust_reason = 0;

    return 0;
}

int set_rtc_cts_time(const struct cts_current_time *cts)
{
    if (!device_is_ready(rtc))
        return -ENODEV;

    struct rtc_time rt;
    memset(&rt, 0, sizeof(rt));

    /* Convert CTS (LE) → rtc_time (host) */
    uint16_t year_host = sys_le16_to_cpu(cts->year);
    rt.tm_year = (int)year_host - 1900;
    rt.tm_mon  = (int)cts->month - 1;                  /* 1–12 → 0–11 */

    rt.tm_mday = (int)cts->day;
    rt.tm_hour = (int)cts->hours;
    rt.tm_min  = (int)cts->minutes;
    rt.tm_sec  = (int)cts->seconds;

    /* CTS 1=Mon…7=Sun → 0=Sun…6=Sat */
    rt.tm_wday = (cts->day_of_week == 7) ? 0 : cts->day_of_week;

    return rtc_set_time(rtc, &rt);
}


#define RV3028_TD_4096HZ 0
#define RV3028_TD_64HZ 1
#define RV3028_TD_1HZ 2
#define RV3028_TD_MINUTE 3
int rv3028_enable_periodic_interrupt(const struct device *dev, uint8_t freq, uint16_t period);
int rv3028_get_timer_status(const struct device *dev, uint16_t *timer_status);

int enable_rtc_pit(uint16_t period_seconds) {
    int err;
    if (!device_is_ready(rtc)) {
        LOG_ERR("RTC is not ready");
        return -ENODEV;
    }

    err = rv3028_enable_periodic_interrupt(rtc, RV3028_TD_64HZ, period_seconds * 64);
    if (err) {
        LOG_ERR("Failed to enable RTC PIT: %d", err);
        return err;
    }

    return 0;
}

int get_rtc_pit_timer_status(uint16_t *timer_status) {
    int err;
    if (!device_is_ready(rtc)) {
        LOG_ERR("RTC is not ready");
        return -ENODEV;
    }

    err = rv3028_get_timer_status(rtc, timer_status);
    if (err) {
        LOG_ERR("Failed to get RTC PIT timer status");
        return err;
    }

    return 0;
}