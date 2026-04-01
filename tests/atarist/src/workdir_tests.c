#include "workdir_tests.h"

#include "test_runner.h"

static void cleanup_relative_file_ops_folder(void) {
  Dsetpath("\\");
  Fdelete("RELPATH\\RELFILE.TXT");
  Fdelete("RELPATH\\RELNEW.TXT");
  Ddelete("RELPATH");
}

void test_change_directory_and_getpath() {
  Dcreate("CHDIR");
  int set_result = Dsetpath("CHDIR");
  assert_result("Change directory to CHDIR", set_result, 0);

  char buffer[64] = {0};
  int get_result = Dgetpath(buffer, 0);
  assert_result("Get current directory on drive 0", get_result, 0);
  assert_result("Verify current directory is CHDIR", strcmp(buffer, "\\CHDIR"),
                0);

  Dsetpath("..\\");  // Return to parent
  Ddelete("CHDIR");
}

void test_change_to_nonexistent_directory() {
  int result = Dsetpath("NOEXIST");
  assert_result("Attempt to change to nonexistent directory", result,
                -34);  // EACCDN
}

void test_return_to_parent_directory() {
  Dsetpath("\\");  // Start the test from root
  Dcreate("LEVEL1");
  Dsetpath("LEVEL1");
  Dcreate("LEVEL2");
  Dsetpath("LEVEL2");

  int result = Dsetpath("..\\");
  assert_result("Return to LEVEL1 using ..", result, 0);

  result = Dsetpath("..\\");
  assert_result("Return to root from LEVEL1", result, 0);

  Dsetpath(".\\");  // Just in case
  Ddelete("LEVEL1\\LEVEL2");
  Ddelete("LEVEL1");
}

void test_query_free_space_on_drive_C() {
  Dfree free_info;
  int result = Dfree(&free_info, 3);  // Drive C
  assert_result("Query free space on default drive", result, 0);
  assert_result("Verify total clusters > 0", (free_info.b_total != 0), TRUE);
  assert_result("Verify free clusters > 0", (free_info.b_free > 0), TRUE);
}

void test_query_free_space_on_default_drive() {
  Dfree free_info;
  int result = Dfree(&free_info, 0);  // Default drive
  assert_result("Query free space on default drive", result, 0);
  assert_result("Verify total clusters > 0", (free_info.b_total != 0), TRUE);
  assert_result("Verify free clusters > 0", (free_info.b_free > 0), TRUE);
}

void test_get_and_set_drive() {
  int current_drive = Dgetdrv();
  assert_result("Get current drive",
                (current_drive >= 0 && current_drive <= 15), 1);

  int result = Dsetdrv(current_drive);
  assert_result("Set drive to current drive again (should succeed)",
                (result & (1 << current_drive)) ? TRUE : FALSE, TRUE);
}

void test_relative_file_operations_in_current_directory() {
  char buffer[16] = {0};
  int handle = 0;
  long read = 0;

  cleanup_relative_file_ops_folder();

  assert_result("Create folder RELPATH", Dcreate("RELPATH"), 0);
  assert_result("Change directory to RELPATH", Dsetpath("RELPATH"), 0);

  handle = Fcreate("RELFILE.TXT", 0);
  assert_result("Create RELFILE.TXT using relative path", handle >= 0, TRUE);
  if (handle >= 0) {
    assert_result("Write RELFILE.TXT using relative path",
                  Fwrite(handle, 5, "HELLO"), 5);
    assert_result("Close RELFILE.TXT after create", Fclose(handle), 0);
  }

  handle = Fopen("RELFILE.TXT", 0);
  assert_result("Open RELFILE.TXT using relative path", handle >= 0, TRUE);
  if (handle >= 0) {
    read = Fread(handle, 5, buffer);
    assert_result("Read RELFILE.TXT using relative path", read, 5);
    assert_result("RELFILE.TXT content matches", strcmp(buffer, "HELLO"), 0);
    assert_result("Close RELFILE.TXT after read", Fclose(handle), 0);
  }

  assert_result("Rename RELFILE.TXT to RELNEW.TXT using relative paths",
                Frename(0, "RELFILE.TXT", "RELNEW.TXT"), 0);

  handle = Fopen("RELFILE.TXT", 0);
  assert_result("Old relative filename no longer opens", handle, -33);

  memset(buffer, 0, sizeof(buffer));
  handle = Fopen("RELNEW.TXT", 0);
  assert_result("Open RELNEW.TXT using relative path", handle >= 0, TRUE);
  if (handle >= 0) {
    read = Fread(handle, 5, buffer);
    assert_result("Read RELNEW.TXT using relative path", read, 5);
    assert_result("RELNEW.TXT content matches", strcmp(buffer, "HELLO"), 0);
    assert_result("Close RELNEW.TXT after read", Fclose(handle), 0);
  }

  assert_result("Delete RELNEW.TXT using relative path", Fdelete("RELNEW.TXT"),
                0);
  assert_result("Return to root after relative file operations", Dsetpath("..\\"),
                0);
  assert_result("Delete folder RELPATH after relative file operations",
                Ddelete("RELPATH"), 0);

  cleanup_relative_file_ops_folder();
}

int run_workdir_tests(int presskey) {
  print("=== GEMDOS Workdir Test Suite ===\n\r");

  test_change_directory_and_getpath();
  if (presskey) press_key("");
  test_change_to_nonexistent_directory();
  if (presskey) press_key("");
  test_return_to_parent_directory();
  if (presskey) press_key("");
  test_query_free_space_on_drive_C();
  if (presskey) press_key("");
  test_query_free_space_on_default_drive();
  if (presskey) press_key("");
  test_get_and_set_drive();
  if (presskey) press_key("");
  test_relative_file_operations_in_current_directory();
  if (presskey) press_key("");

  print("=== All Workdir tests completed ===\n\r");
  return 0;
}
