#include <stdio.h>
#include <sys/types.h>

#include "chksum_tests.h"
#include "files_tests.h"
#include "folder_listing_tests.h"
#include "folder_tests.h"
#include "test_runner.h"
#include "workdir_tests.h"
//================================================================
// Main program
static int run() {
#ifdef _LOG
  open_log();
#endif

  print("Atari ST GEMDRIVE Test Suite\r\n");
  print("Running tests...\r\n");

  run_files_tests(FALSE);
  run_folder_tests(FALSE);
  run_folder_listing_tests(FALSE);
  run_workdir_tests(FALSE);
  run_chksum_tests(FALSE);

#ifdef _LOG
  close_log();
#endif

  press_key("All tests completed.\r\n");
}

//================================================================
// Standard C entry point
int main(int argc, char *argv[]) {
  // switching to supervisor mode and execute run()
  // needed because of direct memory access for reading/writing the palette
  Supexec(&run);

  Pterm(0);
}
