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

void press_key(char* message);

#endif
