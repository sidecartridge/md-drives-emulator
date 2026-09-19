#ifndef WORKDIR_TESTS_H
#define WORKDIR_TESTS_H

typedef struct {
  unsigned long b_free;     // número de clusters libres
  unsigned long b_total;    // número total de clusters
  unsigned long b_secsize;  // tamaño del sector en bytes
  unsigned long b_clsiz;    // sectores por cluster
} Dfree;

int run_workdir_tests();

// Single workdir tests, selectable by name from the FSTESTS.TTP command line.
void test_change_directory_and_getpath();
void test_change_to_nonexistent_directory();
void test_return_to_parent_directory();
void test_query_free_space_on_drive_C();
void test_query_free_space_on_default_drive();
void test_get_and_set_drive();
void test_relative_file_operations_in_current_directory();

#endif  // WORKDIR_TESTS_H
