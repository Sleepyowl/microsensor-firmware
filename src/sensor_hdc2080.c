#include "sensor_hdc2080.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_sensor, LOG_LEVEL_DBG);

#define HDC2080_NODE    DT_NODELABEL(hdc2080)
static const struct device *dev = DEVICE_DT_GET(HDC2080_NODE);

int hdc2080_get_raw_temp_humidity(struct sensor_value *t, struct sensor_value *h)
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

    ret = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, t);
    if (ret < 0) { 
        LOG_ERR("HDC2080 sensor_channel_get(SENSOR_CHAN_AMBIENT_TEMP) failed: %d", ret);
        return ret;
    }

    ret = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, h);
    if (ret < 0) { 
        LOG_ERR("HDC2080 sensor_channel_get(SENSOR_CHAN_HUMIDITY) failed: %d", ret);
        return ret;
    }

    return 0;
}

int hdc2080_get_temp_humidity_x100(int16_t *temp, int16_t *humidity) {
    int ret = 0;
    struct sensor_value t,h;
    ret = hdc2080_get_raw_temp_humidity(&t, &h);
    if (ret) {
        return ret;
    }

    *temp = (int16_t)((t.val1 * 100) + (t.val2 + 5000) / 10000);
    *humidity = (int16_t)((h.val1 * 100) + (h.val2 + 5000) / 10000);

    return 0;
}

int hdc2080_get_temp_humidity_x256(int16_t *temp, int16_t *humidity) {
    int ret = 0;
    struct sensor_value t,h;
    ret = hdc2080_get_raw_temp_humidity(&t, &h);
    if (ret) {
        return ret;
    }

    *temp = (int16_t)(t.val1 * 256 + (t.val2 + 1953) / 3906);
    *humidity = (int16_t)(h.val1 * 256 + (h.val2 + 1953) / 3906);

    return 0;
}
