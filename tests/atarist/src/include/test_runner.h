#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>
#include <string.h>

#define NEWLINE "\r\n"
#define FALSE 0
#define TRUE 1

void print(const char* fmt, ...);

/* The log goes to LOG.TXT on the current drive unless a harness names another
   file before it prints anything. */
void set_log_name(const char* name);

void open_log(void);

void close_log(void);

void assert_result(const char* test, int result, int expected);

/* A handle from the drive under test. Which numbers it hands out is its own
   business: GEMDRIVE starts at 16384, Hatari's GEMDOS drive at 64, TOS at 6.
   A test cares that the handle is usable, not what it is called. */
#define A_VALID_HANDLE(h) ((h) > 0)

/* Where this program is, so the tests that start a second copy can find it:
   FSTESTS.TTP or FSTESTS.TOS when it was started by hand, \AUTO\FSTESTS.PRG
   when it runs from the AUTO folder. */
const char* fstests_program(void);

/* Look at where this program was started from, once, in supervisor mode: the
   two answers below come from system variables. */
void note_if_running_from_auto(void);

/* Whether this program was started by TOS from the AUTO folder. */
int running_from_auto(void);

// The drive TOS booted from (_bootdev), which is the drive this program was
// loaded from when it runs from the AUTO folder.
int booted_from_drive(void);

/* What a test may borrow from the machine and forget to give back. The runner
   takes a copy before each suite and puts it back after, so a test cannot make
   the tests that follow it fail: one that left the DTA pointing at a local of
   its own took a whole run down with two bombs. */
typedef struct {
  void* dta;
  int drive;
  char path[66]; /* GEMDOS path buffer: 64 plus the drive and the NUL */
} BorrowedState;

void state_save(BorrowedState* state);
void state_restore(const BorrowedState* state, const char* who);

/* One line naming the TOS and GEMDOS the run is on. Supervisor mode. */
void print_tos_version(void);

/* How every harness ends: "All tests completed.", and then, when it runs from
   the AUTO folder, the device restarts and the computer reboots with it;
   otherwise it waits for a key. */
void end_of_run(void);

void press_key(char* message);

#endif
