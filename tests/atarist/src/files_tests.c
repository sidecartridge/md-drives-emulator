#include "files_tests.h"

#include "test_runner.h"
void test_create_write_read_file() {
  int handle = Fcreate("TEST1.TXT", 0);
  assert_result("Create file TEST1.TXT", handle >= 16384, TRUE);

  const char *msg = "Hello, GEMDOS!!!!!";
  long written = Fwrite(handle, strlen(msg), msg);
  assert_result("Write to TEST1.TXT", written == (long)strlen(msg), 1);

  Fclose(handle);

  handle = Fopen("TEST1.TXT", 0);
  char buffer[32] = {0};
  long read = Fread(handle, written, buffer);
  Fclose(handle);

  assert_result("Read from TEST1.TXT matches written", strcmp(buffer, msg) == 0,
                1);
  Fdelete("TEST1.TXT");
}

void test_write_power_of_two_files() {
  for (int n = 0; n <= 15; ++n) {
    int size = 1 << n;
    char *buffer = malloc(size);
    if (!buffer) {
      print("[FAIL] Failed to allocate write buffer for 2^%d\n\r", n);
      continue;
    }
    for (int i = 0; i < size; ++i) {
      buffer[i] = (char)(i & 0xFF);  // pattern: 0x00 to 0xFF
    }

    char fname[20];
    sprintf(fname, "POW2_%02d.BIN", n);
    int handle = Fcreate(fname, 0);
    if (handle < 0) {
      assert_result("Create file failed", 0, 1);
      free(buffer);
      continue;
    }

    long written = Fwrite(handle, size, buffer);
    Fclose(handle);

    char testname[64];
    sprintf(testname, "Write file of size 2^%d (%d bytes)", n, size);
    assert_result(testname, written == size, 1);

    handle = Fopen(fname, 0);
    if (handle >= 0) {
      char *readbuf = malloc(size);
      if (!readbuf) {
        print("[FAIL] Failed to allocate read buffer for 2^%d\n\r", n);
        Fclose(handle);
        free(buffer);
        continue;
      }

      long read = Fread(handle, size, readbuf);
      int match = (read == size && memcmp(buffer, readbuf, size) == 0);

      char readtest[64];
      sprintf(readtest, "Verify file 2^%d content matches", n);
      assert_result(readtest, match, 1);

      free(readbuf);
      Fclose(handle);
    } else {
      char err[64];
      sprintf(err, "Open failed for file 2^%d", n);
      assert_result(err, 0, 1);
    }

    Fdelete(fname);
    free(buffer);
  }
}

void test_write_power_of_two_minus_one_files() {
  for (int n = 1; n <= 15; ++n) {
    int size = (1 << n) - 1;
    char *buffer = malloc(size);
    if (!buffer) {
      print("[FAIL] Failed to allocate write buffer for 2^%d - 1\n\r", n);
      continue;
    }
    for (int i = 0; i < size; ++i) {
      buffer[i] = (char)(i & 0xFF);  // pattern: 0x00 to 0xFF
    }

    char fname[24];
    sprintf(fname, "POW2M1%02d.BIN", n);
    int handle = Fcreate(fname, 0);
    if (handle < 0) {
      assert_result("Create file failed", 0, 1);
      free(buffer);
      continue;
    }

    long written = Fwrite(handle, size, buffer);
    Fclose(handle);

    char testname[64];
    sprintf(testname, "Write file of size 2^%d - 1 (%d bytes)", n, size);
    assert_result(testname, written == size, 1);

    handle = Fopen(fname, 0);
    if (handle >= 0) {
      char *readbuf = malloc(size);
      if (!readbuf) {
        print("[FAIL] Failed to allocate read buffer for 2^%d - 1\n\r", n);
        Fclose(handle);
        free(buffer);
        continue;
      }

      long read = Fread(handle, size, readbuf);
      int match = (read == size && memcmp(buffer, readbuf, size) == 0);

      char readtest[64];
      sprintf(readtest, "Verify file 2^%d - 1 content matches", n);
      assert_result(readtest, match, 1);

      free(readbuf);
      Fclose(handle);
    } else {
      char err[64];
      sprintf(err, "Open failed for file 2^%d - 1", n);
      assert_result(err, 0, 1);
    }

    Fdelete(fname);
    free(buffer);
  }
}

