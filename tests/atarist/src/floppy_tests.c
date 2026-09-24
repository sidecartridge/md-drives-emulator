#include "floppy_tests.h"

#include <osbind.h>

#include "folder_listing_tests.h" /* DTA */
#include "test_runner.h"
#include "workdir_tests.h" /* Dfree */

/* The disk in A: is made by tools/dev/make_floppy_image.py, and these rules are
   that script's: 720 KB, 80 tracks, 2 sides, 9 sectors of 512 bytes, FAT12
   with three files, and a signature in every free sector naming the sector.
   The two must agree. */
#define SECTOR 512
#define SECTORS_PER_TRACK 9
#define SIDES 2
#define DRIVE_A 0
#define PATTERN_SIZE 20480L
#define PATTERN_FIRST_SECTOR 18 /* PATTERN.BIN starts at cluster 4 */
#define WRITTEN_SECTOR 1300     /* left written for the host to check */
#define SCRATCH_SECTOR 1301     /* written and put back */
#define XBIOS_SECTOR 1310       /* track 72, side 1, sector 6 */

static const char README[] = "SidecarTridge floppy test image.\r\n";
#define PROGRAM_SIZE 36L  /* PROG.TOS: a header, Pterm0 and no fixups */
#define PROGRAM_FLAGS 5L  /* what its header asks for */

/* Word aligned: TOS's floppy driver moves data by DMA, which cannot reach an
   odd address, and Flopver wants 1024 bytes of it as scratch. */
static unsigned short buffer_words[SECTORS_PER_TRACK * SECTOR / 2];
static unsigned char* const buffer = (unsigned char*)buffer_words;
static unsigned short spare_words[SECTOR / 2];
static unsigned char* const spare = (unsigned char*)spare_words;

static int disk_is_rw = FALSE;
/* Whether an XBIOS read of side 1 came back from drive A. A driver that gets
   the drive wrong sends XBIOS writes to another disk too, so none is made
   until this is known to be TRUE. */
static int xbios_side1_reaches_a = FALSE;

/* etv_critic: TOS hands it the error code as a word at 4(sp). Handing that
   straight back makes the call fail with it, where the default handler shows
   the desktop's alert and waits for somebody to press a button. Machine code
   so it needs no knowledge of the C calling convention:
     move.w 4(sp),d0 ; ext.l d0 ; rts */
static const unsigned short critic_returns_error[] = {0x302F, 0x0004, 0x48C0,
                                                      0x4E75};

static void make_signature(int lba, unsigned char* out) {
  out[0] = 0x46;
  out[1] = 0x4C;
  out[2] = (unsigned char)(lba >> 8);
  out[3] = (unsigned char)lba;
  for (int k = 4; k < SECTOR; k++) out[k] = (unsigned char)(lba * 31 + k);
}

static void make_written(int lba, unsigned char* out) {
  for (int k = 0; k < SECTOR; k++) {
    out[k] = (unsigned char)(lba * 17 + k * 3 + 0x5A);
  }
}

static int holds_signature(int lba, const unsigned char* data) {
  make_signature(lba, spare);
  return memcmp(data, spare, SECTOR) == 0;
}

static int holds_written(int lba, const unsigned char* data) {
  make_written(lba, spare);
  return memcmp(data, spare, SECTOR) == 0;
}

static int lba_of(int track, int side, int sector) {
  return (track * SIDES + side) * SECTORS_PER_TRACK + (sector - 1);
}

/* Which of the two disks is in the drive, from MODE.TXT. FALSE when there is
   no test disk at all, which ends the run early instead of failing every
   case on a disk it was not written for. */
static int find_the_test_disk(void) {
  char mode[8] = {0};
  int handle = Fopen("A:\\MODE.TXT", 0);
  if (handle < 0) {
    print("[SKIP] No test disk in A: (MODE.TXT: %d)\r\n", handle);
    return FALSE;
  }
  Fread(handle, sizeof(mode) - 1, mode);
  Fclose(handle);
  disk_is_rw = (mode[0] == 'R' && mode[1] == 'W');
  print("Test disk in A:, %s\r\n", disk_is_rw ? "writable" : "read-only");
  return TRUE;
}

/* ---------------------------------------------------------------- BIOS */

