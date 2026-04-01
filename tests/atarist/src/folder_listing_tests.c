#include "folder_listing_tests.h"

#include "test_runner.h"

static DTA dta;

static void cleanup_attrtest_folder(void) {
  Fattrib("ATTRTEST\\VISIBLE.TXT", 1, 0x00);
  Fattrib("ATTRTEST\\HIDDEN.TXT", 1, 0x00);
  Fdelete("ATTRTEST\\VISIBLE.TXT");
  Fdelete("ATTRTEST\\HIDDEN.TXT");
  Ddelete("ATTRTEST");
}

static void cleanup_multidta_folders(void) {
  Fdelete("DTAONE\\A1.TXT");
  Fdelete("DTAONE\\A2.TXT");
  Fdelete("DTATWO\\B1.TXT");
  Fdelete("DTATWO\\B2.TXT");
  Ddelete("DTAONE");
  Ddelete("DTATWO");
}

static void cleanup_dirattr_folder(void) {
  Fdelete("DIRATTR\\FILE.TXT");
  Ddelete("DIRATTR\\SUBDIR");
  Ddelete("DIRATTR");
}

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

void test_directory_listing_nonexistent_folder() {
  Fsetdta(&dta);
  Dsetpath("\\");
  Ddelete("MISSDIR");

  int result = Fsfirst("MISSDIR\\*.*", 0);
  assert_result("List files in nonexistent folder MISSDIR", result,
                -34);  // EPTHNF
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
  cleanup_attrtest_folder();
  Dcreate("ATTRTEST");
  int fd1 = Fcreate("ATTRTEST\\VISIBLE.TXT", 0);
  int fd2 = Fcreate("ATTRTEST\\HIDDEN.TXT", 0);
  Fclose(fd1);
  Fclose(fd2);
  Fattrib("ATTRTEST\\HIDDEN.TXT", 1, 0x02);  // Set hidden

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

  cleanup_attrtest_folder();
}

void test_fsnext_after_end() {
  Fsetdta(&dta);

  Dcreate("NEXTEND");
  int fd = Fcreate("NEXTEND\\ONLY.TXT", 0);
  Fclose(fd);

  int result = Fsfirst("NEXTEND\\*.*", 0);
  assert_result("Fsfirst finds the first file in NEXTEND", result, 0);

  result = Fsnext();
  assert_result("Fsnext reaches end of NEXTEND listing", result, -49);

  result = Fsnext();
  assert_result("Fsnext after end stays at no-more-files", result, -49);

  Fdelete("NEXTEND\\ONLY.TXT");
  Ddelete("NEXTEND");
}

void test_directory_listing_includes_subdirs() {
  int result = 0;
  int count = 0;
  int found_file = FALSE;
  int found_subdir = FALSE;
  int fd = 0;

  Fsetdta(&dta);
  Dsetpath("\\");
  cleanup_dirattr_folder();

  assert_result("Create folder DIRATTR", Dcreate("DIRATTR"), 0);
  assert_result("Create subdirectory DIRATTR\\SUBDIR", Dcreate("DIRATTR\\SUBDIR"),
                0);

  fd = Fcreate("DIRATTR\\FILE.TXT", 0);
  assert_result("Create DIRATTR\\FILE.TXT", fd >= 0, TRUE);
  if (fd >= 0) Fclose(fd);

  result = Fsfirst("DIRATTR\\*.*", 0x10);
  while (result == 0) {
    count++;
    if (strcmp(dta.d_fname, "FILE.TXT") == 0) {
      found_file = TRUE;
    }
    if (strcmp(dta.d_fname, "SUBDIR") == 0) {
      found_subdir = TRUE;
    }
    result = Fsnext();
  }

  assert_result("Directory-inclusive listing finds file in DIRATTR", found_file,
                TRUE);
  assert_result("Directory-inclusive listing finds subdirectory in DIRATTR",
                found_subdir, TRUE);
  assert_result("Directory-inclusive listing returns two entries in DIRATTR",
                count, 2);

  cleanup_dirattr_folder();
}

