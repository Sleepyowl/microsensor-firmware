#pragma once

#include <stdint.h>

/** Zigbee application endpoint number for the sensor. */
#define SENSOR_ENDPOINT  10

/** Sampling / reporting interval in milliseconds. */
#define SENSOR_SAMPLING_INTERVAL_MS  60000U



int zigbee_sensor_start(void);