#include <stdio.h>
#include <string.h>

#include "floppy_tests.h"
#include "test_runner.h"

// FLOPTEST: the floppy drive, and nothing else, against the disk made by
// tools/dev/make_floppy_image.py. A program of its own rather than a suite in
// FSTESTS, so a floppy run and a hard disk run are chosen by which program is
// put in the AUTO folder, and neither needs the other's setup to pass.
static int run(void) {
  set_log_name("FLOPTEST.TXT");
  note_if_running_from_auto();
  if (running_from_auto()) {
    /* TOS runs the AUTO folder with A: as the current drive: the log belongs
       on the drive this program came from, not on the disk under test. */
    Dsetdrv(booted_from_drive());
    Dsetpath("\\");
  }

#ifdef _LOG
  open_log();
#endif

  print("Atari ST floppy test suite\r\n");
  print("Started from %s, boot drive %c:\r\n",
        running_from_auto() ? "the AUTO folder" : "the desktop",
        'A' + booted_from_drive());
  print_tos_version();
  /* The physical drives TOS counted at boot (its hdv_boot recounts them after
     the cartridge has started): with one, TOS itself serves B: from it. */
  print("_nflops %d, _drvbits %08lx\r\n", *(const short*)0x4A6L,
        *(const long*)0x4C2L);

  BorrowedState borrowed;
  state_save(&borrowed);
  run_floppy_tests();
  state_restore(&borrowed, "the floppy tests");

#ifdef _LOG
  close_log();
#endif
  return 0;
}

int main(void) {
  Supexec(&run);
  end_of_run();
  Pterm(0);
  return 0;
}
