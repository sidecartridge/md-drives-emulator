#include "floppy_tests.h"

#include <osbind.h>

#include "folder_listing_tests.h" /* DTA */
#include "test_runner.h"
#include "workdir_tests.h" /* Dfree */

/* The disk in A: is made by tools/dev/make_floppy_image.py, and these rules are
   that script's: 80 tracks, 2 sides, 9 sectors of 512 bytes (720 KB) or 18
   (1.44 MB), FAT12 with four files, and a signature in every free sector
   naming the sector. The two must agree. */
#define SECTOR 512
#define MAX_SECTORS_PER_TRACK 18
#define SIDES 2
#define DRIVE_A 0
#define PATTERN_SIZE 20480L
#define WRITTEN_SECTOR 1300     /* left written for the host to check */
#define SCRATCH_SECTOR 1301     /* written and put back */
#define FAILING_SECTOR 1320     /* the host makes its next read fail */
#define CYCLE_SECTOR 1330       /* read to tell the host to press SELECT */
#define FORMAT_TRACK 75         /* Flopfmt is asked to format its side 1 */
#define ERROR -1                /* general error */
#define EREADF -11              /* read fault */
#define EWRPRO -13              /* write protected */
#define E_CHNG -14              /* media change */
#define MEDIA_CHANGED 2         /* what Mediach answers for a changed disk */

/* The two densities the script makes: 720 KB and 1.44 MB. Which one is in
   the drive is read from its boot sector's sectors per track. */
typedef struct {
  int spt;    /* sectors per track */
  int fsiz;   /* sectors per FAT */
  int datrec; /* first data sector: 1 + 2 FATs + 7 root directory sectors */
  int numcl;  /* clusters */
  int media;  /* media byte */
  const char* name;
} Geometry;
static const Geometry DOUBLE_DENSITY = {9, 3, 14, 713, 0xF9, "720 KB"};
static const Geometry HIGH_DENSITY = {18, 5, 18, 1431, 0xF0, "1.44 MB"};
/* A one-sided file system on side 0 of a two-sided 720 KB disk, as many menu
   disks are: MODE.TXT says SS. */
static const Geometry ONE_SIDED_BPB = {9, 2, 12, 354, 0xF8,
                                       "one-sided BPB on two sides"};
static const Geometry* disk = &DOUBLE_DENSITY;

static const char README[] = "SidecarTridge floppy test image.\r\n";
#define PROGRAM_SIZE 36L  /* PROG.TOS: a header, Pterm0 and no fixups */
#define PROGRAM_FLAGS 5L  /* what its header asks for */

/* Word aligned: TOS's floppy driver moves data by DMA, which cannot reach an
   odd address, and Flopver wants 1024 bytes of it as scratch. */
static unsigned short buffer_words[MAX_SECTORS_PER_TRACK * SECTOR / 2];
static unsigned char* const buffer = (unsigned char*)buffer_words;
static unsigned short spare_words[SECTOR / 2];
/* TOS's Flopfmt builds the raw track here before the controller sees it:
   60 + 612 bytes a sector + 1401, 12477 bytes for 18 sectors. */
static unsigned short format_words[16384 / 2];
static unsigned char* const spare = (unsigned char*)spare_words;

static int disk_is_rw = FALSE;
/* Whether an XBIOS read of side 1 came back from drive A. A driver that gets
   the drive wrong sends XBIOS writes to another disk too, so none is made
   until this is known to be TRUE. */
static int xbios_side1_reaches_a = FALSE;

/* etv_critic: TOS hands it the error code and the drive as two words at 4(sp)
   and 6(sp), which a C function taking one long sees as one argument, the
   error in its high word. Handing the error straight back makes the call fail
   with it, where the default handler shows the desktop's alert and waits for
   somebody to press a button. Each call is noted: TOS's floppy driver sends
   a failed Rwabs through here, and a failed Floprd or Flopwr not. */
