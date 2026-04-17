#pragma once

#include <stdint.h>

/**
 * @brief Read temperature and humidity scaled by 256 (fixed-point, 1/256 units).
 *
 * Retrieves the current temperature and relative humidity from the HDC2080 sensor.
 * The returned values are scaled by 256, i.e.:
 * - temperature = real_temperature * 256
 * - humidity    = real_humidity * 256
 *
 * Example:
 * - 25.5°C  -> 6528
 * - 50.0%RH -> 12800
 *
 * @param[out] temp     Pointer to store temperature (scaled by 256)
 * @param[out] humidity Pointer to store humidity (scaled by 256)
 *
 * @retval 0        Success
 * @retval -errno   Error code (Zephyr convention)
 */
int hdc2080_get_temp_humidity_x256(int16_t *temp, int16_t *humidity);


/**
 * @brief Read temperature and humidity scaled by 100 (centi-units, 0.01 resolution).
 *
 * Retrieves the current temperature and relative humidity from the HDC2080 sensor.
 * The returned values are scaled by 100, i.e.:
 * - temperature = real_temperature * 100
 * - humidity    = real_humidity * 100
 *
 * Example:
 * - 25.50°C -> 2550
 * - 50.00%RH -> 5000
 *
 * @param[out] temp     Pointer to store temperature (scaled by 100)
 * @param[out] humidity Pointer to store humidity (scaled by 100)
 *
 * @retval 0        Success
 * @retval -errno   Error code (Zephyr convention)
 */
int hdc2080_get_temp_humidity_x100(int16_t *temp, int16_t *humidity);


int hdc2080_init(void);