static void test_getbpb(void) {
  const short* bpb = (const short*)Getbpb(DRIVE_A);
  assert_result("Getbpb(A:) answers", bpb != NULL, TRUE);
  if (bpb == NULL) return;
  assert_result("BPB sector size", bpb[0], 512);
  assert_result("BPB sectors per cluster", bpb[1], 2);
  assert_result("BPB bytes per cluster", bpb[2], 1024);
  assert_result("BPB root directory sectors", bpb[3], 7);
  assert_result("BPB sectors per FAT", bpb[4], 3);
  assert_result("BPB second FAT at", bpb[5], 4);
  assert_result("BPB first data sector", bpb[6], 14);
  assert_result("BPB cluster count", bpb[7], 713);
  assert_result("BPB flags: a 12-bit FAT", bpb[8], 0);
}

static void test_mediach_after_boot(void) {
  /* Nothing changed since boot. TOS may still answer 1, "not sure", before
     it has read the disk; 2 would be wrong. */
  long answer = Mediach(DRIVE_A);
  print("Mediach(A:) = %ld\r\n", answer);
  assert_result("Mediach(A:) does not claim a change", answer != 2, TRUE);
}

static void test_rwabs_boot_sector(void) {
  long result = Rwabs(0, buffer, 1, 0, DRIVE_A);
  assert_result("Rwabs reads the boot sector", (int)result, 0);
  if (result != 0) return;
  assert_result("Boot sector: 512 bytes a sector",
                buffer[11] == 0x00 && buffer[12] == 0x02, TRUE);
  assert_result("Boot sector: media byte F9", buffer[21], 0xF9);
  assert_result("Boot sector: 9 sectors a track, 2 sides",
                buffer[24] == 9 && buffer[26] == 2, TRUE);
  assert_result("Boot sector: the serial of this disk",
                buffer[8] == 'R' && buffer[9] == (disk_is_rw ? 'W' : 'O'),
                TRUE);
}

static void test_rwabs_free_sector(void) {
  long result = Rwabs(0, buffer, 1, 1093, DRIVE_A);
  assert_result("Rwabs reads free sector 1093", (int)result, 0);
  assert_result("Sector 1093 holds its own signature",
                holds_signature(1093, buffer), TRUE);
}

static void test_rwabs_across_sides(void) {
  /* 1085 to 1093: the last four sectors of track 60 side 0, then the first
     five of side 1. */
  long result = Rwabs(0, buffer, 9, 1085, DRIVE_A);
  int good = 0;
  assert_result("Rwabs reads nine sectors across a side", (int)result, 0);
  for (int n = 0; n < 9; n++) good += holds_signature(1085 + n, buffer + n * SECTOR);
  assert_result("Each of the nine holds its own signature", good, 9);
}

static void test_rwabs_file_sector(void) {
  long result = Rwabs(0, buffer, 1, PATTERN_FIRST_SECTOR, DRIVE_A);
  int good = 1;
  assert_result("Rwabs reads the first sector of PATTERN.BIN", (int)result, 0);
  for (int i = 0; i < SECTOR; i++) {
    if (buffer[i] != (unsigned char)(i * 7 + (i >> 9))) good = 0;
  }
  assert_result("It holds the file's first 512 bytes", good, TRUE);
}

static void test_rwabs_write(void) {
  long result;
  if (!disk_is_rw) {
    /* Nothing may change on a read-only disk, and the caller must hear it. */
    make_written(SCRATCH_SECTOR, buffer);
    result = Rwabs(1, buffer, 1, SCRATCH_SECTOR, DRIVE_A);
    print("Rwabs write on the read-only disk = %ld\r\n", result);
    assert_result("A write to the read-only disk is refused", result < 0, TRUE);
    Rwabs(0, buffer, 1, SCRATCH_SECTOR, DRIVE_A);
    assert_result("And the sector is unchanged",
                  holds_signature(SCRATCH_SECTOR, buffer), TRUE);
    return;
  }
  make_written(SCRATCH_SECTOR, buffer);
  result = Rwabs(1, buffer, 1, SCRATCH_SECTOR, DRIVE_A);
  assert_result("Rwabs writes a sector", (int)result, 0);
  memset(buffer, 0, SECTOR);
  Rwabs(0, buffer, 1, SCRATCH_SECTOR, DRIVE_A);
  assert_result("Sector 1301 reads back as written",
                holds_written(SCRATCH_SECTOR, buffer), TRUE);
  make_signature(SCRATCH_SECTOR, buffer);
  result = Rwabs(1, buffer, 1, SCRATCH_SECTOR, DRIVE_A);
  memset(buffer, 0, SECTOR);
  Rwabs(0, buffer, 1, SCRATCH_SECTOR, DRIVE_A);
  assert_result("And is put back", result == 0 &&
                holds_signature(SCRATCH_SECTOR, buffer), TRUE);
}

