#include "test_runner.h"

static FILE* log_fp = NULL;

void open_log(void) {
  if (!log_fp) {
    log_fp = fopen("LOG.TXT", "a");
  }
}

void close_log(void) {
  if (log_fp) {
    fclose(log_fp);
    log_fp = NULL;
  }
}

void print(const char* fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsprintf(buffer, fmt, args);
  va_end(args);

  // Print to screen
  printf("%s", buffer);

#ifdef _LOG
  // Also log to file
  open_log();
  if (log_fp) {
    fprintf(log_fp, "%s", buffer);
    fflush(log_fp);
  }
#endif
}

void assert_result(const char* test, int result, int expected) {
  if (result == expected) {
    print("[ OK ] %s\r\n", test);
  } else {
    print("[FAIL] %s (R: %d, E: %d)\r\n", test, result, expected);
  }
}

void flush_kbd(void) {
  // while (Bconstat(2) != 0)
  // {
  //     (void)Bconin(2);
  // }
  while (Cconis() != 0) {
    (void)Cnecin();
  }
}

void press_key(char* message) {
  if (message != NULL) {
    Cconws(message);
  }
  flush_kbd();
  (void)Bconin(2);
}
