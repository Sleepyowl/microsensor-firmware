#include "sensor.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_sensor, LOG_LEVEL_DBG);

#define I2C_NODE       DT_NODELABEL(i2c0)
#define HDC2080_NODE    DT_CHILD(I2C_NODE, hdc2080_40)
static const struct device *dev = DEVICE_DT_GET(HDC2080_NODE);

int hdc2080_get_temp_humidity(struct SensorData* data)
{
    int ret = device_init(dev);
    if (ret && ret != -EALREADY) {
        LOG_ERR("HDC2080 couldn't be initialized: %d", ret);
        return -ENODEV;
    }

    if (!device_is_ready(dev)) {
        LOG_ERR("HDC2080 is not ready");
        return -ENODEV;
    }

    ret = sensor_sample_fetch(dev);
    if (ret < 0) { 
        LOG_ERR("HDC2080 sensor_sample_fetch() failed: %d", ret);
        return ret;
    }

    struct sensor_value t, h;

    ret = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &t);
    if (ret < 0) { 
        LOG_ERR("HDC2080 sensor_channel_get(SENSOR_CHAN_AMBIENT_TEMP) failed: %d", ret);
        return ret;
    }

    ret = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &h);
    if (ret < 0) { 
        LOG_ERR("HDC2080 sensor_channel_get(SENSOR_CHAN_HUMIDITY) failed: %d", ret);
        return ret;
    }

    data->magic = BEEEYE_MAGIC;
    data->temp = sensor_value_to_double(&t) * 256;
    data->hum = sensor_value_to_double(&h) * 256;

    return 0;
}