/* --------------------------------------------------------------- XBIOS */

static void test_floprd_side(int side) {
  char name[64];
  int lba = lba_of(60, side, 5);
  short result = Floprd(buffer, 0L, DRIVE_A, 5, 60, side, 1);
  sprintf(name, "Floprd track 60 side %d sector 5", side);
  assert_result(name, result, 0);
  sprintf(name, "It is sector %d of drive A", lba);
  assert_result(name, holds_signature(lba, buffer), TRUE);
  if (side == 1 && result == 0 && holds_signature(lba, buffer)) {
    xbios_side1_reaches_a = TRUE;
  }
}

static void test_floprd_track(void) {
  int first = lba_of(61, 1, 1);
  int good = 0;
  short result = Floprd(buffer, 0L, DRIVE_A, 1, 61, 1, SECTORS_PER_TRACK);
  assert_result("Floprd a whole track, side 1", result, 0);
  for (int n = 0; n < SECTORS_PER_TRACK; n++) {
    good += holds_signature(first + n, buffer + n * SECTOR);
  }
  assert_result("Each sector of it holds its own signature", good,
                SECTORS_PER_TRACK);
}

static void test_flopwr(void) {
  short result;
  if (!xbios_side1_reaches_a) {
    print("[SKIP] Flopwr: XBIOS side 1 does not reach drive A, so a write "
          "would land on another disk\r\n");
    return;
  }
  make_written(XBIOS_SECTOR, buffer);
  result = Flopwr(buffer, 0L, DRIVE_A, 6, 72, 1, 1);
  if (!disk_is_rw) {
    print("Flopwr on the read-only disk = %d\r\n", result);
    assert_result("Flopwr to the read-only disk is refused", result < 0, TRUE);
    return;
  }
  assert_result("Flopwr track 72 side 1 sector 6", result, 0);
  /* Read it back through the BIOS: the two must agree on where it is. */
  memset(buffer, 0, SECTOR);
  Rwabs(0, buffer, 1, XBIOS_SECTOR, DRIVE_A);
  assert_result("Rwabs finds it at sector 1310",
                holds_written(XBIOS_SECTOR, buffer), TRUE);
  make_signature(XBIOS_SECTOR, buffer);
  Flopwr(buffer, 0L, DRIVE_A, 6, 72, 1, 1);
}

static void test_flopver(void) {
  /* All sectors good: 0, and an empty list - its first word 0. */
  short result;
  for (int n = 0; n < SECTOR; n++) buffer_words[n] = 0xFFFF;
  result = Flopver(buffer, 0L, DRIVE_A, 1, 60, 0, SECTORS_PER_TRACK);
  print("Flopver = %d, first word %04x\r\n", result, buffer_words[0]);
  assert_result("Flopver of a good track", result, 0);
  assert_result("It lists no bad sector", buffer_words[0], 0);
}

/* A floppy driver that hooks the XBIOS must pass on what is not a floppy call.
   Mfpint is XBIOS 13 and Flopver is 19 ($13): mistake one number for the other
   and a program installing an MFP interrupt handler never gets it installed.
   Mfpint only writes the vector at $100 + 4 * n and leaves the MFP's enable
   and mask registers alone, so the test gives interrupt 0 (the printer's busy
   line) a trampoline that jumps to the handler already there, looks at the
   table, and puts it back. Supervisor mode, for the vector table. */
static unsigned short trampoline[3];

