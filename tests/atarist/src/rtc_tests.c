#include "rtc_tests.h"

#include <osbind.h>

#include "test_runner.h"

/* What each TOS does with the date in XBIOS Gettime and Settime: which clock
   it uses, how it writes the year into the keyboard processor's clock (the
   IKBD's, six BCD bytes: year, month, day, hour, minute, second), what it
   makes of the year bytes our RTC emulation sends, and whether the IKBD keeps
   and advances them. Runs in supervisor mode: the 200 Hz counter and the
   IKBD's clock vector are system memory. */

#define HZ200 (*(volatile long*)0x4BAL)
#define IKBD_SET_CLOCK 0x1B
#define IKBD_READ_CLOCK 0x1C
#define CLOCKVEC 5 /* in the table Kbdvbase answers */

/* The six bytes of the IKBD's answer to IKBD_READ_CLOCK, caught through the
   clock vector: TOS calls it with a0 at the bytes. */
unsigned char rtc_clock_bytes[6];
volatile char rtc_clock_got;
void rtc_clock_handler(void);
__asm__(
    ".globl _rtc_clock_handler\n"
    "_rtc_clock_handler:\n"
    "  move.l a1,-(sp)\n"
    "  lea _rtc_clock_bytes,a1\n"
    "  move.b (a0)+,(a1)+\n"
    "  move.b (a0)+,(a1)+\n"
    "  move.b (a0)+,(a1)+\n"
    "  move.b (a0)+,(a1)+\n"
    "  move.b (a0)+,(a1)+\n"
    "  move.b (a0)+,(a1)+\n"
    "  move.l (sp)+,a1\n"
    "  st _rtc_clock_got\n"
    "  rts\n");

static void wait_ticks(long ticks) {
  long until = HZ200 + ticks;
  while (HZ200 < until) {
  }
}

static int read_ikbd_clock(unsigned char out[6]) {
  long* vectors = (long*)Kbdvbase();
  long old = vectors[CLOCKVEC];
  char command = IKBD_READ_CLOCK;
  long until;
  rtc_clock_got = 0;
  vectors[CLOCKVEC] = (long)rtc_clock_handler;
  Ikbdws(0, &command);
  until = HZ200 + 100; /* half a second */
  while (!rtc_clock_got && HZ200 < until) {
  }
  vectors[CLOCKVEC] = old;
  if (!rtc_clock_got) return -1;
  for (int i = 0; i < 6; i++) out[i] = rtc_clock_bytes[i];
  return 0;
}

static void write_ikbd_clock(const unsigned char in[6]) {
  unsigned char command[7];
  command[0] = IKBD_SET_CLOCK;
  for (int i = 0; i < 6; i++) command[i + 1] = in[i];
  Ikbdws(6, command);
  wait_ticks(10); /* let the IKBD take it before anything asks */
}

static unsigned long dos_time(int year, int month, int day, int hour, int min,
                              int sec) {
  return ((unsigned long)(year - 1980) << 25) | ((unsigned long)month << 21) |
         ((unsigned long)day << 16) | ((unsigned long)hour << 11) |
         ((unsigned long)min << 5) | (unsigned long)(sec / 2);
}

static int dos_year(unsigned long t) { return 1980 + (int)(t >> 25); }

static void format_dos(unsigned long t, char* out) {
  sprintf(out, "%04d-%02d-%02d %02d:%02d:%02d", dos_year(t),
          (int)(t >> 21) & 15, (int)(t >> 16) & 31, (int)(t >> 11) & 31,
          (int)(t >> 5) & 63, (int)(t & 31) * 2);
}

static void format_bytes(const unsigned char b[6], char* out) {
  sprintf(out, "%02x %02x %02x %02x %02x %02x", b[0], b[1], b[2], b[3], b[4],
          b[5]);
}

/* A line saying what the IKBD holds, what Gettime makes of it, and GEMDOS's
   own date and time. */
static unsigned long report(const char* what) {
  unsigned char raw[6];
  char bytes[24] = "no answer", date[24], dos[24];
  unsigned long t;
  if (read_ikbd_clock(raw) == 0) format_bytes(raw, bytes);
  t = (unsigned long)Gettime();
  format_dos(t, date);
  format_dos(((unsigned long)(unsigned short)Tgetdate() << 16) |
                 (unsigned short)Tgettime(),
             dos);
  print("%s: IKBD %s, Gettime %s ($%08lx), Tgetdate %s\r\n", what, bytes, date,
        t, dos);
  return t;
}

static int ikbd_is_the_clock = 0;

/* Which clock TOS reads: a date only the IKBD holds, and whether Gettime
   follows it. A Mega ST, a Mega STE, a TT and a Falcon have a chip of their
   own, which TOS may read instead. */
static void test_which_clock(void) {
  static const unsigned char y1987[6] = {0x87, 0x01, 0x02, 0x03, 0x04, 0x06};
  unsigned long t;
  write_ikbd_clock(y1987);
  t = report("The IKBD set to 1987-01-02 03:04:06");
  ikbd_is_the_clock = (dos_year(t) == 1987);
  print("Gettime reads %s\r\n",
        ikbd_is_the_clock ? "the IKBD's clock" : "another clock");
}

/* Settime, then Gettime: does the year come back, and what did TOS write into
   the IKBD for it. */
static void test_round_trips(void) {
  static const int years[] = {1999, 2000, 2026, 2030, 2047,
                              2048, 2079, 2080, 2099};
  char name[64];
  for (unsigned i = 0; i < sizeof(years) / sizeof(years[0]); i++) {
    unsigned long t;
    Settime(dos_time(years[i], 6, 15, 12, 30, 0));
    wait_ticks(10);
    sprintf(name, "Settime %d-06-15 12:30:00", years[i]);
    t = report(name);
    sprintf(name, "Settime then Gettime keeps %d", years[i]);
    assert_result(name, dos_year(t), years[i]);
  }
}