static volatile int critic_calls = 0;
static volatile long critic_last = 0;

static long critic_returns_error(long error_and_drive) {
  critic_calls++;
  critic_last = error_and_drive;
  return (short)(error_and_drive >> 16);
}

static int critic_was_told(int error, int drive) {
  return (short)(critic_last >> 16) == error && (short)critic_last == drive;
}

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

/* Whether the buffer still holds the 0x5A it was filled with. */
static int buffer_untouched(void) {
  for (int i = 0; i < SECTOR * 2; i++) {
    if (buffer[i] != 0x5A) return FALSE;
  }
  return TRUE;
}

static int lba_of(int track, int side, int sector) {
  return (track * SIDES + side) * disk->spt + (sector - 1);
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
  if (mode[0] == 'S' && mode[1] == 'S') {
    disk = &ONE_SIDED_BPB;
  } else if (Rwabs(0, buffer, 1, 0, DRIVE_A) == 0 &&
             buffer[24] == HIGH_DENSITY.spt) {
    disk = &HIGH_DENSITY;
  }
  print("Test disk in A:, %s, %s\r\n", disk_is_rw ? "writable" : "read-only",
        disk->name);
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
  assert_result("BPB sectors per FAT", bpb[4], disk->fsiz);
  assert_result("BPB second FAT at", bpb[5], disk->fsiz + 1);
  assert_result("BPB first data sector", bpb[6], disk->datrec);
  assert_result("BPB cluster count", bpb[7], disk->numcl);
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
  assert_result("Boot sector: the media byte", buffer[21], disk->media);
  assert_result("Boot sector: its sectors a track, 2 sides",
                buffer[24] == disk->spt && buffer[26] == 2, TRUE);
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
  /* The last four sectors of track 60 side 0, then the first five of side 1. */
  int first = lba_of(60, 0, disk->spt - 3);
  long result = Rwabs(0, buffer, 9, first, DRIVE_A);
  int good = 0;
  assert_result("Rwabs reads nine sectors across a side", (int)result, 0);
  for (int n = 0; n < 9; n++) good += holds_signature(first + n, buffer + n * SECTOR);
  assert_result("Each of the nine holds its own signature", good, 9);
}

static void test_rwabs_file_sector(void) {
  /* PATTERN.BIN starts at cluster 4 */
  long result = Rwabs(0, buffer, 1, disk->datrec + 2 * 2, DRIVE_A);
  int good = 1;
  assert_result("Rwabs reads the first sector of PATTERN.BIN", (int)result, 0);
  for (int i = 0; i < SECTOR; i++) {
    if (buffer[i] != (unsigned char)(i * 7 + (i >> 9))) good = 0;
  }
  assert_result("It holds the file's first 512 bytes", good, TRUE);
}

/* TOS's floppy driver keeps a media-change state per drive: Rwabs with no
   buffer sets it to the count, Mediach answers it, Rwabs in modes 0 and 1
   answers E_CHNG while it says changed - without a transfer - and Getbpb,
   which GEMDOS calls after being told, ends it. Mode 2 and the XBIOS are
   physical and never ask. So a change can be made here without touching the
   disk, and every part of that checked against TOS's own driver. */
