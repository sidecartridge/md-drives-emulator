#include "chksum_tests.h"

#include "test_runner.h"
void test_checksum_known_pattern_even() {
  const char pattern[] = {0x01, 0x02, 0x03,
                          0x04};  // Checksum = 0x0102 + 0x0304 = 0x0406
  const int size = sizeof(pattern);
  char fname[] = "CHECKSUM.BIN";

  int handle = Fcreate(fname, 0);
  assert_result("Create CHECKSUM.BIN", handle >= 0, 1);

  long written = Fwrite(handle, size, pattern);
  Fclose(handle);
  assert_result("Write CHECKSUM.BIN", written == size, 1);

  handle = Fopen(fname, 0);
  if (handle >= 0) {
    char buffer[4] = {0};
    long read = Fread(handle, size, buffer);
    Fclose(handle);

    int match = (read == size && memcmp(buffer, pattern, size) == 0);
    assert_result("Verify CHECKSUM.BIN content matches", match, 1);
  } else {
    assert_result("Open CHECKSUM.BIN failed", 0, 1);
  }

  Fdelete(fname);
}

void test_checksum_known_pattern_odd() {
  const char* labels[] = {"ODD01", "ODD03", "ODD05", "ODD07"};
  const char patterns[][8] = {
      {0x01},
      {0x01, 0x02, 0x03},
      {0x01, 0x02, 0x03, 0x04, 0x05},
      {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
  };
  const int sizes[] = {1, 3, 5, 7};

  for (int i = 0; i < 4; ++i) {
    const char* name = labels[i];
    const char* pattern = patterns[i];
    int size = sizes[i];

    char fname[20];
    sprintf(fname, "%s.BIN", name);

    char label[64];
    sprintf(label, "Create %s (%d bytes)", fname, size);
    int handle = Fcreate(fname, 0);
    assert_result(label, handle >= 0, 1);

    long written = Fwrite(handle, size, pattern);
    Fclose(handle);
    sprintf(label, "Write %s (%d bytes)", fname, size);
    assert_result(label, written == size, 1);

    handle = Fopen(fname, 0);
    if (handle >= 0) {
      char buffer[8] = {0};
      long read = Fread(handle, size, buffer);
      Fclose(handle);

      int match = (read == size && memcmp(buffer, pattern, size) == 0);
      sprintf(label, "Verify %s content matches (%d bytes)", fname, size);
      assert_result(label, match, 1);
    } else {
      sprintf(label, "Open %s failed", fname);
      assert_result(label, 0, 1);
    }

    Fdelete(fname);
  }
};

int run_chksum_tests(int presskey) {
  print("=== GEMDOS Checksum Write Test Suite ===\n\r");

  test_checksum_known_pattern_even();
  if (presskey) press_key("");
  test_checksum_known_pattern_odd();
  if (presskey) press_key("");

  print("=== All Files tests completed ===\n\r");
  return 0;
}
