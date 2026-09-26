#ifndef RTC_TESTS_H
#define RTC_TESTS_H

/* The clock as each TOS keeps it: XBIOS Gettime and Settime, the IKBD's
   clock bytes, GEMDOS's Tsetdate. Run by RTCTEST, in supervisor mode. */
int run_rtc_tests(void);

/* "Gettime at the start from <mode>": the clock before the probe changes it.
   Called from supervisor mode first, then from user mode. */
void rtc_report_start(const char* mode);

/* A program's end, around a Pexec made in user mode: each in supervisor mode.
   The second puts the clock back as it was before run_rtc_tests. */
long rtc_before_program_end(void);
long rtc_after_program_end(void);

#endif  // RTC_TESTS_H