static void test_forced_media_change(void) {
  long result;
  short xresult;
  assert_result("Rwabs with no buffer sets the drive changed",
                (int)Rwabs(0, NULL, MEDIA_CHANGED, 0, DRIVE_A), 0);
  assert_result("Mediach then says changed", (int)Mediach(DRIVE_A),
                MEDIA_CHANGED);
  memset(buffer, 0x5A, SECTOR * 2);
  result = Rwabs(0, buffer, 1, 1093, DRIVE_A);
  assert_result("Rwabs on the changed disk answers E_CHNG", (int)result,
                E_CHNG);
  assert_result("Without reading", buffer_untouched(), TRUE);
  result = Rwabs(2, buffer, 1, 1093, DRIVE_A);
  assert_result("Rwabs mode 2 does not ask, and reads", result == 0 &&
                holds_signature(1093, buffer), TRUE);
  xresult = Floprd(buffer, 0L, DRIVE_A, 5, 60, 0, 1);
  assert_result("Nor does Floprd", xresult == 0 &&
                holds_signature(lba_of(60, 0, 5), buffer), TRUE);
  assert_result("Neither ends the change", (int)Mediach(DRIVE_A),
                MEDIA_CHANGED);
  assert_result("Getbpb answers", Getbpb(DRIVE_A) != 0, TRUE);
  assert_result("And that ends it", (int)Mediach(DRIVE_A) != MEDIA_CHANGED,
                TRUE);
  result = Rwabs(0, buffer, 1, 1093, DRIVE_A);
  assert_result("Rwabs reads again", result == 0 &&
                holds_signature(1093, buffer), TRUE);
}

/* The desktop's Esc on a window (aes/trap14.S, _mediach, from hd_keybd): it
   wraps hdv_bpb, hdv_mediach and hdv_rw so that the window's drive reads as
   changed, opens a file on it so that GEMDOS checks the drive, and counts on
   GEMDOS then asking hdv_bpb for it - that wrapper puts the three vectors
   back. The wrappers here are the desktop's, the "restored" count added. A
   driver that answers the drive in front of the vectors is never asked
   through them: the wrappers stay, and the next Esc wraps them around
   themselves, after which every disk call behind them loops for ever. */
short esc_dev;
long esc_oldgetbpb, esc_oldmediach, esc_oldrwabs;
volatile short esc_restored;
void esc_newgetbpb(void);
void esc_newmediach(void);
void esc_newrwabs(void);
__asm__(
    "_esc_newgetbpb:\n"
    "  move.w _esc_dev,d0\n"
    "  cmp.w 4(sp),d0\n"
    "  bne.s 1f\n"
    "  addq.w #1,_esc_restored\n"
    "  move.l _esc_oldgetbpb,0x472.w\n"
    "  move.l _esc_oldmediach,0x47e.w\n"
    "  move.l _esc_oldrwabs,0x476.w\n"
    "1:move.l _esc_oldgetbpb,a0\n"
    "  jmp (a0)\n"
    "_esc_newmediach:\n"
    "  move.w _esc_dev,d0\n"
    "  cmp.w 4(sp),d0\n"
    "  bne.s 2f\n"
    "  moveq #2,d0\n"
    "  rts\n"
    "2:move.l _esc_oldmediach,a0\n"
    "  jmp (a0)\n"
    "_esc_newrwabs:\n"
    "  move.w _esc_dev,d0\n"
    "  cmp.w 14(sp),d0\n"
    "  bne.s 3f\n"
    "  moveq #-14,d0\n"
    "  rts\n"
    "3:move.l _esc_oldrwabs,a0\n"
    "  jmp (a0)\n");

static void test_desktop_esc(void) {
  volatile long* const hdv_bpb = (volatile long*)0x472L;
  volatile long* const hdv_rw = (volatile long*)0x476L;
  volatile long* const hdv_mediach = (volatile long*)0x47EL;
  long handle;
  int still_wrapped;
  esc_dev = DRIVE_A;
  esc_restored = 0;
  esc_oldgetbpb = *hdv_bpb;
  esc_oldmediach = *hdv_mediach;
  esc_oldrwabs = *hdv_rw;
  *hdv_bpb = (long)esc_newgetbpb;
  *hdv_mediach = (long)esc_newmediach;
  *hdv_rw = (long)esc_newrwabs;
  handle = Fopen("A:\\X", 0);
  if (handle >= 0) Fclose((short)handle);
  still_wrapped = (*hdv_bpb == (long)esc_newgetbpb);
  if (still_wrapped) { /* the desktop takes them out itself then */
    *hdv_bpb = esc_oldgetbpb;
    *hdv_mediach = esc_oldmediach;
    *hdv_rw = esc_oldrwabs;
  }
  print("Desktop Esc on A:: Getbpb through the wrapper %d time(s), %s\r\n",
        (int)esc_restored, still_wrapped ? "wrappers left" : "wrappers gone");
  assert_result("The desktop's Esc on A: is seen through the disk vectors",
                esc_restored == 1 && !still_wrapped, TRUE);
  assert_result("And they are back as they were",
                *hdv_bpb == esc_oldgetbpb && *hdv_mediach == esc_oldmediach &&
                    *hdv_rw == esc_oldrwabs,
                TRUE);
  Getbpb(DRIVE_A); /* GEMDOS logged A: out: nothing left pending */
}

