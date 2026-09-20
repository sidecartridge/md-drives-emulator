#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>
#include <string.h>

#define NEWLINE "\r\n"
#define FALSE 0
#define TRUE 1

void print(const char* fmt, ...);

void open_log(void);

void close_log(void);

void assert_result(const char* test, int result, int expected);

/* A handle from the drive under test. Which numbers it hands out is its own
   business: GEMDRIVE starts at 16384, Hatari's GEMDOS drive at 64, TOS at 6.
   A test cares that the handle is usable, not what it is called. */
#define A_VALID_HANDLE(h) ((h) > 0)

void press_key(char* message);

#endif
