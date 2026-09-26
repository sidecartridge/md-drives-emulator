#include <stdio.h>
#include <string.h>

#include "rtc_tests.h"
#include "test_runner.h"

// RTCTEST: what the TOS it runs on does with the date - the clock it reads,
// how it keeps the year there, what it makes of the year bytes an RTC
// emulation sends, and which date GEMDOS keeps when a program ends. It needs no
// drive but the one it starts from.
static int run(void) {
  set_log_name("RTCTEST.TXT");
  note_if_running_from_auto();
  if (running_from_auto()) {
    Dsetdrv(booted_from_drive());
    Dsetpath("\\");
  }

#ifdef _LOG
  open_log();
#endif

  print("Atari ST RTC test suite\r\n");
  print("Started from %s, boot drive %c:\r\n",
        running_from_auto() ? "the AUTO folder" : "the desktop",
        'A' + booted_from_drive());
  print_tos_version();
  rtc_report_start("supervisor mode");
  return 0;
}

static int tests(void) { return run_rtc_tests(); }

// The program itself, as the program-end test starts it.
static const char* rtctest_program(void) {
  static const char* names[] = {"RTCTEST.TOS", "\\AUTO\\RTCTEST.PRG"};
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    int handle = Fopen(names[i], 0);
    if (handle >= 0) {
      Fclose(handle);
      return names[i];
    }
  }
  return names[0];
}

int main(int argc, char* argv[]) {
  // The program the program-end test starts: it only ends.
  if (argc >= 2 && strcasecmp(argv[1], "child") == 0) Pterm(0);

  Supexec(&run);
  // From user mode, where programs call: a hook finds the call elsewhere.
  rtc_report_start("user mode");
  Supexec(&tests);

  // Starts a program, so it runs here in user mode, not under Supexec.
  print("=== RTC: a program's end ===\r\n");
  Supexec(&rtc_before_program_end);
  long rc = Pexec(0, rtctest_program(), "\005child", NULL);
  if (rc != 0) print("Pexec %s returned %ld\r\n", rtctest_program(), rc);
  Supexec(&rtc_after_program_end);

  end_of_run();
  Pterm(0);
  return 0;
}
