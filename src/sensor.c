#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define I2C_NODE       DT_NODELABEL(i2c0)
#define HDC2080_NODE    DT_CHILD(I2C_NODE, hdc2080_40)

int hdc2080_get_temp_humidity(float *temp_c, float *hum_pc)
{
    const struct device *dev = DEVICE_DT_GET(HDC2080_NODE);

    int ret = device_init(dev);
    if (ret && ret != -EALREADY) {
        return -ENODEV;
    }

    if (!device_is_ready(dev))
        return -ENODEV;

    ret = sensor_sample_fetch(dev);
    if (ret < 0)
        return ret;

    struct sensor_value t, h;

    ret = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &t);
    if (ret < 0)
        return ret;

    ret = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &h);
    if (ret < 0)
        return ret;

    *temp_c = sensor_value_to_double(&t);
    *hum_pc = sensor_value_to_double(&h);

    // pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_OFF);

    return 0;
}
