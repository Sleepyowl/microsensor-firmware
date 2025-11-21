#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rv3028_timer_addon, LOG_LEVEL_DBG);

/*
    IMPORTANT

    Code below depends internal workings of RV3028 driver (drivers/rtc/rtc_v3028.c):
        - to access semaphore 
        - to access relevant i2c device

    If something stops working, check if RV3028 driver changed
*/

#define DT_DRV_COMPAT microcrystal_rv3028
#if DT_ANY_INST_HAS_PROP_STATUS_OKAY(int_gpios) &&                                                 \
	(defined(CONFIG_RTC_ALARM) || defined(CONFIG_RTC_UPDATE))
#define RV3028_INT_GPIOS_IN_USE 1
#endif


struct rv3028_config {
	const struct i2c_dt_spec i2c;
#ifdef RV3028_INT_GPIOS_IN_USE
	struct gpio_dt_spec gpio_int;
#endif /* RV3028_INT_GPIOS_IN_USE */
};

struct rv3028_data {
	struct k_sem lock;
};


static void rv3028_lock_sem(const struct device *dev)
{
	struct rv3028_data *data = dev->data;

	(void)k_sem_take(&data->lock, K_FOREVER);
}

static void rv3028_unlock_sem(const struct device *dev)
{
	struct rv3028_data *data = dev->data;

	k_sem_give(&data->lock);
}

#define RV3028_REG_STATUS               0x0E
#define RV3028_REG_CONTROL1             0x0F
#define RV3028_REG_CONTROL2             0x10
#define RV3028_REG_TV0                  0x0A
#define RV3028_REG_TV1                  0x0B
#define RV3028_REG_TV1_MASK             GENMASK(3, 0)
#define RV3028_REG_TS0                  0x0C
#define RV3028_REG_TS1                  0x0D
#define RV3028_REG_TS1_MASK             GENMASK(3, 0)

#define RV3028_CONTROL1_TD              GENMASK(1, 0)
#define RV3028_CONTROL1_TE              BIT(2)
#define RV3028_CONTROL1_TRPT            BIT(7)
#define RV3028_CONTROL2_TIE             BIT(4)
#define RV3028_STATUS_TF                BIT(3)

int rv3028_enable_periodic_interrupt(const struct device *dev, uint8_t freq, uint16_t period) {

    if (freq > 3 || period > 0xFFF) {
        return -EINVAL;
    }

    const struct rv3028_config *config = dev->config;
    int err = 0;
    rv3028_lock_sem(dev);

    /*
    1. Initialize bits TE, TIE and TF to 0. In that order, to prevent inadvertent interrupts on INT pin.
    2. Set TRPT bit to 1 if periodic countdown is needed (Repeat Mode).
    3. Choose the Timer Clock Frequency and write the corresponding value in the TD field.
    4. Choose the Countdown Period based on the Timer Clock Frequency, and write the corresponding Timer
    Value to the registers Timer Value 0 (0Ah) and Timer Value 1 (0Bh). See following table.
    5. Set the TIE bit to 1 if you want to get a hardware interrupt on INT pin.
    6. Set CTIE bit to 1 to enable clock output when a timer interrupt occurs. See also CLOCK OUTPUT
    SCHEME.
    7. Set the TIE and CLKOE bits to 1 and the FD field to 110 if you want to get the timer signal on CLKOUT.
    8. Set the TE bit from 0 to 1 to start the Periodic Countdown Timer. The countdown starts at the rising edge of
    the SCL signal after Bit 0 of the Address 0Fh is transferred. See subsequent Figure that shows the start
    timing.
     */

    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_CONTROL1, RV3028_CONTROL1_TE, 0);
	if (err) {
		LOG_ERR("failed to clear RV3028_CONTROL1_TE: %d", err);
		return err;
	}


    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_CONTROL2, RV3028_CONTROL2_TIE, 0);
	if (err) {
		LOG_ERR("failed to clear RV3028_CONTROL2_TIE: %d", err);
		return err;
	}


    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_STATUS, RV3028_STATUS_TF, 0);
	if (err) {
		LOG_ERR("failed to clear RV3028_STATUS_TF: %d", err);
		return err;
	}

    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_CONTROL1, RV3028_CONTROL1_TRPT, RV3028_CONTROL1_TRPT);
	if (err) {
		LOG_ERR("failed to set RV3028_CONTROL1_TRPT: %d", err);
		return err;
	}

    // 0b11 =  1/60Hz
    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_CONTROL1, RV3028_CONTROL1_TD, freq);
	if (err) {
		LOG_ERR("failed to set RV3028_CONTROL1_TD: %d", err);
		return err;
	}

    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_TV0, 0xFF, period & 0xFF);
	if (err) {
		LOG_ERR("failed to set RV3028_REG_TV0: %d", err);
		return err;
	}

    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_TV1, RV3028_REG_TV1_MASK, period >> 8);
	if (err) {
		LOG_ERR("failed to set RV3028_REG_TV1: %d", err);
		return err;
	}

    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_CONTROL2, RV3028_CONTROL2_TIE, RV3028_CONTROL2_TIE);
	if (err) {
		LOG_ERR("failed to set RV3028_CONTROL2_TIE: %d", err);
		return err;
	}

    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_CONTROL1, RV3028_CONTROL1_TE, RV3028_CONTROL1_TE);
	if (err) {
		LOG_ERR("failed to set RV3028_CONTROL1_TE: %d", err);
		return err;
	}

    rv3028_unlock_sem(dev);
    return 0;
}


int rv3028_clear_tf(const struct device *dev) {
    const struct rv3028_config *config = dev->config;
    int err = 0;
    rv3028_lock_sem(dev);

    err = i2c_reg_update_byte_dt(&config->i2c, RV3028_REG_STATUS, RV3028_STATUS_TF, 0);
	if (err) {
		LOG_ERR("failed to clear RV3028_STATUS_TF: %d", err);
		return err;
	}

    rv3028_unlock_sem(dev);
    return 0;
}


int rv3028_get_timer_status(const struct device *dev, uint16_t *timer_status) {
    const struct rv3028_config *config = dev->config;
    int err = 0;
    rv3028_lock_sem(dev);
    uint8_t buf[2] = {0, 0};

    err = i2c_reg_read_byte_dt(&config->i2c, RV3028_REG_TS0, buf);
	if (err) {
		LOG_ERR("failed to read RV3028_REG_TS0: %d", err);
		return err;
	}

    err = i2c_reg_read_byte_dt(&config->i2c, RV3028_REG_TS1, buf + 1);
	if (err) {
		LOG_ERR("failed to read RV3028_REG_TS1: %d", err);
		return err;
	}

    // LOG_DBG("RV3028_REG_TS* = [%u, %u]", buf[0], buf[1]);

    *timer_status = buf[0] | ((uint16_t)(buf[1] & RV3028_REG_TS1_MASK) << 8);

    rv3028_unlock_sem(dev);
    return 0;
}