#include "folder_tests.h"

#include "test_runner.h"

void test_create_folder() {
  int result = Dcreate("TESTDIR");
  assert_result("Create folder 'TESTDIR'", result, 0);
}

void test_rename_folder() {
  Ddelete("RENAMEDR");  // Clean up in case it exists
  int result = Frename(0, "TESTDIR", "RENAMEDR");
  assert_result("Rename 'TESTDIR' to 'RENAMEDR'", result, 0);
}

void test_delete_folder() {
  int result = Ddelete("RENAMEDR");
  assert_result("Delete folder 'RENAMEDR'", result, 0);
}

void test_nested_folders() {
  assert_result("Create nested folders 'A'", Dcreate("A"), 0);
  assert_result("Create nested folders 'A/B'", Dcreate("A\\B"), 0);
  assert_result("Create nested folders 'A/B/C'", Dcreate("A\\B\\C"), 0);
  assert_result("Delete nested folders 'A/B/C'", Ddelete("A\\B\\C"), 0);
  assert_result("Delete nested folders 'A/B'", Ddelete("A\\B"), 0);
  assert_result("Delete nested folders 'A'", Ddelete("A"), 0);
}

void test_existing_folder_creation() {
  Dcreate("DUPDIR");
  int result = Dcreate("DUPDIR");
  assert_result("Create folder 'DUPDIR' twice", result, -36);
  Ddelete("DUPDIR");
}

void test_delete_non_empty_folder() {
  Dcreate("NONEMPTY");
  int fh = Fcreate("NONEMPTY\\file.txt", 0);
  assert_result("Create folder 'NONEMPTY' and file inside it",
                (fh >= 0 && fh <= 32767), TRUE);
  Fclose(fh);
  int result = Ddelete("NONEMPTY");
  assert_result("Cannot delete folder 'NONEMPTY' with file inside", result,
                -36);  // ENOTEMPTY
  Fdelete("NONEMPTY\\file.txt");
  Ddelete("NONEMPTY");
}

void test_rename_to_existing_folder() {
  Dcreate("FOO");
  Dcreate("BAR");
  int result = Frename(0, "FOO", "BAR");
  assert_result("Rename folder to existing folder name should fail", result,
                -36);
  Ddelete("FOO");
  Ddelete("BAR");
}

void test_invalid_path() {
  int result = Dcreate("X\\Y");
  assert_result("Create folder with non-existing intermediate path", result,
                -34);  // ENOENT or EACCDN
}

void test_relative_paths() {
  int result = Dcreate(".\\RELTEST");
  assert_result("Create folder using relative path '.\\RELTEST'", result, 0);
  result = Ddelete(".\\RELTEST");
  assert_result("Delete folder using relative path '.\\RELTEST'", result, 0);
}

int run_folder_tests(int presskey) {
  print("=== GEMDOS Folder Test Suite ===\n\r");

  test_create_folder();
  if (presskey) press_key("");
  test_rename_folder();
  if (presskey) press_key("");
  test_delete_folder();
  if (presskey) press_key("");
  test_nested_folders();
  if (presskey) press_key("");
  test_existing_folder_creation();
  if (presskey) press_key("");
  test_delete_non_empty_folder();
  if (presskey) press_key("");
  test_rename_to_existing_folder();
  if (presskey) press_key("");
  test_invalid_path();
  if (presskey) press_key("");
  test_relative_paths();
  if (presskey) press_key("");

  print("=== All folder tests completed ===\n\r");
  return 0;
}
