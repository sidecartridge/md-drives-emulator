#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "chksum_tests.h"
#include "files_tests.h"
#include "folder_listing_tests.h"
#include "folder_tests.h"
#include "test_runner.h"
#include "workdir_tests.h"
// Suites named on the command line (run as FSTESTS.TTP), or all of them.
static int suiteArgc = 0;
static char **suiteArgv = NULL;

static int suite_selected(const char *name) {
  if (suiteArgc < 2) return TRUE;
  for (int i = 1; i < suiteArgc; i++) {
    if (strcasecmp(suiteArgv[i], name) == 0) return TRUE;
  }
  return FALSE;
}

//================================================================
// Main program
static int run() {
#ifdef _LOG
  open_log();
#endif

  print("Atari ST GEMDRIVE Test Suite\r\n");
  print("Running tests...\r\n");

  // Show the current drive
  print("Current drive: %c:\r\n", 'A' + Dgetdrv());
  char path[66];     /* GEMDOS path buffer; 64 + drive & NUL is safe */
  Dgetpath(path, 0); /* fills e.g. "\FOLDER\SUBLEVEL" */

  print("Current path: %s\r\n", path);
  // Standard handles of this process (basepage offset $30). Damaged values
  // here send the console output elsewhere (a device, or a file).
  extern BASEPAGE *_base;
  const signed char *uft = (const signed char *)_base + 0x30;
  print("Standard handles: %d %d %d %d %d %d\r\n\r\n", uft[0], uft[1], uft[2],
        uft[3], uft[4], uft[5]);

  static const struct {
    const char *name;
    void (*fn)(void);
  } singleTests[] = {
      {"wd-chdir", test_change_directory_and_getpath},
      {"wd-noexist", test_change_to_nonexistent_directory},
      {"wd-parent", test_return_to_parent_directory},
      {"wd-dfreec", test_query_free_space_on_drive_C},
      {"wd-dfree", test_query_free_space_on_default_drive},
      {"wd-drive", test_get_and_set_drive},
      {"wd-relative", test_relative_file_operations_in_current_directory},
  };
  for (unsigned i = 0; i < sizeof(singleTests) / sizeof(singleTests[0]);
       i++) {
    if (suiteArgc >= 2 && suite_selected(singleTests[i].name)) {
      print("=== %s ===\r\n", singleTests[i].name);
      singleTests[i].fn();
    }
  }

  if (suite_selected("files")) run_files_tests(FALSE);
  if (suite_selected("folder")) run_folder_tests(FALSE);
  if (suite_selected("listing")) run_folder_listing_tests(FALSE);
  if (suite_selected("workdir")) run_workdir_tests(FALSE);
  if (suite_selected("chksum")) run_chksum_tests(FALSE);

#ifdef _LOG
  close_log();
#endif

  return 0;
}

//================================================================
// Standard C entry point
int main(int argc, char *argv[]) {
  // Child of test_handles_closed_on_pterm(): create a file and end without
  // closing it.
  if (argc >= 2 && strcasecmp(argv[1], "leakchild") == 0) {
    int handle = Fcreate("LEAKCHLD.TMP", 0);
    print("leakchild: Fcreate LEAKCHLD.TMP = %d\r\n", handle);
    Pterm(0);
  }
  // Child of test_fforce_onto_gemdrive_file(): write to the inherited stdout.
  if (argc >= 2 && strcasecmp(argv[1], "forcechild") == 0) {
    Pterm(Fwrite(1, 5, "CHILD") == 5 ? 0 : 1);
  }
  suiteArgc = argc;
  suiteArgv = argv;
  // switching to supervisor mode and execute run()
  // needed because of direct memory access for reading/writing the palette
  Supexec(&run);

  // Starts child programs, so it runs here in user mode, not under Supexec.
  if (suite_selected("files") || suite_selected("pterm"))
    test_handles_closed_on_pterm();
  if (suite_selected("files") || suite_selected("fforce"))
    test_fforce_onto_gemdrive_file();
  if (suite_selected("files") || suite_selected("pexec"))
    test_pexec_from_another_current_drive();

  print("All tests completed.\r\n");
  press_key("Press a key.\r\n");
  Pterm(0);
  return EXIT_SUCCESS;
}
//================================================================
