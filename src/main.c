#include "sensor.h"
#include "ble_server.h"
#include "rtc.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/hwinfo.h>


LOG_MODULE_REGISTER(app_main, LOG_LEVEL_DBG);


//#define NOBLINK
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);
int blink(int count, int duration) {
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

int megablink(int countA, int countB, int spacing) {
    #ifndef NOBLINK
    for(int i=0;i<countB;++i) {
        blink(countA, 200);
        k_msleep(spacing);
    }
    #endif
    return 0;
}

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);

int main(void)
{
    uint32_t reset_cause = 0;
    int err = hwinfo_get_reset_cause(&reset_cause);
    if (err == 0) {
        hwinfo_clear_reset_cause();
    }

    const bool woke_from_sysoff = reset_cause & RESET_LOW_POWER_WAKE;
    if (!woke_from_sysoff) {
        LOG_INF("Power-off boot");
        k_msleep(500); // sleep for half a second (let bulk capacitor charge up)
        blink(1,100);  // indicator that thing is working
        k_msleep(500); // more sleep for capacitor
    } else {
        LOG_INF("Deep sleep (system off) wake");
    }

    // If we are woken up by a button press then reset the clock 
    // because alarm is aligned to minute boundary
    bool button_is_pressed = woke_from_sysoff && gpio_pin_get_dt(&btn) == 0; // SW0 is active-low
    if (button_is_pressed) {
        LOG_INF("Woken by button press");
    }
    if(intinitialize_rtc(false)) {
        LOG_ERR("RTC init failed");
        megablink(3,3, 300);
        k_msleep(5000);
        sys_reboot(SYS_REBOOT_COLD);
        return 0;
    }

    print_rtc_time();

    if(!woke_from_sysoff) {
        enable_rtc_pit(SENSOR_TRANSMIT_PERIOD);
    }

    if(btadv(!woke_from_sysoff || button_is_pressed)) {
        LOG_ERR("BT adv failed");
        megablink(4,4, 300);
        k_msleep(5000);
        sys_reboot(SYS_REBOOT_COLD);
        return 0;
    }

    int status = set_alarm_and_sleep(2);
    if(status) {
        LOG_ERR("Deep sleep (System OFF) failed");
        megablink(2,2, 100);
        k_msleep(5000);
        sys_reboot(SYS_REBOOT_COLD);
    }

    return 0;
}