void test_write_files_linear_0_to_32() {
  for (int size = 0; size <= 32; ++size) {
    char *buffer = malloc(size);
    if (!buffer && size > 0) {
      print("[FAIL] Failed to allocate write buffer for size %d\n\r", size);
      continue;
    }
    for (int i = 0; i < size; ++i) {
      buffer[i] = (char)(i & 0xFF);
    }

    char fname[24];
    sprintf(fname, "LNEAR_%02d.BIN", size);
    int handle = Fcreate(fname, 0);
    if (handle < 0) {
      assert_result("Create file failed", 0, 1);
      free(buffer);
      continue;
    }

    long written = Fwrite(handle, size, buffer);
    Fclose(handle);

    char testname[64];
    sprintf(testname, "Write file of size %d bytes", size);
    assert_result(testname, written == size, 1);

    handle = Fopen(fname, 0);
    if (handle >= 0) {
      char *readbuf = malloc(size);
      if (!readbuf && size > 0) {
        print("[FAIL] Failed to allocate read buffer for size %d\n\r", size);
        Fclose(handle);
        free(buffer);
        continue;
      }

      long read = Fread(handle, size, readbuf);
      int match =
          (read == size && (size == 0 || memcmp(buffer, readbuf, size) == 0));

      char readtest[64];
      sprintf(readtest, "Verify file of %d bytes matches", size);
      assert_result(readtest, match, 1);

      free(readbuf);
      Fclose(handle);
    } else {
      char err[64];
      sprintf(err, "Open failed for file size %d", size);
      assert_result(err, 0, 1);
    }

    Fdelete(fname);
    free(buffer);
  }
}

void test_open_nonexistent_file() {
  int handle = Fopen("NOFILE.TXT", 0);
  assert_result("Open non-existent file", handle < 0, 1);
}

void test_write_empty_file() {
  int handle = Fcreate("EMPTY.TXT", 0);
  long written = Fwrite(handle, 0, "");
  assert_result("Write empty file with zero content", written == 0, TRUE);
  Fclose(handle);
  Fdelete("EMPTY.TXT");
}

void test_write_sizes() {
  char block_32k[32768];
  memset(block_32k, 'A', sizeof(block_32k));

  int handle = Fcreate("SIZES.TXT", 0);
  Fwrite(handle, 1, "T");
  Fwrite(handle, 1, "E");
  Fwrite(handle, 1, "S");
  Fwrite(handle, 1, "T");
  long written = Fwrite(handle, sizeof(block_32k), block_32k);
  assert_result("Write 32KB block", written == 32768, 1);
  Fclose(handle);
  Fdelete("SIZES.TXT");
}

void test_append_mode() {
  int handle = Fcreate("APPEND.TXT", 0);
  Fwrite(handle, 4, "INIT");
  Fclose(handle);

  handle = Fopen("APPEND.TXT", 1);  // write-only
  Fseek(0, handle, 2);              // SEEK_END
  Fwrite(handle, 3, "XYZ");
  Fclose(handle);

  handle = Fopen("APPEND.TXT", 0);
  char buffer[8] = {0};
  Fread(handle, 7, buffer);
  Fclose(handle);

  assert_result("Append data to file", strcmp(buffer, "INITXYZ") == 0, 1);
  Fdelete("APPEND.TXT");
}