static void test_rwabs_write(void) {
  long result;
  if (!disk_is_rw) {
    /* Nothing may change on a read-only disk, and the caller must hear it. */
    int calls = critic_calls;
    make_written(SCRATCH_SECTOR, buffer);
    result = Rwabs(1, buffer, 1, SCRATCH_SECTOR, DRIVE_A);
    print("Rwabs write on the read-only disk = %ld\r\n", result);
    assert_result("A write to the read-only disk is refused, write protected",
                  (int)result, EWRPRO);
    assert_result("Through etv_critic, told the error and the drive",
                  critic_calls - calls == 1 && critic_was_told(EWRPRO, DRIVE_A),
                  TRUE);
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
  sprintf(name, "It is that sector of drive A, side %d", side);
  assert_result(name, holds_signature(lba, buffer), TRUE);
  if (side == 1 && result == 0 && holds_signature(lba, buffer)) {
    xbios_side1_reaches_a = TRUE;
  }
}

static void test_floprd_track(void) {
  int first = lba_of(61, 1, 1);
  int good = 0;
  short result = Floprd(buffer, 0L, DRIVE_A, 1, 61, 1, disk->spt);
  assert_result("Floprd a whole track, side 1", result, 0);
  for (int n = 0; n < disk->spt; n++) {
    good += holds_signature(first + n, buffer + n * SECTOR);
  }
  assert_result("Each sector of it holds its own signature", good, disk->spt);
}

static void test_flopwr(void) {
  short result;
  int calls;
  int xbios_sector = lba_of(72, 1, 6);
  if (!xbios_side1_reaches_a) {
    print("[SKIP] Flopwr: XBIOS side 1 does not reach drive A, so a write "
          "would land on another disk\r\n");
    return;
  }
  make_written(xbios_sector, buffer);
  calls = critic_calls;
  result = Flopwr(buffer, 0L, DRIVE_A, 6, 72, 1, 1);
  if (!disk_is_rw) {
    print("Flopwr on the read-only disk = %d\r\n", result);
    assert_result("Flopwr to the read-only disk is refused, write protected",
                  result, EWRPRO);
    assert_result("Without etv_critic", critic_calls - calls, 0);
    return;
  }
  assert_result("Flopwr track 72 side 1 sector 6", result, 0);
  /* Read it back through the BIOS: the two must agree on where it is. */
  memset(buffer, 0, SECTOR);
  Rwabs(0, buffer, 1, xbios_sector, DRIVE_A);
  assert_result("Rwabs finds it where Flopwr put it",
                holds_written(xbios_sector, buffer), TRUE);
  make_signature(xbios_sector, buffer);
  Flopwr(buffer, 0L, DRIVE_A, 6, 72, 1, 1);
}

static void test_flopver(void) {
  /* All sectors good: 0, and an empty list - its first word 0. */
  short result;
  for (int n = 0; n < SECTOR; n++) buffer_words[n] = 0xFFFF;
  result = Flopver(buffer, 0L, DRIVE_A, 1, 60, 0, disk->spt);
  print("Flopver = %d, first word %04x\r\n", result, buffer_words[0]);
  assert_result("Flopver of a good track", result, 0);
  assert_result("It lists no bad sector", buffer_words[0], 0);
}

/* The emulation answers Getbpb with a BPB in the cartridge's window; TOS's
   own driver with one in RAM. */
static int drive_a_is_emulated(void) {
  return ((long)Getbpb(DRIVE_A) & 0xFF0000L) == 0xFA0000L;
}

/* EmuTOS signs its ROM header with 'ETOS' at +$2C, where Atari's TOS has 0.
   The ROM is found as print_tos_version finds it; the run is in supervisor
   mode. */
static int running_emutos(void) {
  const long* rom = (*(const unsigned short*)0x4L == 0x00FC)
                        ? (const long*)0xFC0000L
                        : (const long*)0xE00000L;
  return rom[0x2C / 4] == 0x45544F53L;
}

/* A write to the boot sector is a media change, as TOS's own driver has it:
   its flopwrt marks the drive changed whenever it writes track 0, side 0,
   sector 1, whatever the data, and its getbpb reads the boot sector each time
   it is called. On the read-only disk the write is refused and nothing
   changes. The boot sector is put back as it was, and Getbpb called, so the
   run goes on with no change pending. EmuTOS's own driver has a rule of its
   own - it answers an Rwabs write of the boot sector with E_CHNG, and a
   Flopwr of it is no change - so under it the case is skipped. */
static void test_boot_sector_write(void) {
  static unsigned short original_words[SECTOR / 2];
  unsigned char* const original = (unsigned char*)original_words;
  const short* bpb;
  long result;
  short xresult;
  int entries;
  /* Its Getbpb also ends whatever the cases before left pending. */
  if (!drive_a_is_emulated() && running_emutos()) {
    print("[SKIP] Boot sector writes: EmuTOS's own driver does not follow "
          "TOS's rule\r\n");
    return;
  }
  Rwabs(0, original, 1, 0, DRIVE_A);
  if (!disk_is_rw) {
    result = Rwabs(1, original, 1, 0, DRIVE_A);
    assert_result("A write to the read-only disk's boot sector is refused",
                  (int)result, EWRPRO);
    result = Rwabs(0, buffer, 1, 0, DRIVE_A);
    assert_result("And the disk has not changed", (int)result, 0);
    return;
  }
  result = Rwabs(1, original, 1, 0, DRIVE_A);
  assert_result("Rwabs writes the boot sector back as it was", (int)result, 0);
  assert_result("Mediach then says changed", (int)Mediach(DRIVE_A),
                MEDIA_CHANGED);
  result = Rwabs(0, buffer, 1, 0, DRIVE_A);
  assert_result("And Rwabs answers E_CHNG", (int)result, E_CHNG);
  Getbpb(DRIVE_A);
  result = Rwabs(0, buffer, 1, 0, DRIVE_A);
  assert_result("Until Getbpb", (int)result, 0);

  xresult = Flopwr(original, 0L, DRIVE_A, 1, 0, 0, 1);
  assert_result("Flopwr writes the boot sector back as it was", xresult, 0);
  assert_result("Mediach then says changed, too", (int)Mediach(DRIVE_A),
                MEDIA_CHANGED);
  Getbpb(DRIVE_A);

  memcpy(buffer, original, SECTOR);
  entries = buffer[17] | (buffer[18] << 8);
  entries += SECTOR / 32; /* one more root directory sector */
  buffer[17] = entries & 0xFF;
  buffer[18] = entries >> 8;
  Rwabs(1, buffer, 1, 0, DRIVE_A);
  bpb = (const short*)Getbpb(DRIVE_A);
  assert_result("Getbpb answers the boot sector just written: 8 root sectors",
                bpb != NULL && bpb[3] == 8 && bpb[6] == disk->datrec + 1, TRUE);
  Rwabs(1, original, 1, 0, DRIVE_A);
  bpb = (const short*)Getbpb(DRIVE_A);
  assert_result("And the old one once it is put back",
                bpb != NULL && bpb[3] == 7 && bpb[6] == disk->datrec, TRUE);
  result = Rwabs(0, buffer, 1, 0, DRIVE_A);
  assert_result("With no change left pending", result == 0 &&
                memcmp(buffer, original, SECTOR) == 0, TRUE);
}

/* TOS's Flopfmt formats the track, or answers E_WRPRO on a write-protected
   disk. An emulated drive formats nothing - the ROM would format the disk in
   the physical drive instead: a read-only image answers E_WRPRO as TOS does,
   a writable one the general error, and the track is left as it was. Under
   TOS's own driver the writable case would format the test disk, so it runs
   only on an emulated drive, whose BPB lives in the cartridge. */
static void test_flopfmt(void) {
  short result;
  int good = 0;
  int first = lba_of(FORMAT_TRACK, 1, 1);
  if (disk_is_rw && !drive_a_is_emulated()) {
    print("[SKIP] Flopfmt on the writable disk: TOS's driver would format "
          "it\r\n");
    return;
  }
  result = Flopfmt(format_words, 0L, DRIVE_A, disk->spt, FORMAT_TRACK, 1, 1,
                   0x87654321L, 0xE5E5);
  print("Flopfmt track %d side 1 = %d\r\n", FORMAT_TRACK, result);
  if (disk_is_rw) {
    assert_result("Flopfmt on the writable image is refused", result, ERROR);
  } else {
    assert_result("Flopfmt on the read-only disk is refused, write protected",
                  result, EWRPRO);
  }
  Floprd(buffer, 0L, DRIVE_A, 1, FORMAT_TRACK, 1, disk->spt);
  for (int n = 0; n < disk->spt; n++) {
    good += holds_signature(first + n, buffer + n * SECTOR);
  }
  assert_result("And the track is as it was", good, disk->spt);
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

/* A count of zero moves nothing and answers 0 at once, as TOS's Rwabs does.
   The buffer is filled first, so anything written into it shows. A driver
   that counts down with dbf after a subq turns zero into 65536 sectors and
   overruns memory, so this runs before anything is written to the disk.
   Floprd is another matter: Atari's TOS, 1.04 to 2.06, wraps as well, reads
   on to the end of the track - the controller cannot go further - and stops
   with -8, sector not found, while EmuTOS answers 0 and moves nothing. Either
   is TOS; running on past the track is not. */
static void test_count_of_zero(void) {
  long result;
  short xresult;
  memset(buffer, 0x5A, SECTOR * 2);
  result = Rwabs(0, buffer, 0, 1093, DRIVE_A);
  assert_result("Rwabs of no sectors answers 0", (int)result, 0);
  assert_result("And moves nothing", buffer_untouched(), TRUE);
  memset(buffer, 0x5A, SECTOR * 2);
  xresult = Floprd(buffer, 0L, DRIVE_A, 1, 60, 0, 0);
  print("Floprd of no sectors = %d\r\n", xresult);
  assert_result("Floprd of no sectors answers as TOS does, 0 or -8",
                xresult == 0 || xresult == -8, TRUE);
}

/* A read that cannot be served fails, and leaves the caller's buffer alone
   instead of handing it whatever was read before. The failure is made from
   the host before the run - swd.py app floppy_fail_read 1320 - and without
   that the read succeeds and there is nothing to see here: under Hatari, or
   on a run nobody armed. */
static void test_read_failure(void) {
  long result;
  int calls;
  Rwabs(0, buffer, 1, 1093, DRIVE_A); /* something else read just before */
  memset(buffer, 0x5A, SECTOR * 2);
  calls = critic_calls;
  result = Rwabs(0, buffer, 1, FAILING_SECTOR, DRIVE_A);
  if (result == 0) {
    print("[SKIP] No read failure made (swd.py app floppy_fail_read %d)\r\n",
          FAILING_SECTOR);
    return;
  }
  print("Rwabs of a sector that fails = %ld\r\n", result);
  assert_result("A read that fails is a read fault", (int)result, EREADF);
  assert_result("Through etv_critic, told the error and the drive",
                critic_calls - calls == 1 && critic_was_told(EREADF, DRIVE_A),
                TRUE);
  assert_result("And the buffer is left as it was", buffer_untouched(), TRUE);
  result = Rwabs(0, buffer, 1, FAILING_SECTOR, DRIVE_A);
  assert_result("Read again, it reads", (int)result, 0);
  assert_result("And it is that sector",
                holds_signature(FAILING_SECTOR, buffer), TRUE);
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
  assert_result("Dfree: the clusters on the disk", (int)info.b_total,
                disk->numcl);
  /* The files take 23 clusters, so 690 are free on the 720 KB disk. Atari's
     TOS, 1.00 to 2.06, says two fewer: its Dfree walks the FAT from entry 0
     instead of 2, counting the two reserved entries as used and never
     reaching the last two clusters. EmuTOS says 690. GEMDOS counts this from
     the FAT, so a driver serving wrong FAT sectors would show some other
     number, while either of these two is TOS being itself. */
  print("Dfree(A:) free clusters = %ld\r\n", (long)info.b_free);
  assert_result("Dfree: the free clusters TOS counts on this disk",
                info.b_free == disk->numcl - 25 || info.b_free == disk->numcl - 23,
                TRUE);
}

static void test_create_file(void) {
  int handle = Fcreate("A:\\NEWFILE.TMP", 0);
  long written;
  if (!disk_is_rw) {
    print("Fcreate on the read-only disk = %d\r\n", handle);
    assert_result("A file cannot be created on the read-only disk", handle,
                  EWRPRO);
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

/* On the one-sided-BPB disk every sector's signature names its place on the
   two-sided disk. TOS reads a record with the BPB's geometry - one side, so
   record r is track r / 9, side 0 - and the XBIOS reads the disk as it is. */
static int two_sided_lba(int track, int side, int sector) {
  return (track * 2 + side) * ONE_SIDED_BPB.spt + (sector - 1);
}

static int record_lba(int record) {
  return two_sided_lba(record / ONE_SIDED_BPB.spt, 0,
                       record % ONE_SIDED_BPB.spt + 1);
}

static void test_one_sided_bpb(void) {
  long result;
  short xresult;
  int good = 0;
  test_getbpb();
  result = Rwabs(0, buffer, 1, 600, DRIVE_A);
  assert_result("Rwabs record 600 is on side 0 of track 66",
                result == 0 && holds_signature(record_lba(600), buffer), TRUE);
  result = Rwabs(0, buffer, 9, 603, DRIVE_A);
  assert_result("Rwabs reads nine records across a track", (int)result, 0);
  for (int n = 0; n < 9; n++) {
    good += holds_signature(record_lba(603 + n), buffer + n * SECTOR);
  }
  assert_result("Each on side 0 of its track", good, 9);
  xresult = Floprd(buffer, 0L, DRIVE_A, 5, 60, 1, 1);
  assert_result("Floprd side 1 reads side 1 of the disk", xresult == 0 &&
                holds_signature(two_sided_lba(60, 1, 5), buffer), TRUE);
  xresult = Floprd(buffer, 0L, DRIVE_A, 5, 60, 0, 1);
  assert_result("Floprd side 0 reads where the records are", xresult == 0 &&
                holds_signature(two_sided_lba(60, 0, 5), buffer), TRUE);
  good = 0;
  xresult = Floprd(buffer, 0L, DRIVE_A, 1, 61, 1, ONE_SIDED_BPB.spt);
  for (int n = 0; n < ONE_SIDED_BPB.spt; n++) {
    good += holds_signature(two_sided_lba(61, 1, n + 1), buffer + n * SECTOR);
  }
  assert_result("Floprd a whole track of side 1", xresult == 0 &&
                good == ONE_SIDED_BPB.spt, TRUE);
}

/* A real change: the host cycles drive A to its next slot with SELECT while
   this waits, and GEMDOS must then read the other disk. The host is told by
   the read of CYCLE_SECTOR, which shows on the RP's console; drive A's slots
   are the two test disks, so MODE.TXT says the other mode afterwards. The
   last case, since it leaves the other disk in the drive. Nobody presses
   SELECT under Hatari, or on a run nobody set up for it: skipped then. */
static void test_slot_cycle(void) {
  char mode[8] = {0};
  int handle;
  int changed = FALSE;
  Rwabs(0, buffer, 1, CYCLE_SECTOR, DRIVE_A);
  print("Waiting for drive A to be cycled (SELECT)...\r\n");
  for (int i = 0; i < 50 * 20 && !changed; i++) {
    Vsync();
    changed = (Mediach(DRIVE_A) == MEDIA_CHANGED);
  }
  if (!changed) {
    print("[SKIP] Drive A was not cycled (swd.py select short when the "
          "console shows sector %d)\r\n", CYCLE_SECTOR);
    return;
  }
  assert_result("Cycling drive A makes Mediach say changed", changed, TRUE);
  handle = Fopen("A:\\MODE.TXT", 0);
  assert_result("GEMDOS opens MODE.TXT on the new disk", A_VALID_HANDLE(handle),
                TRUE);
  if (handle > 0) {
    Fread(handle, sizeof(mode) - 1, mode);
    Fclose(handle);
  }
  print("MODE.TXT after the cycle: %c%c, before: %s\r\n", mode[0], mode[1],
        disk_is_rw ? "RW" : "RO");
  assert_result("And reads the other disk",
                (mode[0] == 'R' && mode[1] == 'W') != disk_is_rw, TRUE);
  assert_result("The change is over", (int)Mediach(DRIVE_A) != MEDIA_CHANGED,
                TRUE);
}

int run_floppy_tests(void) {
  long old_critic = (long)Setexc(0x101, (long)critic_returns_error);

  print("=== Floppy: calls that are not floppy calls ===\r\n");
  test_mfpint_passes_through();

  print("=== Floppy: the test disk ===\r\n");
  if (!find_the_test_disk()) {
    /* nothing to test */
  } else if (disk == &ONE_SIDED_BPB) {
    print("=== Floppy: a one-sided BPB on a two-sided disk ===\r\n");
    test_one_sided_bpb();
    print("=== Floppy: GEMDOS ===\r\n");
    test_listing();
    test_read_files();
    test_program_on_the_floppy();
    test_dfree();
  } else {
    print("=== Floppy: BIOS ===\r\n");
    test_getbpb();
    test_mediach_after_boot();
    test_rwabs_boot_sector();
    test_rwabs_free_sector();
    test_rwabs_across_sides();
    test_rwabs_file_sector();
    test_forced_media_change();
    test_desktop_esc();

    print("=== Floppy: XBIOS ===\r\n");
    test_floprd_side(0);
    test_floprd_side(1);
    test_floprd_track();
    test_flopver();
    test_count_of_zero();
    test_read_failure();

    print("=== Floppy: GEMDOS ===\r\n");
    test_listing();
    test_read_files();
    test_program_on_the_floppy();
    test_what_the_loader_clears();
    test_dfree();

    print("=== Floppy: writing ===\r\n");
    test_rwabs_write();
    test_flopwr();
    test_flopfmt();
    test_boot_sector_write();
    test_create_file();
    test_leave_a_sector_written();

    print("=== Floppy: media change ===\r\n");
    test_slot_cycle();
  }

  Setexc(0x101, old_critic);
  return 0;
}