void test_multiple_dtas_independent_listing() {
  DTA dta_a = {0};
  DTA dta_b = {0};
  char first_a[14] = {0};
  char first_b[14] = {0};
  int fd = 0;
  int result = 0;

  Dsetpath("\\");
  cleanup_multidta_folders();

  result = Dcreate("DTAONE");
  assert_result("Ensure folder DTAONE exists",
                (result == 0 || result == -36), TRUE);

  result = Dcreate("DTATWO");
  assert_result("Ensure folder DTATWO exists",
                (result == 0 || result == -36), TRUE);

  fd = Fcreate("DTAONE\\A1.TXT", 0);
  assert_result("Create DTAONE\\A1.TXT", fd >= 0, TRUE);
  if (fd >= 0) Fclose(fd);

  fd = Fcreate("DTAONE\\A2.TXT", 0);
  assert_result("Create DTAONE\\A2.TXT", fd >= 0, TRUE);
  if (fd >= 0) Fclose(fd);

  fd = Fcreate("DTATWO\\B1.TXT", 0);
  assert_result("Create DTATWO\\B1.TXT", fd >= 0, TRUE);
  if (fd >= 0) Fclose(fd);

  fd = Fcreate("DTATWO\\B2.TXT", 0);
  assert_result("Create DTATWO\\B2.TXT", fd >= 0, TRUE);
  if (fd >= 0) Fclose(fd);

  Fsetdta(&dta_a);
  result = Fsfirst("DTAONE\\A?.TXT", 0);
  assert_result("Fsfirst with DTA A finds first file in DTAONE", result, 0);
  if (result == 0) {
    strcpy(first_a, dta_a.d_fname);
    assert_result("DTA A first file belongs to DTAONE", dta_a.d_fname[0] == 'A',
                  TRUE);
  }

  Fsetdta(&dta_b);
  result = Fsfirst("DTATWO\\B?.TXT", 0);
  assert_result("Fsfirst with DTA B finds first file in DTATWO", result, 0);
  if (result == 0) {
    strcpy(first_b, dta_b.d_fname);
    assert_result("DTA B first file belongs to DTATWO", dta_b.d_fname[0] == 'B',
                  TRUE);
  }

  Fsetdta(&dta_a);
  result = Fsnext();
  assert_result("Fsnext on DTA A resumes DTAONE listing", result, 0);
  if (result == 0) {
    assert_result("DTA A second file belongs to DTAONE",
                  dta_a.d_fname[0] == 'A', TRUE);
    assert_result("DTA A advanced to the second file",
                  strcmp(dta_a.d_fname, first_a) != 0, TRUE);
  }

  Fsetdta(&dta_b);
  result = Fsnext();
  assert_result("Fsnext on DTA B resumes DTATWO listing", result, 0);
  if (result == 0) {
    assert_result("DTA B second file belongs to DTATWO",
                  dta_b.d_fname[0] == 'B', TRUE);
    assert_result("DTA B advanced to the second file",
                  strcmp(dta_b.d_fname, first_b) != 0, TRUE);
  }

  Fsetdta(&dta_a);
  result = Fsnext();
  assert_result("Fsnext on exhausted DTA A reaches end", result, -49);

  Fsetdta(&dta_b);
  result = Fsnext();
  assert_result("Fsnext on exhausted DTA B reaches end", result, -49);

  cleanup_multidta_folders();
}

int run_folder_listing_tests(int presskey) {
  print("=== GEMDOS Folder Listing Test Suite ===\n\r");
  test_directory_listing_wildcards();
  if (presskey) press_key("");
  test_directory_listing_all_files();
  if (presskey) press_key("");
  test_directory_listing_empty_folder();
  if (presskey) press_key("");
  test_directory_listing_nonexistent_folder();
  if (presskey) press_key("");
  test_directory_listing_by_extension();
  if (presskey) press_key("");
  test_listing_with_attributes();
  if (presskey) press_key("");
  test_fsnext_after_end();
  if (presskey) press_key("");
  test_directory_listing_includes_subdirs();
  if (presskey) press_key("");
  test_multiple_dtas_independent_listing();
  if (presskey) press_key("");
  print("=== End of GEMDOS Folder Listing Test Suite ===\n\r");
}