static void test_mfpint_passes_through(void) {
  volatile long* vector = (volatile long*)0x100L; /* MFP interrupt 0 */
  long original = *vector;
  long installed;

  trampoline[0] = 0x4EF9; /* jmp original.l */
  trampoline[1] = (unsigned short)(original >> 16);
  trampoline[2] = (unsigned short)original;
  Mfpint(0, (void*)trampoline);
  installed = *vector;
  Mfpint(0, (void*)original);
  if (*vector != original) *vector = original; /* never leave ours behind */
  assert_result("Mfpint installs the vector it is given",
                installed == (long)trampoline, TRUE);
}

/* -------------------------------------------------------------- GEMDOS */

static void test_listing(void) {
  static DTA dta;
  int found = 0;
  long sizes = 0;
  void* old_dta = (void*)Fgetdta();
  Fsetdta(&dta);
  for (int r = Fsfirst("A:\\*.*", 0); r == 0; r = Fsnext()) {
    if (strcmp(dta.d_fname, "README.TXT") == 0 ||
        strcmp(dta.d_fname, "MODE.TXT") == 0 ||
        strcmp(dta.d_fname, "PATTERN.BIN") == 0 ||
        strcmp(dta.d_fname, "PROG.TOS") == 0) {
      found++;
      sizes += dta.d_length;
    }
  }
  Fsetdta(old_dta);
  assert_result("A: lists its four files", found, 4);
  assert_result("With the sizes they were written with", (int)sizes,
                (int)(sizeof(README) - 1 + 4 + PATTERN_SIZE + PROGRAM_SIZE));
}

static void test_read_files(void) {
  char text[64] = {0};
  long total = 0;
  int good = TRUE;
  int handle = Fopen("A:\\README.TXT", 0);
  assert_result("Open A:\\README.TXT", A_VALID_HANDLE(handle), TRUE);
  if (handle > 0) {
    Fread(handle, sizeof(text) - 1, text);
    Fclose(handle);
    assert_result("README.TXT reads as written", strcmp(text, README), 0);
  }
  handle = Fopen("A:\\PATTERN.BIN", 0);
  assert_result("Open A:\\PATTERN.BIN", A_VALID_HANDLE(handle), TRUE);
  if (handle <= 0) return;
  for (;;) {
    long count = Fread(handle, 4096, buffer);
    if (count <= 0) break;
    for (long i = 0; i < count; i++) {
      long at = total + i;
      if (buffer[i] != (unsigned char)(at * 7 + (at >> 9))) good = FALSE;
    }
    total += count;
  }
  Fclose(handle);
  assert_result("PATTERN.BIN is all there", (int)total, (int)PATTERN_SIZE);
  assert_result("And every byte of it is right", good, TRUE);
}

/* A program on the floppy is loaded by TOS itself: neither GEMDRIVE's loader
   nor Hatari's is involved. Loading it without running it shows what TOS does
   with the flags in its header; running it shows it runs. */
static void test_program_on_the_floppy(void) {
  long basepage = Pexec(3, "A:\\PROG.TOS", "", NULL);
  long result;
  assert_result("Load A:\\PROG.TOS without running it", basepage > 0, TRUE);
  if (basepage > 0) {
    print("PROG.TOS: p_flags = %lx, its header asks for %lx\r\n",
          ((const long*)basepage)[10], PROGRAM_FLAGS);
    Mfree((void*)basepage);
  }
  result = Pexec(0, "A:\\PROG.TOS", "", NULL);
  assert_result("Run A:\\PROG.TOS, which ends with Pterm0", (int)result, 0);
}

/* What TOS's own loader clears for a program that asks for fastload, as
   PROG.TOS does. Free memory is filled with $AA first, so a heap left as it
   was can be told from memory that happened to be zero. A measurement, not a
   verdict: it is here to learn which TOS honours the flag. */
static void test_what_the_loader_clears(void) {
  long size = (long)Malloc(-1L);
  unsigned char* block;
  long basepage;
  if (size < 65536L) {
    print("[SKIP] Not enough free memory to measure the loader\r\n");
    return;
  }
  block = (unsigned char*)Malloc(size);
  if (block == NULL) return;
  memset(block, 0xAA, size);
  Mfree(block);
  basepage = Pexec(3, "A:\\PROG.TOS", "", NULL);
  if (basepage <= 0) return;
  {
    const long* pd = (const long*)basepage;
    const unsigned char* heap = (const unsigned char*)(pd[6] + pd[7]);
    long span = pd[1] - (long)heap;
    long marked = 0;
    for (long i = 0; i < 4096 && i < span; i++) {
      if (heap[i] == 0xAA) marked++;
    }
    print("PROG.TOS asks for fastload: its heap is %s (%ld of 4096 bytes $AA)\r\n",
          marked ? "left as it was" : "cleared", marked);
  }
  Mfree((void*)basepage);
}