void test_tiny_files() {
  int handle = Fcreate("TINY.TXT", 0);
  Fwrite(handle, 1, "A");
  Fclose(handle);

  handle = Fopen("TINY.TXT", 0);
  char buffer[1] = {0};
  long read = Fread(handle, 1, buffer);
  Fclose(handle);

  print("Read tiny file content: '%s', should be 'A'\n", buffer);
  assert_result("Read tiny file", read == 1 && buffer[0] == 'A', 1);

  handle = Fcreate("TINY.TXT", 0);  // recreate
  Fwrite(handle, 2, "BC");
  Fclose(handle);

  handle = Fopen("TINY.TXT", 0);
  char buffer2[16] = {0};
  memset(buffer2, 0, sizeof(buffer2));
  read = Fread(handle, 2, buffer2);
  Fclose(handle);

  print("Read recreated tiny file content: '%s', should be 'BC'\n", buffer2);
  assert_result("Read recreated tiny file",
                read == 2 && strcmp(buffer2, "BC") == 0, 1);

  handle = Fcreate("TINY.TXT", 0);  // recreate again
  Fwrite(handle, 3, "DEF");
  Fclose(handle);

  handle = Fopen("TINY.TXT", 0);
  char buffer3[16] = {0};
  memset(buffer3, 0, sizeof(buffer3));
  read = Fread(handle, 3, buffer3);
  Fclose(handle);

  print("Read recreated tiny file content: '%s', should be 'DEF'\n", buffer3);
  assert_result("Read recreated tiny file again",
                read == 3 && strcmp(buffer3, "DEF") == 0, 1);

  handle = Fcreate("TINY.TXT", 0);  // recreate again
  Fwrite(handle, 4, "GHIJ");
  Fclose(handle);

  handle = Fopen("TINY.TXT", 0);
  char buffer4[16] = {0};
  memset(buffer4, 0, sizeof(buffer4));
  read = Fread(handle, 4, buffer4);
  Fclose(handle);

  print("Read recreated tiny file content: '%s', should be 'GHIJ'\n", buffer4);
  assert_result("Read recreated tiny file again",
                read == 4 && strcmp(buffer4, "GHIJ") == 0, 1);

  Fdelete("TINY.TXT");
}

void test_overwrite_file() {
  int handle = Fcreate("OVER.TXT", 0);
  Fwrite(handle, 6, "OLD123");
  Fclose(handle);

  handle = Fcreate("OVER.TXT", 0);  // recreate, should overwrite
  long written = Fwrite(handle, 3, "NEW");
  Fclose(handle);

  handle = Fopen("OVER.TXT", 0);
  char buffer[8] = {0};
  Fread(handle, 3, buffer);
  Fclose(handle);

  print("Content of OVER.TXT: '%s', should be 'NEW'\n", buffer);
  assert_result("Overwrite existing file", written == 3, 1);
  assert_result("Read after overwrite", strcmp(buffer, "NEW") == 0, 1);
  Fdelete("OVER.TXT");
}

void test_delete_file() {
  int handle = Fcreate("DEL.TXT", 0);
  Fclose(handle);
  int result = Fdelete("DEL.TXT");
  assert_result("Delete file DEL.TXT", result, 0);
}

void test_fseek_positions() {
  int handle = Fcreate("SEEK.TXT", 0);
  Fwrite(handle, 5, "12345");

  long pos = Fseek(0, handle, 0);  // SEEK_SET
  assert_result("Seek to start", pos == 0, 1);

  pos = Fseek(0, handle, 2);  // SEEK_END
  assert_result("Seek to end", pos == 5, 1);

  pos = Fseek(-2, handle, 1);  // SEEK_CUR
  assert_result("Seek back 2 from end", pos == 3, 1);

  Fclose(handle);
  Fdelete("SEEK.TXT");
}

void test_read_after_write_no_close() {
  int handle = Fcreate("RAW.TXT", 0);
  Fwrite(handle, 6, "ABCDEF");
  Fseek(0, handle, 0);

  char buffer[8] = {0};
  Fread(handle, 6, buffer);
  assert_result("Read-after-write without close", strcmp(buffer, "ABCDEF") == 0,
                1);
  Fclose(handle);
  Fdelete("RAW.TXT");
}

void test_truncation_on_recreate() {
  int handle = Fcreate("TRUNC.TXT", 0);
  Fwrite(handle, 10, "ABCDEFGHIJ");
  Fclose(handle);

  handle = Fcreate("TRUNC.TXT", 0);  // recreate
  Fwrite(handle, 3, "XYZ");
  Fclose(handle);

  handle = Fopen("TRUNC.TXT", 0);
  char buffer[12] = {0};
  Fread(handle, 10, buffer);
  Fclose(handle);

  assert_result("Truncate file on recreate", strcmp(buffer, "XYZ") == 0, 1);
  Fdelete("TRUNC.TXT");
}

