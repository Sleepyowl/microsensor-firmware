#pragma once
#include <stdbool.h>
#include <stdint.h>

struct device;

struct __attribute__((packed)) cts_current_time {
    uint16_t year;         /* little-endian */
    uint8_t  month;        /* 1–12 */
    uint8_t  day;          /* 1–31 */
    uint8_t  hours;        /* 0–23 */
    uint8_t  minutes;      /* 0–59 */
    uint8_t  seconds;      /* 0–59 */
    uint8_t  day_of_week;  /* 1 = Monday … 7 = Sunday */
    uint8_t  fractions256; /* 1/256th of a second */
    uint8_t  adjust_reason;/* bitfield */
};

int intinitialize_rtc(bool setTime);
int get_rtc_unix_time(uint32_t* unix_timestamp);
int set_rtc_unix_time(uint32_t unix_timestamp);
int print_rtc_time(void);
int get_rtc_cts_time(struct cts_current_time* unix_timestamp);
int set_rtc_cts_time(const struct cts_current_time* unix_timestamp);
int enable_rtc_pit(uint16_t period_seconds);
int get_rtc_pit_timer_status(uint16_t *timer_status);

