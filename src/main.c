#include "ble_server.h"
#include "rtc.h"
#include "vsense.h"
#include "deep_sleep.h"
#include "sensor_hdc2080.h"
#include "zigbee_sensor.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/pm.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/settings/settings.h>


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
static int configure_button0_as_reset(void);
static int user_menu(void);
static int save_device_mode(void);

int device_mode = 1;

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

    k_msleep(300);
    if(pair_button_is_pressed()) {
        user_menu();
    }
    
    if (device_mode == 0) {
        LOG_INF("Device is in BLE mode");
        bool pairing_mode = is_lp_wake || pair_button_is_pressed();
        if(btadv(pairing_mode)) {
            blink_and_restart(4);
            return;
        }

        ret = enter_deep_sleep();
        if(ret) {
            blink_and_restart(5);
        }
    } else {
        LOG_INF("Device is in ZigBee mode");
        zigbee_sensor_start();
        configure_button0_as_reset();
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

    settings_subsys_init();
    settings_load();

    ret = rtc_init(!fast);
    if (ret) {
        LOG_ERR("RTC init failed: %d", ret);
    }

    ret = hdc2080_init();
    if (ret) {
        LOG_ERR("HDC2080 init failed: %d", ret);
    }

	if (!gpio_is_ready_dt(&pair_button)) {
		LOG_ERR("button0 GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&pair_button, GPIO_INPUT);
	if (ret) {
		LOG_ERR("button0 configure failed (%d)", ret);
		return ret;
	}

    if (!fast) {
        LOG_INF("Normal boot");
        blink(1, 200);  // indicator that thing is working

        ret = enable_rtc_pit(SENSOR_TRANSMIT_PERIOD);
        if(ret) {
            LOG_ERR("RTC PIT init failed");
            // return ret;
            return 0; // DO NOT COMMIT
        }        
    } else {
        LOG_INF("Fast boot (wake from System off)");
    }


    return 0;
}

static struct gpio_callback button0_cb;
static void button0_reset_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	sys_reboot(SYS_REBOOT_COLD);
}

static int configure_button0_as_reset(void)
{
	int rc;

	if (!gpio_is_ready_dt(&pair_button)) {
		LOG_ERR("button0 GPIO not ready");
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&pair_button, GPIO_INPUT);
	if (rc) {
		LOG_ERR("button0 configure failed (%d)", rc);
		return rc;
	}

	rc = gpio_pin_interrupt_configure_dt(&pair_button, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc) {
		LOG_ERR("button0 interrupt configure failed (%d)", rc);
		return rc;
	}

	gpio_init_callback(&button0_cb, button0_reset_callback, BIT(pair_button.pin));
	gpio_add_callback(pair_button.port, &button0_cb);

    return 0;
}

static int user_menu(void)
{
    LOG_INF("Entering mode select...");
    int64_t start;

    /* --- Initial 5s wait --- */
    start = k_uptime_get();
    while (k_uptime_get() - start < 5000) {
        if (!pair_button_is_pressed()) {
            LOG_INF("Entering mode select aborted");
            return 0;
        }
        k_msleep(50);
    }

    while (pair_button_is_pressed()) {
        blink(1, 50);
        k_msleep(50);
    }

    LOG_INF("Entered mode select.");

    /* --- Menu loop --- */
    start = k_uptime_get();
    while (1) {

        /* Indicate current mode */
        if (device_mode == 0) {
            megablink(1, 1, 100); /* BLE */
        } else {
            megablink(2, 1, 100); /* ZigBee */
        }

        /* Short window to catch button press */
        int64_t poll_start = k_uptime_get();
        while (k_uptime_get() - poll_start < 200) {
            if (pair_button_is_pressed()) {
                device_mode = !device_mode;   /* toggle */
                save_device_mode();
                start = k_uptime_get();       /* reset inactivity timer */

                /* debounce: wait release */
                while (pair_button_is_pressed()) {
                    k_msleep(20);
                }
                break;
            }
            k_msleep(20);
        }

        /* Exit after 5s inactivity */
        if (k_uptime_get() - start >= 5000) {
            blink(1, 1000);
            return 0;
        }
    }
}

static int settings_set(const char *name, size_t len_rd,
                        settings_read_cb read_cb, void *cb_arg)
{
    if (strcmp(name, "mode") == 0) {
        if (len_rd != sizeof(device_mode)) {
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, &device_mode, sizeof(device_mode));
        return (rc >= 0) ? 0 : rc;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(device, "device", NULL, settings_set, NULL, NULL);

static int save_device_mode(void)
{
    LOG_INF("Saving device mode = %d", device_mode);
    return settings_save_one("device/mode", &device_mode, sizeof(device_mode));
}