void test_file_handle_exhaustion() {
  int count = 0;
  int handles[32];
  for (int i = 0; i < 32; ++i) {
    char fname[16];
    sprintf(fname, "TEMP%02d.TXT", i);
    handles[i] = Fcreate(fname, 0);
    if (handles[i] < 0) break;
    count++;
  }
  assert_result("Open many file handles", count >= 8, 1);
  for (int i = 0; i < count; ++i) {
    Fclose(handles[i]);
    char fname[16];
    sprintf(fname, "TEMP%02d.TXT", i);
    Fdelete(fname);
  }
}

void test_fseek_overwrite_middle() {
  int handle = Fcreate("MID.TXT", 0);
  Fwrite(handle, 10, "1234567890");
  Fseek(-5, handle, 2);
  Fwrite(handle, 5, "abcde");
  Fclose(handle);

  handle = Fopen("MID.TXT", 0);
  char buffer[16] = {0};
  Fread(handle, 10, buffer);
  Fclose(handle);

  assert_result("Overwrite middle of file", strcmp(buffer, "12345abcde") == 0,
                1);
  Fdelete("MID.TXT");
}

void test_partial_read() {
  int handle = Fcreate("PART.TXT", 0);
  Fwrite(handle, 8, "ABCDEFGH");
  Fseek(0, handle, 0);

  char buf1[4] = {0};
  Fread(handle, 3, buf1);
  char buf2[6] = {0};
  Fread(handle, 5, buf2);

  Fclose(handle);

  assert_result("Partial read 1st chunk", strcmp(buf1, "ABC") == 0, 1);
  assert_result("Partial read 2nd chunk", strcmp(buf2, "DEFGH") == 0, 1);
  Fdelete("PART.TXT");
}

void test_concurrent_handles() {
  int fd = Fcreate("DUAL.TXT", 0);
  Fclose(fd);

  // IMPORTANT: FATFS does not support concurrent file access when opening
  // files in write mode
  int h1 = Fopen("DUAL.TXT", 1);
  int h2 = Fopen("DUAL.TXT", 1);
  assert_result("Concurrent write to an open file must fail.", h2, -33);
  Fwrite(h1, 6, "ONETWO");
  Fclose(h1);

  int r1 = Fopen("DUAL.TXT", 0);
  int r2 = Fopen("DUAL.TXT", 0);
  char buf1[8] = {0};
  Fread(r1, 6, buf1);
  char buf2[8] = {0};
  Fread(r2, 6, buf2);
  Fclose(r1);
  Fclose(r2);

  assert_result("Concurrent read handles file 1", strlen(buf1), 6);
  assert_result("Concurrent read handles file 2", strlen(buf2), 6);
  assert_result("Concurrent read handles file 1 and 2 same content",
                strcmp(buf1, buf2), 0);
  Fdelete("DUAL.TXT");
}

void test_delete_while_open() {
  int h = Fcreate("LOCK.TXT", 0);
  int res = Fdelete("LOCK.TXT");
  assert_result("Delete open file should fail", res < 0, 1);
  Fclose(h);
  Fdelete("LOCK.TXT");
}

int run_files_tests(int presskey) {
  print("=== GEMDOS Files Test Suite ===\n\r");

  test_tiny_files();
  if (presskey) press_key("");
  test_delete_while_open();
  if (presskey) press_key("");
  test_concurrent_handles();
  if (presskey) press_key("");
  test_fseek_positions();
  if (presskey) press_key("");
  test_fseek_overwrite_middle();
  if (presskey) press_key("");
  test_open_nonexistent_file();
  if (presskey) press_key("");
  test_write_empty_file();
  if (presskey) press_key("");
  test_create_write_read_file();
  if (presskey) press_key("");
  test_write_sizes();
  if (presskey) press_key("");
  test_append_mode();
  if (presskey) press_key("");
  test_overwrite_file();
  if (presskey) press_key("");
  test_delete_file();
  if (presskey) press_key("");
  test_read_after_write_no_close();
  if (presskey) press_key("");
  test_truncation_on_recreate();
  if (presskey) press_key("");
  test_file_handle_exhaustion();
  if (presskey) press_key("");
  test_partial_read();
  if (presskey) press_key("");
  test_write_power_of_two_files();
  if (presskey) press_key("");
  test_write_power_of_two_minus_one_files();
  if (presskey) press_key("");
  test_write_files_linear_0_to_32();
  if (presskey) press_key("");

  print("=== All Files tests completed ===\n\r");
  return 0;
}