static void test_dfree(void) {
  Dfree info;
  int result = Dfree(&info, 1); /* A: */
  assert_result("Dfree(A:)", result, 0);
  assert_result("Dfree: 713 clusters on the disk", (int)info.b_total, 713);
  /* 690 clusters are free. Atari's TOS, 1.00 to 2.06, says 688: its Dfree
     walks the FAT from entry 0 instead of 2, counting the two reserved entries
     as used and never reaching the last two clusters. EmuTOS says 690. GEMDOS
     counts this from the FAT, so a driver serving wrong FAT sectors would show
     some other number, while either of these two is TOS being itself. */
  print("Dfree(A:) free clusters = %ld\r\n", (long)info.b_free);
  assert_result("Dfree: the free clusters TOS counts on this disk",
                info.b_free == 688 || info.b_free == 690, TRUE);
}

static void test_create_file(void) {
  int handle = Fcreate("A:\\NEWFILE.TMP", 0);
  long written;
  if (!disk_is_rw) {
    print("Fcreate on the read-only disk = %d\r\n", handle);
    assert_result("A file cannot be created on the read-only disk",
                  handle < 0, TRUE);
    if (handle >= 0) {
      Fclose(handle);
      Fdelete("A:\\NEWFILE.TMP");
    }
    return;
  }
  assert_result("Create A:\\NEWFILE.TMP", A_VALID_HANDLE(handle), TRUE);
  if (handle <= 0) return;
  for (int i = 0; i < 4096; i++) buffer[i] = (unsigned char)(i ^ 0xA5);
  written = Fwrite(handle, 4096, buffer);
  Fclose(handle);
  assert_result("Write 4 KB to it", (int)written, 4096);
  memset(buffer, 0, 4096);
  handle = Fopen("A:\\NEWFILE.TMP", 0);
  if (handle > 0) {
    int good = (Fread(handle, 4096, buffer) == 4096);
    for (int i = 0; i < 4096 && good; i++) {
      if (buffer[i] != (unsigned char)(i ^ 0xA5)) good = FALSE;
    }
    Fclose(handle);
    assert_result("NEWFILE.TMP reads back as written", good, TRUE);
  }
  assert_result("Delete NEWFILE.TMP", Fdelete("A:\\NEWFILE.TMP"), 0);
}

/* The last thing a writable run does, and deliberately not undone: the host
   reads the image back afterwards and checks this sector reached the card. */
static void test_leave_a_sector_written(void) {
  long result;
  if (!disk_is_rw) return;
  make_written(WRITTEN_SECTOR, buffer);
  result = Rwabs(1, buffer, 1, WRITTEN_SECTOR, DRIVE_A);
  assert_result("Leave sector 1300 written for the host", (int)result, 0);
}

int run_floppy_tests(void) {
  long old_critic = (long)Setexc(0x101, (long)critic_returns_error);

  print("=== Floppy: calls that are not floppy calls ===\r\n");
  test_mfpint_passes_through();

  print("=== Floppy: the test disk ===\r\n");
  if (find_the_test_disk()) {
    print("=== Floppy: BIOS ===\r\n");
    test_getbpb();
    test_mediach_after_boot();
    test_rwabs_boot_sector();
    test_rwabs_free_sector();
    test_rwabs_across_sides();
    test_rwabs_file_sector();

    print("=== Floppy: XBIOS ===\r\n");
    test_floprd_side(0);
    test_floprd_side(1);
    test_floprd_track();
    test_flopver();

    print("=== Floppy: GEMDOS ===\r\n");
    test_listing();
    test_read_files();
    test_program_on_the_floppy();
    test_what_the_loader_clears();
    test_dfree();

    print("=== Floppy: writing ===\r\n");
    test_rwabs_write();
    test_flopwr();
    test_create_file();
    test_leave_a_sector_written();
  }

  Setexc(0x101, old_critic);
  return 0;
}
