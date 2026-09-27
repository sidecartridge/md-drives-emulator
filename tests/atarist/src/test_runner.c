#include "test_runner.h"

#include "sidecart.h"

static FILE* log_fp = NULL;
static const char* log_name = "LOG.TXT";

void set_log_name(const char* name) { log_name = name; }

void open_log(void) {
  if (!log_fp) {
    log_fp = fopen(log_name, "a");
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

void state_save(BorrowedState* state) {
  state->dta = (void*)Fgetdta();
  state->drive = Dgetdrv();
  state->path[0] = '\0';
  Dgetpath(state->path, 0);
}

void state_restore(const BorrowedState* state, const char* who) {
  /* Say who left something behind: the runner is a net, not a licence, and a
     test that needs the net is a test worth fixing. */
  char path[66] = {0};
  Dgetpath(path, 0);
  if ((Dgetdrv() != state->drive) || strcmp(path, state->path) != 0) {
    print("[note] %s left drive %c: path %s, restoring %c: %s\r\n", who,
          'A' + Dgetdrv(), path, 'A' + state->drive, state->path);
  }
  if ((void*)Fgetdta() != state->dta) {
    print("[note] %s left the DTA at %lx, restoring %lx\r\n", who,
          (long)Fgetdta(), (long)state->dta);
  }
  Dsetdrv(state->drive);
  Dsetpath(state->path[0] ? state->path : "\\");
  Fsetdta(state->dta);
}

static void print_machine(void);

void print_tos_version(void) {
  /* Which TOS ran this, so a log says on its own where it comes from. The
     version word is at ROM+2, and the ROM is at $FC0000 on a 192 KB machine or
     at $E00000 on a 256 KB one. The reset vector at $4 points into it, so its
     high word says which: reading the byte instead sees the $00 above the
     address and sends a 192 KB machine to $E00000, which is not mapped there.
     This is the same test get_tos_version makes in the cartridge firmware. */
  const unsigned short* rom = (*(const unsigned short*)0x4L == 0x00FC)
                                  ? (const unsigned short*)0xFC0002L
                                  : (const unsigned short*)0xE00002L;
  print("TOS %x.%02x, GEMDOS %x\r\n", *rom >> 8, *rom & 0xFF,
        (int)Sversion());
  print_machine();
}

/* The Mega STE's speed and cache register: bit 0 the cache, bit 1 16 MHz.
   The hardware keeps the cache off at 8 MHz. It exists on no other machine,
   where reading it is a bus error, so the machine cookie decides first. */
#define MEGA_STE_CONTROL ((volatile unsigned char*)0xFFFF8E21L)
#define MCH_MEGA_STE 0x00010010L
static int mega_ste = 0;

static long machine_cookie(void) {
  const long* jar = *(const long**)0x5A0L; /* _p_cookies */
  if (jar == NULL) return 0;
  for (; jar[0] != 0; jar += 2) {
    if (jar[0] == 0x5F4D4348L) return jar[1]; /* '_MCH' */
  }
  return 0;
}

static void print_mega_ste(const char* when) {
  unsigned char control = *MEGA_STE_CONTROL;
  print("Mega STE %s: %s MHz, cache %s ($%02x)\r\n", when,
        (control & 2) ? "16" : "8", (control & 1) ? "on" : "off", control);
}

/* The machine, from the _MCH cookie: 0 without a cookie jar, as TOS 1.04 and
   earlier have none. */
static void print_machine(void) {
  long mch = machine_cookie();
  const char* name = mch == 0L            ? "ST"
                     : mch == 0x00010000L ? "STE"
                     : mch == 0x00010010L ? "Mega STE"
                     : mch == 0x00020000L ? "TT"
                     : mch == 0x00030000L ? "Falcon"
                                          : "other";
  print("Machine: %s (_MCH $%08lx)\r\n", name, mch);
}

void mega_ste_setup(void) {
  char asked[8] = {0};
  int handle;

  mega_ste = (machine_cookie() == MCH_MEGA_STE);
  if (!mega_ste) return;
  /* A run asks for its setting in a file, so one harness binary measures
     every setting: tools/dev/hardware_tests.py --mste writes it. */
  handle = Fopen("\\MSTE.INF", 0);
  if (handle >= 0) {
    Fread(handle, sizeof(asked) - 1, asked);
    Fclose(handle);
    if (strncmp(asked, "16C", 3) == 0) {
      *MEGA_STE_CONTROL |= 2; /* the speed first: no cache at 8 MHz */
      *MEGA_STE_CONTROL |= 1;
    } else if (strncmp(asked, "16", 2) == 0) {
      *MEGA_STE_CONTROL &= ~1;
      *MEGA_STE_CONTROL |= 2;
    } else if (asked[0] == '8') {
      *MEGA_STE_CONTROL &= ~3;
    } else {
      print("MSTE.INF asks for \"%s\": not 8, 16 or 16C\r\n", asked);
    }
  }
  print_mega_ste("at the start");
}

void print_clock(const char* mode) {
  unsigned long clock = Gettime(); /* the DOS layout, from 1980 */
  print("Gettime from %s: %04d-%02d-%02d %02d:%02d:%02d\r\n", mode,
        1980 + (int)(clock >> 25),
        (int)(clock >> 21) & 15, (int)(clock >> 16) & 31,
        (int)(clock >> 11) & 31, (int)(clock >> 5) & 63, (int)(clock & 31) * 2);
}

static long print_mega_ste_at_end(void) {
  print_mega_ste("at the end");
  return 0;
}

// Ask the device to restart and take the computer with it. The device comes
// back in the setup menu, where the card is on USB; the computer has to reboot
// too, because the cartridge it booted from goes away for a moment and comes
// back in another mode. Runs in supervisor mode: reading the 200 Hz counter
// and reaching the reset vector both need it, and nothing here calls the OS,
// whose GEMDOS trap goes through a cartridge that is restarting.
static int restart_device_and_reboot(void) {
  const volatile long* hz200 = (const volatile long*)0x4BAL;
  long deadline;

  /* A Mega STE's cache answers the cartridge's reads from copies it made
     before, which breaks a command: off, and not put back, since the computer
     reboots. */
  if (mega_ste) *MEGA_STE_CONTROL &= ~1;
  if (sidecart_restart_device() != 0) return -1;

  deadline = *hz200 + (6L * 200L); /* the device needs about two seconds */
  while (*hz200 < deadline) {
  }
  __asm__ volatile("move.w #0x2700,%sr\n\tmove.l 4.w,%a0\n\tjmp (%a0)");
  return 0; /* not reached */
}

void end_of_run(void) {
  /* A driver that changed the setting and did not put it back shows here. */
  if (mega_ste) Supexec(&print_mega_ste_at_end);
  print("All tests completed.\r\n");
  if (running_from_auto()) {
    // Nobody is watching a boot: ask the device to restart, so it comes back
    // in the setup menu with the card on USB and the log can be read from a
    // computer, and reboot with it.
    print("Asking the device to restart, the log is on the card\r\n");
    close_log();
    if (Supexec(&restart_device_and_reboot) != 0)
      press_key("The device did not answer. Press a key.\r\n");
  } else {
    press_key("Press a key.\r\n");
  }
}

static const char* result_prefix = "";

void set_result_prefix(const char* prefix) { result_prefix = prefix; }

void assert_result(const char* test, int result, int expected) {
  if (result == expected) {
    print("[ OK ] %s%s\r\n", result_prefix, test);
  } else {
    print("[FAIL] %s%s (R: %d, E: %d)\r\n", result_prefix, test, result,
          expected);
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
