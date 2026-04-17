#include "ble_server.h"
#include "rtc.h"
#include "vsense.h"
#include "deep_sleep.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/dfu/mcuboot.h>


LOG_MODULE_REGISTER(app_main, LOG_LEVEL_DBG);

static const struct gpio_dt_spec pair_button = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

static bool pair_button_is_pressed(void);
static int blink(int count, int duration);
static int megablink(int countA, int countB, int spacing);
static int confirm_image_once(void);
static void blink_and_restart(int ser_num);
static bool get_is_low_power_wake(void);
static int initialize_peripherals(bool fast);

void main(void)
{
    int ret = 0;
    const bool is_lp_wake = get_is_low_power_wake();

    ret = initialize_peripherals(is_lp_wake);
    if (ret) {
        blink_and_restart(3);
        return;
    }

    confirm_image_once();
    
    bool pairing_mode = is_lp_wake || pair_button_is_pressed();
    if(btadv(pairing_mode)) {
        blink_and_restart(4);
        return;
    }

    int status = enter_deep_sleep();
    if(status) {
        blink_and_restart(5);
    }

    return;
}

static bool pair_button_is_pressed(void)
{
    int v = gpio_pin_get_dt(&pair_button);
    return v > 0;   /* logical active level */
}

static int blink(int count, int duration) {
    #ifndef NOBLINK
    if (!device_is_ready(led.port)) return -ENODEV;

    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    for(int i=0;i<count;++i) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        k_msleep(duration);
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
        k_msleep(duration);
    }
    #endif
    
    return 0;
}

static int megablink(int countA, int countB, int spacing) {
    #ifndef NOBLINK
    for(int i=0;i<countB;++i) {
        blink(countA, 200);
        k_msleep(spacing);
    }
    #endif
    return 0;
}

static int confirm_image_once(void)
{
    int rc;

    if (boot_is_img_confirmed()) {
        return 0;
    }

    rc = boot_write_img_confirmed();
    if (rc) {
        return rc;
    }

    return 0;
}

static void blink_and_restart(int ser_num) {
    megablink(ser_num, 3, 300);
    k_msleep(5000);
    sys_reboot(SYS_REBOOT_COLD);
    while(1) {
        k_sleep(K_FOREVER);
    }
}

static bool get_is_low_power_wake(void) {
    uint32_t reset_cause = 0;
    int ret = hwinfo_get_reset_cause(&reset_cause);
    if (ret == 0) {
        hwinfo_clear_reset_cause();
    }

    return reset_cause & RESET_LOW_POWER_WAKE;
}

static int initialize_peripherals(bool fast) {
    int ret = 0;
    if (!fast) {
        LOG_INF("Normal boot");
        blink(1, 200);  // indicator that thing is working

        ret = enable_rtc_pit(SENSOR_TRANSMIT_PERIOD);
        if(ret) {
            LOG_ERR("RTC PIT init failed");
            return 0;
        }        
    } else {
        LOG_INF("Fast boot (wake from System off)");
    }

    return 0;
}