#include "files_tests.h"

#include "test_runner.h"

typedef struct {
  unsigned short time;
  unsigned short date;
} DosDateTime;

static unsigned short dos_make_time(unsigned short hour, unsigned short minute,
                                    unsigned short second) {
  return (unsigned short)(((hour & 0x1F) << 11) | ((minute & 0x3F) << 5) |
                          ((second / 2) & 0x1F));
}

static unsigned short dos_make_date(unsigned short year, unsigned short month,
                                    unsigned short day) {
  return (unsigned short)((((year - 1980) & 0x7F) << 9) |
                          ((month & 0x0F) << 5) | (day & 0x1F));
}

static void cleanup_fattrib_test_file(void) {
  Fattrib("FATTR.TXT", 1, 0x00);
  Fdelete("FATTR.TXT");
}

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

void test_file_rename_roundtrip() {
  int handle = Fcreate("RENOLD.TXT", 0);
  long written = Fwrite(handle, 7, "RENAMED");
  Fclose(handle);
  assert_result("Write RENOLD.TXT before rename", written == 7, 1);

  int result = Frename(0, "RENOLD.TXT", "RENNEW.TXT");
  assert_result("Rename RENOLD.TXT to RENNEW.TXT", result, 0);

  handle = Fopen("RENOLD.TXT", 0);
  assert_result("Open old renamed path should fail", handle, -33);

  handle = Fopen("RENNEW.TXT", 0);
  assert_result("Open RENNEW.TXT after rename", handle >= 0, 1);
  if (handle >= 0) {
    char buffer[16] = {0};
    long read = Fread(handle, 7, buffer);
    Fclose(handle);
    assert_result("Read renamed file content", read == 7, 1);
    assert_result("Renamed file content matches", strcmp(buffer, "RENAMED"),
                  0);
  }

  Fdelete("RENOLD.TXT");
  Fdelete("RENNEW.TXT");
}

void test_file_rename_error_paths() {
  int handle = 0;
  int result = 0;

  Fdelete("RENMISS.TXT");
  Fdelete("RENEXIST.TXT");
  Fdelete("RENDEST.TXT");

  result = Frename(0, "RENMISS.TXT", "RENDEST.TXT");
  assert_result("Rename missing source file should fail", result, -33);

  handle = Fcreate("RENEXIST.TXT", 0);
  assert_result("Create RENEXIST.TXT before rename error test", handle >= 0, 1);
  if (handle >= 0) {
    Fwrite(handle, 6, "SOURCE");
    Fclose(handle);
  }

  handle = Fcreate("RENDEST.TXT", 0);
  assert_result("Create RENDEST.TXT before rename error test", handle >= 0, 1);
  if (handle >= 0) {
    Fwrite(handle, 4, "DEST");
    Fclose(handle);
  }

  result = Frename(0, "RENEXIST.TXT", "RENDEST.TXT");
  assert_result("Rename onto existing destination should fail", result, -36);

  handle = Fopen("RENEXIST.TXT", 0);
  assert_result("Source file still exists after failed rename", handle >= 0, 1);
  if (handle >= 0) {
    Fclose(handle);
  }

  handle = Fopen("RENDEST.TXT", 0);
  assert_result("Destination file still exists after failed rename",
                handle >= 0, 1);
  if (handle >= 0) {
    Fclose(handle);
  }

  Fdelete("RENMISS.TXT");
  Fdelete("RENEXIST.TXT");
  Fdelete("RENDEST.TXT");
}

