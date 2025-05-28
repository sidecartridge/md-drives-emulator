#include "folder_listing_tests.h"

#include "test_runner.h"

static DTA dta;

void test_directory_listing_wildcards() {
  Fsetdta(&dta);

  Dcreate("LISTTEST");
  int fd = Fcreate("LISTTEST\\FILE1.TXT", 0);
  Fclose(fd);
  fd = Fcreate("LISTTEST\\FILE2.TXT", 0);
  Fclose(fd);
  fd = Fcreate("LISTTEST\\NOTE.DOC", 0);
  Fclose(fd);

  int result = Fsfirst("LISTTEST\\*.TXT", 0);
  int count = 0;

  while (result == 0) {
    count++;
    result = Fsnext();
  }

  assert_result("List only *.TXT files in LISTTEST", count, 2);

  // Cleanup
  Fdelete("LISTTEST\\FILE1.TXT");
  Fdelete("LISTTEST\\FILE2.TXT");
  Fdelete("LISTTEST\\NOTE.DOC");
  Ddelete("LISTTEST");
}

void test_directory_listing_all_files() {
  Fsetdta(&dta);

  Dcreate("ALLFILES");
  int fd1 = Fcreate("ALLFILES\\A.TXT", 0);
  int fd2 = Fcreate("ALLFILES\\B.TXT", 0);
  int fd3 = Fcreate("ALLFILES\\C.TXT", 0);
  Fclose(fd1);
  Fclose(fd2);
  Fclose(fd3);

  int result = Fsfirst("ALLFILES\\*.*", 0);
  int count = 0;

  while (result == 0) {
    count++;
    result = Fsnext();
  }

  assert_result("List all files in ALLFILES", count, 3);

  // Cleanup
  Fdelete("ALLFILES\\A.TXT");
  Fdelete("ALLFILES\\B.TXT");
  Fdelete("ALLFILES\\C.TXT");
  Ddelete("ALLFILES");
}

void test_directory_listing_empty_folder() {
  Fsetdta(&dta);

  Dcreate("EMPTYDIR");
  int result = Fsfirst("EMPTYDIR\\*.*", 0);

  assert_result("List files in empty folder EMPTYDIR", result, -33);  // EFILNF
  Ddelete("EMPTYDIR");
}

void test_directory_listing_by_extension() {
  Fsetdta(&dta);
  Dcreate("EXTTEST");
  int fd1 = Fcreate("EXTTEST\\A.PRG", 0);
  int fd2 = Fcreate("EXTTEST\\B.TXT", 0);
  int fd3 = Fcreate("EXTTEST\\C.PRG", 0);
  Fclose(fd1);
  Fclose(fd2);
  Fclose(fd3);

  int result = Fsfirst("EXTTEST\\*.PRG", 0);
  int count = 0;
  while (result == 0) {
    count++;
    result = Fsnext();
  }
  assert_result("List *.PRG files in EXTTEST", count, 2);

  Fdelete("EXTTEST\\A.PRG");
  Fdelete("EXTTEST\\B.TXT");
  Fdelete("EXTTEST\\C.PRG");
  Ddelete("EXTTEST");
}

void test_listing_with_attributes() {
  Fsetdta(&dta);
  Dcreate("ATTRTEST");
  int fd1 = Fcreate("ATTRTEST\\VISIBLE.TXT", 0);
  int fd2 = Fcreate("ATTRTEST\\HIDDEN.TXT", 0);
  Fclose(fd1);
  Fclose(fd2);
  Fattrib("ATTRTEST\\HIDDEN.TXT", 1, 1);  // Set hidden

  int result = Fsfirst("ATTRTEST\\*.*", 0x3);  // Should only list visible
  int count = 0;
  while (result == 0) {
    count++;
    result = Fsnext();
  }
  assert_result("List visible files in ATTRTEST", count, 1);

  result = Fsfirst("ATTRTEST\\*.*", 0x02);  // List hidden
  count = 0;
  while (result == 0) {
    count++;
    result = Fsnext();
  }
  assert_result("List hidden files in ATTRTEST", count, 1);

  Fdelete("ATTRTEST\\VISIBLE.TXT");
  Fdelete("ATTRTEST\\HIDDEN.TXT");
  Ddelete("ATTRTEST");
}

int run_folder_listing_tests(int presskey) {
  print("=== GEMDOS Folder Listing Test Suite ===\n\r");
  test_directory_listing_wildcards();
  if (presskey) press_key("");
  test_directory_listing_all_files();
  if (presskey) press_key("");
  test_directory_listing_empty_folder();
  if (presskey) press_key("");
  test_directory_listing_by_extension();
  if (presskey) press_key("");
  test_listing_with_attributes();
  if (presskey) press_key("");
  print("=== End of GEMDOS Folder Listing Test Suite ===\n\r");
}
