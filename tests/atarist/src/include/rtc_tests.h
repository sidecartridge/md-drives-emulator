#ifndef RTC_TESTS_H
#define RTC_TESTS_H

/* The clock as each TOS keeps it: XBIOS Gettime and Settime, the IKBD's
   clock bytes, GEMDOS's Tsetdate. Run by RTCTEST, in supervisor mode. */
int run_rtc_tests(void);

/* A program's end, around a Pexec made in user mode: each in supervisor mode.
   The second puts the clock back as it was before run_rtc_tests. */
long rtc_before_program_end(void);
long rtc_after_program_end(void);

#endif  // RTC_TESTS_H