void test_fattrib_roundtrip() {
  cleanup_fattrib_test_file();
  int handle = Fcreate("FATTR.TXT", 0);
  assert_result("Create FATTR.TXT", handle >= 0, 1);
  Fclose(handle);

  long result = Fattrib("FATTR.TXT", 0, 0);
  assert_result("Inquire attributes of FATTR.TXT", result >= 0, 1);

  result = Fattrib("FATTR.TXT", 1, 0x01);
  assert_result("Set readonly on FATTR.TXT returns previous attributes", result,
                0x20);

  result = Fattrib("FATTR.TXT", 0, 0);
  assert_result("Readonly bit is set on FATTR.TXT", result & 0x03, 0x01);

  result = Fattrib("FATTR.TXT", 1, 0x02);
  assert_result("Set hidden on FATTR.TXT returns previous attributes", result,
                0x21);

  result = Fattrib("FATTR.TXT", 0, 0);
  assert_result("Hidden bit is set on FATTR.TXT", result & 0x03, 0x02);

  result = Fattrib("FATTR.TXT", 1, 0x03);
  assert_result("Set readonly+hidden on FATTR.TXT returns previous attributes",
                result, 0x22);

  result = Fattrib("FATTR.TXT", 0, 0);
  assert_result("Readonly+hidden bits are set on FATTR.TXT", result & 0x03,
                0x03);

  result = Fattrib("FATTR.TXT", 1, 0x00);
  assert_result(
      "Clear readonly+hidden on FATTR.TXT returns previous attributes", result,
      0x23);

  result = Fattrib("FATTR.TXT", 0, 0);
  assert_result("Readonly+hidden bits are cleared on FATTR.TXT", result & 0x03,
                0);

  result = Fdelete("FATTR.TXT");
  assert_result("Delete FATTR.TXT", result, 0);

  result = Fattrib("FATTR.TXT", 0, 0);
  assert_result("Inquire deleted FATTR.TXT should fail", result, -33);

  cleanup_fattrib_test_file();
}

void test_fdatime_roundtrip() {
  int handle = Fcreate("FDTIME.TXT", 0);
  Fwrite(handle, 4, "TIME");
  Fclose(handle);

  handle = Fopen("FDTIME.TXT", 2);
  assert_result("Open FDTIME.TXT read/write", handle >= 0, 1);
  if (handle >= 0) {
    DosDateTime original = {0};
    DosDateTime expected_a = {dos_make_time(13, 25, 10),
                              dos_make_date(2024, 4, 1)};
    DosDateTime set_a = expected_a;
    DosDateTime verify_same_handle_a = {0};
    DosDateTime verify_reopen_a = {0};
    DosDateTime expected_b = {dos_make_time(21, 42, 58),
                              dos_make_date(2025, 12, 31)};
    DosDateTime set_b = expected_b;
    DosDateTime verify_same_handle_b = {0};
    DosDateTime verify_reopen_b = {0};

    int result = Fdatime(&original, handle, 0);
    assert_result("Inquire FDTIME.TXT timestamp", result, 0);

    result = Fdatime(&set_a, handle, 1);
    assert_result("Set FDTIME.TXT timestamp A", result, 0);

    result = Fdatime(&verify_same_handle_a, handle, 0);
    assert_result("Re-inquire FDTIME.TXT timestamp A on same handle", result,
                  0);
    assert_result("FDTIME.TXT time A matches expected on same handle",
                  verify_same_handle_a.time, expected_a.time);
    assert_result("FDTIME.TXT date A matches expected on same handle",
                  verify_same_handle_a.date, expected_a.date);

    result = Fclose(handle);
    assert_result("Close FDTIME.TXT after timestamp A", result, 0);

    handle = Fopen("FDTIME.TXT", 0);
    assert_result("Re-open FDTIME.TXT read-only after timestamp A",
                  handle >= 0, 1);
    if (handle >= 0) {
      result = Fdatime(&verify_reopen_a, handle, 0);
      assert_result("Re-inquire FDTIME.TXT timestamp A after reopen", result,
                    0);
      assert_result("FDTIME.TXT time A matches expected after reopen",
                    verify_reopen_a.time, expected_a.time);
      assert_result("FDTIME.TXT date A matches expected after reopen",
                    verify_reopen_a.date, expected_a.date);
      Fclose(handle);
    }

    handle = Fopen("FDTIME.TXT", 2);
    assert_result("Re-open FDTIME.TXT read/write for timestamp B",
                  handle >= 0, 1);
    if (handle >= 0) {
      result = Fdatime(&set_b, handle, 1);
      assert_result("Set FDTIME.TXT timestamp B", result, 0);

      result = Fdatime(&verify_same_handle_b, handle, 0);
      assert_result("Re-inquire FDTIME.TXT timestamp B on same handle", result,
                    0);
      assert_result("FDTIME.TXT time B matches expected on same handle",
                    verify_same_handle_b.time, expected_b.time);
      assert_result("FDTIME.TXT date B matches expected on same handle",
                    verify_same_handle_b.date, expected_b.date);

      result = Fclose(handle);
      assert_result("Close FDTIME.TXT after timestamp B", result, 0);
    }

    handle = Fopen("FDTIME.TXT", 0);
    assert_result("Final re-open FDTIME.TXT read-only after timestamp B",
                  handle >= 0, 1);
    if (handle >= 0) {
      result = Fdatime(&verify_reopen_b, handle, 0);
      assert_result("Re-inquire FDTIME.TXT timestamp B after reopen", result,
                    0);
      assert_result("FDTIME.TXT time B matches expected after reopen",
                    verify_reopen_b.time, expected_b.time);
      assert_result("FDTIME.TXT date B matches expected after reopen",
                    verify_reopen_b.date, expected_b.date);
      Fclose(handle);
    }
  }

  Fdelete("FDTIME.TXT");
}