/* The year bytes an RTC emulation may send, and what TOS decodes from each. */
static void test_year_bytes(void) {
  static const struct {
    unsigned char year;
    const char* what;
  } bytes[] = {
      {0x26, "26: plain BCD"},
      {0x96, "96: 26 + 70, our Y2K patch's"},
      {0xC6, "C6: BCD 46 + $80, TOS's own for 2026"},
  };
  char name[80];
  for (unsigned i = 0; i < sizeof(bytes) / sizeof(bytes[0]); i++) {
    unsigned char set[6] = {0, 0x09, 0x25, 0x10, 0x20, 0x30};
    unsigned char got[6] = {0};
    set[0] = bytes[i].year;
    write_ikbd_clock(set);
    sprintf(name, "The IKBD's year byte set to %s", bytes[i].what);
    report(name);
    read_ikbd_clock(got);
    sprintf(name, "The IKBD keeps a year byte of $%02x", bytes[i].year);
    assert_result(name, got[0], bytes[i].year);
  }
}

/* From TOS 1.02 on, GEMDOS's Tsetdate and Tsettime also tell the XBIOS
   (xbsettime); TOS 1.00's set GEMDOS's date alone. What each answers, and what
   lands in the clock. */
static void test_gemdos_sets_the_clock(void) {
  static const unsigned char y1999[6] = {0x99, 0x01, 0x01, 0x00, 0x00, 0x00};
  unsigned long t;
  long answer;
  write_ikbd_clock(y1999);
  print("The IKBD set to 1999-01-01\r\n");
  answer = Tsetdate((unsigned short)(dos_time(2026, 9, 25, 0, 0, 0) >> 16));
  print("Tsetdate answered %ld\r\n", answer);
  answer =
      Tsettime((unsigned short)(dos_time(2026, 9, 25, 10, 20, 30) & 0xFFFF));
  print("Tsettime answered %ld\r\n", answer);
  wait_ticks(10);
  t = report("Tsetdate 2026-09-25, Tsettime 10:20:30, after the IKBD at 1999");
  assert_result("Tsetdate reaches Gettime", dos_year(t), 2026);
}

/* The clock Gettime reads ticks: across the end of a year, and the end of a
   February, from a date TOS's Settime wrote. */
static void tick_from(int year, int month, int day, int hour, int min, int sec,
                      const char* what, int want_year, int want_month,
                      int want_day) {
  unsigned long t;
  char name[96];
  Settime(dos_time(year, month, day, hour, min, sec));
  wait_ticks(10);
  report(what);
  wait_ticks(3 * 200);
  sprintf(name, "%s, 3 s later", what);
  t = report(name);
  sprintf(name, "%s: the clock reaches %d-%02d-%02d", what, want_year,
          want_month, want_day);
  assert_result(name,
                dos_year(t) == want_year &&
                    (int)((t >> 21) & 15) == want_month &&
                    (int)((t >> 16) & 31) == want_day,
                TRUE);
}

static void test_ticks(void) {
  tick_from(1999, 12, 31, 23, 59, 58, "Settime 1999-12-31 23:59:58", 2000, 1,
            1);
  tick_from(2026, 12, 31, 23, 59, 58, "Settime 2026-12-31 23:59:58", 2027, 1,
            1);
  tick_from(2028, 2, 28, 23, 59, 58, "Settime 2028-02-28 23:59:58", 2028, 2,
            29);
}

/* When a program ends, GEMDOS may take its date back from the XBIOS (Pterm
   calls xbgettime): a year Gettime gets wrong then becomes the date of every
   file saved after it. The IKBD is left at $26, which an IKBD TOS decodes as
   2054, GEMDOS is told 2026, and a program ends. */
long rtc_before_program_end(void) {
  static const unsigned char y26[6] = {0x26, 0x09, 0x25, 0x10, 0x20, 0x30};
  write_ikbd_clock(y26);
  Tsetdate((unsigned short)(dos_time(2026, 9, 25, 0, 0, 0) >> 16));
  Tsettime((unsigned short)(dos_time(2026, 9, 25, 10, 20, 30) & 0xFFFF));
  wait_ticks(10);
  report("Before a program ends");
  return 0;
}

static unsigned long start;
static int start_taken = 0;

/* What the clock says before the probe touches it: with the RTC on, the date
   the cartridge's module left, from the mode given. The first call keeps it
   for the end. */
void rtc_report_start(const char* mode) {
  unsigned long t = (unsigned long)Gettime();
  char date[24], dos[24];
  if (!start_taken) {
    start = t;
    start_taken = 1;
  }
  format_dos(t, date);
  format_dos(((unsigned long)(unsigned short)Tgetdate() << 16) |
                 (unsigned short)Tgettime(),
             dos);
  print("Gettime at the start from %s: %s ($%08lx), Tgetdate %s\r\n", mode,
        date, t, dos);
}

long rtc_after_program_end(void) {
  report("After a program ended");
  assert_result("GEMDOS keeps its date when a program ends",
                1980 + ((unsigned short)Tgetdate() >> 9), 2026);
  Settime(start); /* put the clock back, near enough */
  return 0;
}

int run_rtc_tests(void) {
  print("=== RTC: which clock ===\r\n");
  test_which_clock();
  print("=== RTC: Settime and Gettime ===\r\n");
  test_round_trips();
  print("=== RTC: year bytes in the IKBD ===\r\n");
  test_year_bytes();
  print("=== RTC: GEMDOS ===\r\n");
  test_gemdos_sets_the_clock();
  print("=== RTC: the clock ticking ===\r\n");
  test_ticks();
  return 0;
}
