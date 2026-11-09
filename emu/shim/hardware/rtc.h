/* hardware/rtc.h: the RP2040's real-time clock, which EmuMD has no stand-in
 * for. Kept by emu/standins.c from the host's clock. */
#ifndef _HARDWARE_RTC_H
#define _HARDWARE_RTC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int16_t year;
  int8_t month;
  int8_t day;
  int8_t dotw;
  int8_t hour;
  int8_t min;
  int8_t sec;
} datetime_t;

typedef void (*rtc_callback_t)(void);

void rtc_init(void);
bool rtc_set_datetime(const datetime_t *t);
bool rtc_get_datetime(datetime_t *t);
bool rtc_running(void);

#endif
