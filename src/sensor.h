#pragma once

#include <stdint.h>

#define BEEEYE_MAGIC 0xBEEE

struct __attribute__((packed)) SensorData {
    uint16_t magic;
    int16_t temp;
    int16_t hum;
};

int hdc2080_get_temp_humidity(struct SensorData* data);