void test_fdatime_invalid_handle() {
  int handle = Fcreate("FDTBAD.TXT", 0);
  DosDateTime query = {0};
  DosDateTime set_value = {dos_make_time(10, 20, 30),
                           dos_make_date(2024, 6, 15)};

  assert_result("Create FDTBAD.TXT for invalid-handle test", handle >= 0, 1);
  if (handle >= 0) {
    assert_result("Close FDTBAD.TXT before invalid-handle test", Fclose(handle),
                  0);

    assert_result("Fdatime inquire on closed handle fails",
                  Fdatime(&query, handle, 0), -37);
    assert_result("Fdatime set on closed handle fails",
                  Fdatime(&set_value, handle, 1), -37);
  }

  Fdelete("FDTBAD.TXT");
}

void test_eof_and_closed_handle_behavior() {
  int handle = Fcreate("EOFCLOSE.TXT", 0);
  Fwrite(handle, 5, "ABCDE");
  Fclose(handle);

  handle = Fopen("EOFCLOSE.TXT", 0);
  assert_result("Open EOFCLOSE.TXT for EOF checks", handle >= 0, 1);
  if (handle >= 0) {
    char buffer[8] = {0};
    char eof_buffer[4] = {0};
    long read = Fread(handle, 8, buffer);
    assert_result("Read to EOF returns short length", read, 5);
    assert_result("Read to EOF content matches", strcmp(buffer, "ABCDE"), 0);

    read = Fread(handle, 3, eof_buffer);
    assert_result("Read past EOF returns zero", read, 0);

    assert_result("Close EOFCLOSE.TXT handle", Fclose(handle), 0);
    assert_result("Close EOFCLOSE.TXT handle twice fails", Fclose(handle), -37);

    read = Fread(handle, 1, buffer);
    assert_result("Read from closed handle fails", read, -37);

    long pos = Fseek(0, handle, 0);
    assert_result("Seek on closed handle fails", pos, -37);
  }

  Fdelete("EOFCLOSE.TXT");
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
  test_file_rename_roundtrip();
  if (presskey) press_key("");
  test_file_rename_error_paths();
  if (presskey) press_key("");
  test_fattrib_roundtrip();
  if (presskey) press_key("");
  test_fdatime_roundtrip();
  if (presskey) press_key("");
  test_fdatime_invalid_handle();
  if (presskey) press_key("");
  test_eof_and_closed_handle_behavior();
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
