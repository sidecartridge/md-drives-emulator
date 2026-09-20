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

const char* fstests_program(void) {
  static const char* names[] = {"FSTESTS.TTP", "FSTESTS.TOS",
                                "\\AUTO\\FSTESTS.PRG"};
  static const char* found = NULL;
  if (found == NULL) {
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
      int handle = Fopen(names[i], 0);
      if (handle >= 0) {
        Fclose(handle);
        found = names[i];
        break;
      }
    }
    if (found == NULL) found = names[0];
  }
  return found;
}

/* Where this program was started from, and the drive it was loaded from, taken
   once from the suites: reading the system variables needs supervisor mode. */
static int from_auto = 0;
static int boot_drive = 2; /* C: */

/* A basepage begins with p_lowtpa, which points at the basepage itself, so a
   pointer into the TPA that describes itself is a process and anything else is
   not. */
static int is_basepage(long address) {
  long membot = *(const long*)0x432L;
  long phystop = *(const long*)0x42EL;
  if ((address & 1L) || (address < membot) || (address >= phystop)) return 0;
  return *(const long*)address == address;
}

void note_if_running_from_auto(void) {
  extern BASEPAGE* _base;
  long parent = ((const long*)_base)[9]; /* p_parent, basepage offset $24 */

  /* Two live processes above this one mean a shell started it: the desktop,
     and the process TOS boots with. From the AUTO folder only the boot process
     is there, and EmuTOS keeps even that one below the TPA. */
  long above = is_basepage(parent) ? ((const long*)parent)[9] : 0;
  from_auto = !is_basepage(above);
  boot_drive = *(const short*)0x446L; /* _bootdev */
}

int running_from_auto(void) { return from_auto; }

int booted_from_drive(void) { return boot_drive; }

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
