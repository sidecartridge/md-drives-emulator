/**
 * File: floppy.c
 * Author: Diego Parrilla Santamaría
 * Date: August 2023-2025
 * Copyright: 2023-2025 - GOODDATA LABS SL
 * Description: Load floppy images files from SD card
 */

#include "floppy.h"

#include <assert.h>

static BPBData BPBDataA = {
    FLOPPY_SECTOR_SIZE, /* recsize     */
    2,                  /* clsiz       */
    1024,               /* clsizb      */
    8,                  /* rdlen       */
    6,                  /* fsiz        */
    7,                  /* fatrec      */
    21,                 /* datrec      */
    1015,               /* numcl       */
    0,                  /* bflags      */
    0,                  /* trackcnt    */
    0,                  /* sidecnt     */
    0,                  /* secpcyl     */
    0,                  /* secptrack   */
    {0, 0, 0},          /* reserved  */
    0                   /* diskNum */
};

static BPBData BPBDataB = {
    FLOPPY_SECTOR_SIZE, /* recsize     */
    2,                  /* clsiz       */
    1024,               /* clsizb      */
    8,                  /* rdlen       */
    6,                  /* fsiz        */
    7,                  /* fatrec      */
    21,                 /* datrec      */
    1015,               /* numcl       */
    0,                  /* bflags      */
    0,                  /* trackcnt    */
    0,                  /* sidecnt     */
    0,                  /* secpcyl     */
    0,                  /* secptrack   */
    {0, 0, 0},          /* reserved  */
    1                   /* diskNum */
};

static DiskVectors diskVectors = {
    .XBIOSTrapPayload = 0,
    .BIOSTrapPayload = 0,
};

// Store the full path for the floppy images in A and B drives
static char fullPathA[FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH] = {0};
static char fullPathB[FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH] = {0};

// Store the file objects for the floppy images in A and B drives
static FIL fobjA; /* File objects for drive A*/
static FIL fobjB; /* File objects for drive B */

// Different floppy drives
typedef enum { FLOPPY_DRIVE_A = 0, FLOPPY_DRIVE_B } FloppyDrive;

// Hold the different states of a virtual floppy disk
typedef enum {
  FLOPPY_DISK_UNMOUNTED = 0, /* Floppy disk is not mounted */
  FLOPPY_DISK_MOUNTED_RW,    /* Floppy disk is mounted in read-write mode */
  FLOPPY_DISK_MOUNTED_RO,    /* Floppy disk is mounted in read-only mode */
  FLOPPY_DISK_ERROR,         /* Floppy disk is in error state */
  FLOPPY_DISK_UNKNOWN        /* Floppy disk state is unknown */
} FloppyDiskState;

// Hold the state of the floppy disks
typedef struct {
  FloppyDiskState stateA; /* State of the floppy disk in drive A */
  FloppyDiskState stateB; /* State of the floppy disk in drive B */
  FIL fobjA;              /* File object for the floppy disk in drive A */
  FIL fobjB;              /* File object for the floppy disk in drive B */
  char fullPathA[FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH]; /* Full path for the
                                                    floppy disk in drive A */
  char fullPathB[FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH]; /* Full path for the
                                                    floppy disk in drive B */
} FloppyDiskStatus;

static FloppyDiskStatus floppyDiskStatus = {
    .stateA = FLOPPY_DISK_UNMOUNTED, /* Initial state of floppy disk A */
    .stateB = FLOPPY_DISK_UNMOUNTED, /* Initial state of floppy disk B */
    .fobjA = {0},                    /* File object for floppy disk A */
    .fobjB = {0},                    /* File object for floppy disk B */
    .fullPathA = {0},                /* Full path for floppy disk A */
    .fullPathB = {0}                 /* Full path for floppy disk B */
};

static FATFS filesys;
static char hdFolder[FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH] = {0};

static TransmissionProtocol lastProtocol;
static bool lastProtocolValid = false;
static uint32_t incrementalCmdCount = 0;

static uint32_t memorySharedAddress = 0;
static uint32_t memoryRandomTokenAddress = 0;
static uint32_t memoryRandomTokenSeedAddress = 0;
static uint32_t memoryFirmwareCode = 0;

#define FLOPPY_DRIVE_A_SLOT_MIN 1
#define FLOPPY_DRIVE_A_SLOT_MAX 10

static const char *const floppyDriveASlotKeys[FLOPPY_DRIVE_A_SLOT_MAX + 1] = {
    NULL,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A_2,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A_3,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A_4,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A_5,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A_6,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A_7,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A_8,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A_9,
    ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A_10,
};

static uint8_t currentDriveASlot = FLOPPY_DRIVE_A_SLOT_MIN;
static bool floppyMediaChangeClearPending[2] = {false, false};
static uint16_t floppyMediaChangeClearSector[2] = {0, 0};

static inline bool floppyStateIsMounted(FloppyDiskState state) {
  return state == FLOPPY_DISK_MOUNTED_RW || state == FLOPPY_DISK_MOUNTED_RO;
}

static inline FIL *floppyGetFileObject(FloppyDrive drive) {
  return (drive == FLOPPY_DRIVE_A) ? &fobjA : &fobjB;
}

static inline char *floppyGetFullPath(FloppyDrive drive) {
  return (drive == FLOPPY_DRIVE_A) ? fullPathA : fullPathB;
}

static inline FloppyDiskState *floppyGetStatePtr(FloppyDrive drive) {
  return (drive == FLOPPY_DRIVE_A) ? &floppyDiskStatus.stateA
                                   : &floppyDiskStatus.stateB;
}

static inline BPBData *floppyGetBPBData(FloppyDrive drive) {
  return (drive == FLOPPY_DRIVE_A) ? &BPBDataA : &BPBDataB;
}

static inline uint32_t floppyGetMediaChangedVarIndex(FloppyDrive drive) {
  return (drive == FLOPPY_DRIVE_A) ? FLOPPYEMUL_SVAR_MEDIA_CHANGED_A
                                   : FLOPPYEMUL_SVAR_MEDIA_CHANGED_B;
}

static inline void floppySetMediaChange(FloppyDrive drive, uint32_t status) {
  SET_SHARED_PRIVATE_VAR(floppyGetMediaChangedVarIndex(drive), status,
                         memorySharedAddress,
                         FLOPPYEMUL_SHARED_VARIABLES_OFFSET);
}

static inline uint16_t floppyGetRootDirStartSector(const BPBData *bpb) {
  if (bpb == NULL || bpb->datrec < bpb->rdlen) {
    return 0;
  }
  return (uint16_t)(bpb->datrec - bpb->rdlen);
}

static inline void floppyResetMediaChangeClearOnRootRead(FloppyDrive drive) {
  floppyMediaChangeClearPending[drive] = false;
  floppyMediaChangeClearSector[drive] = 0;
}

static inline void floppyArmMediaChangeClearOnRootRead(FloppyDrive drive) {
  floppyMediaChangeClearSector[drive] =
      floppyGetRootDirStartSector(floppyGetBPBData(drive));
  floppyMediaChangeClearPending[drive] = true;
}

static inline void floppyMaybeClearMediaChangeAfterRead(FloppyDrive drive,
                                                        uint16_t lSector) {
  if (!floppyMediaChangeClearPending[drive]) {
    return;
  }

  if (lSector != floppyMediaChangeClearSector[drive]) {
    return;
  }

  floppySetMediaChange(drive, FLOPPY_MEDIA_NOCHANGE);
  floppyResetMediaChangeClearOnRootRead(drive);
}

static inline bool floppyTransferSizeIsValid(uint16_t sSize) {
  if (sSize == 0) {
    DPRINTF("ERROR: Floppy transfer size must be greater than zero\n");
    return false;
  }

  if ((sSize & 1u) != 0u) {
    DPRINTF("ERROR: Floppy transfer size must be even for 16-bit swaps: %u\n",
            sSize);
    return false;
  }

  if ((uint32_t)sSize > FLOPPYEMUL_IMAGE_BUFFER_SIZE) {
    DPRINTF(
        "ERROR: Floppy transfer size %u exceeds shared image buffer size %lu\n",
        sSize, (unsigned long)FLOPPYEMUL_IMAGE_BUFFER_SIZE);
    return false;
  }

  return true;
}

static inline const char *floppyGetDriveASettingKey(uint8_t slotIndex) {
  if (slotIndex < FLOPPY_DRIVE_A_SLOT_MIN ||
      slotIndex > FLOPPY_DRIVE_A_SLOT_MAX) {
    return NULL;
  }
  return floppyDriveASlotKeys[slotIndex];
}

static bool floppyGetDriveAPathForSlot(uint8_t slotIndex, char *pathBuffer,
                                       size_t pathBufferSize) {
  if (pathBuffer == NULL || pathBufferSize == 0) {
    return false;
  }

  pathBuffer[0] = '\0';
  const char *key = floppyGetDriveASettingKey(slotIndex);
  if (key == NULL) {
    return false;
  }

  SettingsConfigEntry *entry = settings_find_entry(aconfig_getContext(), key);
  if (entry == NULL || entry->value[0] == '\0') {
    return false;
  }

  snprintf(pathBuffer, pathBufferSize, "%s", entry->value);
  return pathBuffer[0] != '\0';
}

static uint8_t floppyCountConfiguredDriveASlots(void) {
  uint8_t count = 0;
  char pathBuffer[FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH];

  for (uint8_t slot = FLOPPY_DRIVE_A_SLOT_MIN; slot <= FLOPPY_DRIVE_A_SLOT_MAX;
       ++slot) {
    if (floppyGetDriveAPathForSlot(slot, pathBuffer, sizeof(pathBuffer))) {
      count++;
    }
  }

  return count;
}

static uint8_t floppyFindNextConfiguredDriveASlot(uint8_t currentSlot) {
  char pathBuffer[FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH];

  for (uint8_t step = 1; step <= FLOPPY_DRIVE_A_SLOT_MAX; ++step) {
    uint8_t slot = (uint8_t)(((currentSlot - 1 + step) %
                              FLOPPY_DRIVE_A_SLOT_MAX) +
                             1);
    if (floppyGetDriveAPathForSlot(slot, pathBuffer, sizeof(pathBuffer))) {
      return slot;
    }
  }

  return 0;
}

/**
 * @brief Copies a file within the same folder to a new file with a specified
 * name.
 *
 * This function reads a specified file from a specified folder, and creates a
 * new file with a specified name in the same folder, copying the contents of
 * the original file to the new file. If a file with the specified new name
 * already exists, the behavior depends on the value of the overwrite_flag
 * argument: if true, the existing file is overwritten; if false, the function
 * returns an error code and the operation is canceled.
 *
 * @param folder The path of the folder containing the source file, as a
 * null-terminated string.
 * @param src_filename The name of the source file, as a null-terminated string.
 * @param dest_filename The name for the new file, as a null-terminated string.
 * @param overwrite_flag A flag indicating whether to overwrite the destination
 * file if it already exists: true to overwrite, false to cancel the operation.
 *
 * @return A result code of type FRESULT, indicating the result of the
 * operation:
 *         - FR_OK on success.
 *         - FR_EXISTS if the destination file exists and overwrite_flag is
 * false.
 *         - Other FatFS error codes for other error conditions.
 *
 * @note This function uses the FatFS library to perform file operations, and is
 * designed to work in environments where FatFS is available. It requires the
 * ff.h header file.
 *
 * Usage:
 * @code
 * FRESULT result = copy_file("/folder", "source.txt", "destination.txt", true);
 * // Overwrite if destination.txt exists
 * @endcode
 */
FRESULT copy_file(const char *folder, const char *src_filename,
                  const char *dest_filename, bool overwrite_flag) {
  if (folder == NULL || src_filename == NULL || dest_filename == NULL) {
    DPRINTF("ERROR: Invalid NULL parameter in copy_file\n");
    return FR_INVALID_PARAMETER;
  }

  FRESULT fr;    // FatFS function common result code
  FIL src_file;  // File objects
  FIL dest_file;
  UINT br, bw;        // File read/write count
  BYTE buffer[4096];  // File copy buffer

  char src_path[256];
  char dest_path[256];

  // Create full paths for source and destination files
  int src_path_len = snprintf(src_path, sizeof(src_path), "%s/%s", folder,
                              src_filename);
  if (src_path_len < 0 || (size_t)src_path_len >= sizeof(src_path)) {
    DPRINTF("ERROR: Source path is too long in copy_file\n");
    return FR_INVALID_NAME;
  }

  int dest_path_len = snprintf(dest_path, sizeof(dest_path), "%s/%s", folder,
                               dest_filename);
  if (dest_path_len < 0 || (size_t)dest_path_len >= sizeof(dest_path)) {
    DPRINTF("ERROR: Destination path is too long in copy_file\n");
    return FR_INVALID_NAME;
  }

  DPRINTF("Copying file '%s' to '%s'. Overwrite? %s\n", src_path, dest_path,
          overwrite_flag ? "YES" : "NO");

  // Check if the destination file already exists
  FILINFO fno;
  fr = f_stat(dest_path, &fno);
  if (fr == FR_OK && !overwrite_flag) {
    DPRINTF(
        "Destination file exists and overwrite_flag is false, canceling "
        "operation\n");
    return FR_EXIST;  // Destination file exists and overwrite_flag is
                      // false, cancel the operation
  }

  // Open the source file
  fr = f_open(&src_file, src_path, FA_READ);
  if (fr != FR_OK) {
    DPRINTF("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
    return fr;
  }

  // Create and open the destination file
  fr = f_open(&dest_file, dest_path, FA_CREATE_ALWAYS | FA_WRITE);
  if (fr != FR_OK) {
    DPRINTF("f_open error: %s (%d)\n", FRESULT_str(fr), fr);
    f_close(&src_file);  // Close the source file if it was opened successfully
    return fr;
  }

  // Copy the file
  do {
    fr = f_read(&src_file, buffer, sizeof buffer,
                &br);        // Read a chunk of source file
    if (fr != FR_OK) break;  // Break on error
    fr = f_write(&dest_file, buffer, br,
                 &bw);  // Write it to the destination file
    if (fr == FR_OK && bw != br) {
      DPRINTF("ERROR: Short write while copying file (%u/%u bytes)\n", bw, br);
      fr = FR_DISK_ERR;
      break;
    }
  } while (fr == FR_OK && br == sizeof buffer);

  // Close files
  f_close(&src_file);
  f_close(&dest_file);

  DPRINTF("File copied\n");
  return fr;  // Return the result
}

/**
 * @brief Creates the BIOS Parameter Block (BPB) from the first sector of the
 * floppy image.
 *
 * This function reads the first sector of the floppy image file and extracts
 * the necessary information to create the BPB. The BPB is a data structure used
 * by the file system to store information about the disk.
 *
 * @param fsrc Pointer to the file object representing the floppy image file.
 * @param bpb Pointer to the BPBData structure to be populated.
 * @return FRESULT The result of the operation. FR_OK if successful, an error
 * code otherwise.
 */
static inline uint16_t floppyReadLe16(const BYTE *buffer, size_t offset) {
  return (uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1] << 8);
}

static inline uint32_t floppyReadLe32(const BYTE *buffer, size_t offset) {
  return (uint32_t)buffer[offset] | ((uint32_t)buffer[offset + 1] << 8) |
         ((uint32_t)buffer[offset + 2] << 16) |
         ((uint32_t)buffer[offset + 3] << 24);
}

static FRESULT __not_in_flash_func(createBPB)(FIL *fsrc, BPBData *bpb) {
  BYTE buffer[FLOPPY_SECTOR_SIZE] = {0}; /* File copy buffer */
  unsigned int br = 0;                   /* File read/write count */
  FRESULT fr;

  DPRINTF("Creating BPB from first sector of floppy image\n");

  /* Set read/write pointer to logical sector position */
  fr = f_lseek(fsrc, 0);
  if (fr) {
    DPRINTF(
        "ERROR: Could not seek to the start of the first sector to create "
        "BPB\n");
    return fr;  // Check for error in reading
  }

  fr = f_read(fsrc, buffer, sizeof buffer,
              &br); /* Read a chunk of data from the source file */
  if (fr) {
    DPRINTF("ERROR: Could not read the first boot sector to create the BPBP\n");
    return fr;  // Check for error in reading
  }

  BPBData bpb_tmp = {0};  // Temporary BPBData structure

  uint16_t reservedSectors = floppyReadLe16(buffer, 14);
  uint16_t rootEntryCount = floppyReadLe16(buffer, 17);
  uint32_t totalSectors = floppyReadLe16(buffer, 19);
  uint8_t fatCount = buffer[16];

  bpb_tmp.recsize = floppyReadLe16(buffer, 11);  // Sector size in bytes
  bpb_tmp.clsiz = (uint16_t)buffer[13];          // Cluster size
  bpb_tmp.clsizb = bpb_tmp.clsiz * bpb_tmp.recsize;

  if (totalSectors == 0) {
    totalSectors = floppyReadLe32(buffer, 32);
  }

  if (bpb_tmp.recsize != 0) {
    bpb_tmp.rdlen =
        (uint16_t)(((uint32_t)rootEntryCount * 32U + bpb_tmp.recsize - 1U) /
                   bpb_tmp.recsize);
  }

  bpb_tmp.fsiz = floppyReadLe16(buffer, 22);  // FAT size in sectors
  bpb_tmp.fatrec = reservedSectors + bpb_tmp.fsiz;
  bpb_tmp.datrec =
      reservedSectors + ((uint16_t)fatCount * bpb_tmp.fsiz) + bpb_tmp.rdlen;

  if (bpb_tmp.clsiz != 0 && totalSectors >= bpb_tmp.datrec) {
    bpb_tmp.numcl = (uint16_t)((totalSectors - bpb_tmp.datrec) / bpb_tmp.clsiz);
  }

  bpb_tmp.bflags = 0;  // Magic flags
  bpb_tmp.sidecnt = floppyReadLe16(buffer, 26);
  bpb_tmp.secptrack = floppyReadLe16(buffer, 24);
  bpb_tmp.secpcyl = (uint16_t)(bpb_tmp.secptrack * bpb_tmp.sidecnt);
  bpb_tmp.trackcnt = 0;

  // Copy the temporary BPB data to the provided BPB structure
  *bpb = bpb_tmp;

  // Print the BPB data for debugging
  DPRINTF("BPB Data:\n");
  DPRINTF("  recsize: %u\n", bpb->recsize);
  DPRINTF("  clsiz: %u\n", bpb->clsiz);
  DPRINTF("  clsizb: %u\n", bpb->clsizb);
  DPRINTF("  rdlen: %u\n", bpb->rdlen);
  DPRINTF("  fsiz: %u\n", bpb->fsiz);
  DPRINTF("  fatrec: %u\n", bpb->fatrec);
  DPRINTF("  datrec: %u\n", bpb->datrec);
  DPRINTF("  numcl: %u\n", bpb->numcl);
  DPRINTF("  bflags: %u\n", bpb->bflags);
  DPRINTF("  trackcnt: %u\n", bpb->trackcnt);
  DPRINTF("  sidecnt: %u\n", bpb->sidecnt);
  DPRINTF("  secpcyl: %u\n", bpb->secpcyl);
  DPRINTF("  secptrack: %u\n", bpb->secptrack);
  DPRINTF("  reserved: %u %u %u\n", bpb->reserved[0], bpb->reserved[1],
          bpb->reserved[2]);
  DPRINTF("  disk_number: %u\n", bpb->disk_number);

  return FR_OK;
}

/**
 * @brief Check if the filename is a read-write floppy disk image file with the
 * ".rw" extension.
 *
 * This file contains functions related to file system operations in the Atari
 * ST sidecart emulation project. It provides a utility function to check if a
 * given filename is a read-write floppy disk image file.
 *
 * @param filename The name of the file to check.
 * @return True if the filename ends with ".rw", indicating a read-write floppy
 * disk image file. False otherwise.
 */
static inline bool isFloppyRW(const char *filename) {
  return (strlen(filename) >= 3 &&
          strcmp(filename + strlen(filename) - 3, ".rw") == 0);
}

static bool isTrue(const char *value) {
  if ((value == NULL) || (value[0] == '\0')) {
    return false;
  }
  if ((value[0] == 't') || (value[0] == 'T') || (value[0] == 'y') ||
      (value[0] == 'Y') || (value[0] == '1')) {
    return true;
  }
  return false;
}

/**
 * @brief Opens an file on the floppy drive
 *
 * This function opens the specified file on the floppy drive and performs
 * additional operations such as enabling fast seek mode, creating a CLMT
 * (Cluster Link Map Table), and retrieving the file size.
 *
 * @param fullpath The full path of the file to open.
 * @param floppyRW Specifies whether the file should be opened for both
 * reading and writing.
 * @param error Pointer to a boolean variable indicating whether an error
 * occurred during the operation.
 * @param fsrc Pointer to a FIL structure representing the opened file.
 * @return The result of the file open operation.
 */
static FRESULT __not_in_flash_func(floppyImgOpen)(const char *fullpath,
                                                  bool floppyRW, FIL *fsrc) {
  /* Open source file on the drive 0 */
  FRESULT fr = f_open(fsrc, fullpath, floppyRW ? FA_READ | FA_WRITE : FA_READ);
  if (fr) {
    DPRINTF("ERROR: Could not open file %s (%d)\r\n", fullpath, fr);
    return fr;
  }
  // Get file size
  uint32_t size = f_size(fsrc);
  fr = f_lseek(fsrc, size);
  if (fr) {
    DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath,
            fr);
    f_close(fsrc);
    return fr;
  }
  fr = f_lseek(fsrc, 0);
  if (fr) {
    DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n", fullpath,
            fr);
    f_close(fsrc);
    return fr;
  }
  DPRINTF("File size of %s: %i bytes\n", fullpath, size);

  return FR_OK;
}

/**
 * @brief Closes an opened file image on the floppy drive
 *
 * This function closes the specified file on the floppy drive and performs any
 * necessary cleanup operations.
 *
 * @param fsrc Pointer to a FIL structure representing the opened file.
 * @return The result of the file close operation.
 */
static FRESULT __not_in_flash_func(floppyImgClose)(FIL *fsrc) {
  FRESULT fr = f_close(fsrc);
  if (fr) {
    DPRINTF("ERROR: Could not close file (%d)\r\n", fr);
    return fr;
  }

  DPRINTF("File successfully closed.\n");
  return FR_OK;
}

static void printPayload(uint8_t *payloadShowBytesPtr) {
  if (!payloadShowBytesPtr) return;
  const int bytesPerLine = 8;
  char ascii[bytesPerLine + 1];
  // Display the first 256 bytes: 8 bytes per line with ASCII
  for (int i = 0; i < 256; i += bytesPerLine) {
    for (int j = 0; j < bytesPerLine; ++j) {
      uint8_t c = payloadShowBytesPtr[i + j];
      ascii[j] = (c >= 32 && c <= 126) ? c : '.';
    }
    ascii[bytesPerLine] = '\0';
    DPRINTF("%04x - %02x %02x %02x %02x %02x %02x %02x %02x | %s\n", i,
            payloadShowBytesPtr[i + 0], payloadShowBytesPtr[i + 1],
            payloadShowBytesPtr[i + 2], payloadShowBytesPtr[i + 3],
            payloadShowBytesPtr[i + 4], payloadShowBytesPtr[i + 5],
            payloadShowBytesPtr[i + 6], payloadShowBytesPtr[i + 7], ascii);
  }
}

static FRESULT __not_in_flash_func(floppyUnmountDrive)(FloppyDrive drive) {
  FIL *fobj = floppyGetFileObject(drive);
  FloppyDiskState *state = floppyGetStatePtr(drive);
  char *fullPath = floppyGetFullPath(drive);

  if (floppyStateIsMounted(*state)) {
    FRESULT fr = floppyImgClose(fobj);
    if (fr != FR_OK) {
      *state = FLOPPY_DISK_ERROR;
      return fr;
    }
  }

  memset(fobj, 0, sizeof(*fobj));
  fullPath[0] = '\0';
  *state = FLOPPY_DISK_UNMOUNTED;

  CLEAR_SHARED_PRIVATE_VAR_BIT(FLOPPYEMUL_SVAR_EMULATION_MODE,
                               (drive == FLOPPY_DRIVE_A) ? 0 : 1,
                               memorySharedAddress,
                               FLOPPYEMUL_SHARED_VARIABLES_OFFSET);
  return FR_OK;
}

static FRESULT __not_in_flash_func(floppyMountDrivePath)(FloppyDrive drive, const char *fname) {
  if (fname == NULL || fname[0] == '\0') {
    *floppyGetStatePtr(drive) = FLOPPY_DISK_ERROR;
    return FR_INVALID_NAME;
  }

  bool isRW = isFloppyRW(fname);
  FIL *fobj = floppyGetFileObject(drive);
  char *fullPath = floppyGetFullPath(drive);
  BPBData *bpb = floppyGetBPBData(drive);
  FloppyDiskState *state = floppyGetStatePtr(drive);
  floppyResetMediaChangeClearOnRootRead(drive);

  snprintf(fullPath, FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH, "%s", fname);
  DPRINTF("Mounting drive %c path: %s\n",
          (drive == FLOPPY_DRIVE_A) ? 'A' : 'B', fullPath);

  FRESULT err = floppyImgOpen(fullPath, isRW, fobj);
  if (err != FR_OK) {
    *state = FLOPPY_DISK_ERROR;
    return err;
  }

  FRESULT bpbFound = createBPB(fobj, bpb);
  if (bpbFound != FR_OK) {
    *state = FLOPPY_DISK_ERROR;
    floppyImgClose(fobj);
    memset(fobj, 0, sizeof(*fobj));
    fullPath[0] = '\0';
    return bpbFound;
  }

  if (drive == FLOPPY_DRIVE_A) {
    memcpy((void *)(memorySharedAddress + FLOPPYEMUL_BPB_DATA_A), bpb,
           sizeof(BPBDataA));
  } else {
    memcpy((void *)(memorySharedAddress + FLOPPYEMUL_BPB_DATA_B), bpb,
           sizeof(BPBDataB));
  }

  SET_SHARED_PRIVATE_VAR_BIT(FLOPPYEMUL_SVAR_EMULATION_MODE,
                             (drive == FLOPPY_DRIVE_A) ? 0 : 1,
                             memorySharedAddress,
                             FLOPPYEMUL_SHARED_VARIABLES_OFFSET);

  *state = isRW ? FLOPPY_DISK_MOUNTED_RW : FLOPPY_DISK_MOUNTED_RO;

  DPRINTF("Drive %c mounted successfully.\n",
          (drive == FLOPPY_DRIVE_A) ? 'A' : 'B');
  return FR_OK;
}

FRESULT __not_in_flash_func(vDriveOpen)(uint8_t drive) {
  char *fname = NULL;

  if (drive == FLOPPY_DRIVE_A) {
    SettingsConfigEntry *fnameDriveAParam = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A);
    if (fnameDriveAParam != NULL) {
      fname = fnameDriveAParam->value;
    }
  } else {
    SettingsConfigEntry *fnameDriveBParam = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_B);
    if (fnameDriveBParam != NULL) {
      fname = fnameDriveBParam->value;
    }
  }

  DPRINTF("Mounting drive %c with filename: %s\n",
          (drive == FLOPPY_DRIVE_A) ? 'A' : 'B', fname ? fname : "NULL");

  if (!fname || strlen(fname) == 0) {
    DPRINTF("Error: Missing filename.\n");
    if (drive == FLOPPY_DRIVE_A) {
      floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
    } else {
      floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
    }
    return FR_INVALID_NAME;  // Return error if no filename is provided
  }
  bool isRW = isFloppyRW(fname);
  DPRINTF("Floppy image is %s\n", isRW ? "read/write" : "read only");

  FRESULT err = FR_OK;  // Initialize error code
  if (drive == FLOPPY_DRIVE_A) {
    // Check if the floppy image is already mounted
    snprintf(fullPathA, sizeof(fullPathA), "%s", fname);
    DPRINTF("Full path for drive A: %s\n", fullPathA);
    err = floppyImgOpen(fullPathA, isFloppyRW(fullPathA), &fobjA);
  } else {
    // Check if the floppy image is already mounted
    snprintf(fullPathB, sizeof(fullPathB), "%s", fname);
    DPRINTF("Full path for drive B: %s\n", fullPathB);
    err = floppyImgOpen(fullPathB, isFloppyRW(fullPathB), &fobjB);
  }
  if (err != FR_OK) {
    DPRINTF("ERROR: Could not open floppy image. Error code: %d\n", err);
    if (drive == FLOPPY_DRIVE_A) {
      floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
    } else {
      floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
    }
    return err;  // Return error if the file could not be opened
  }

  FRESULT bpbFound;
  // Set the BPB of the floppy
  if (drive == FLOPPY_DRIVE_A) {
    DPRINTF("Floppy image %s opened successfully in Drive A\n", fullPathA);
    bpbFound = createBPB(&fobjA, &BPBDataA);
  } else {
    DPRINTF("Floppy image %s opened successfully in Drive B\n", fullPathB);
    bpbFound = createBPB(&fobjB, &BPBDataB);
  }
  if (bpbFound != FR_OK) {
    DPRINTF("ERROR: Could not create BPB for drive %c. Error code: %d\n",
            (drive == FLOPPY_DRIVE_A) ? 'A' : 'B', bpbFound);
    if (drive == FLOPPY_DRIVE_A) {
      floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
    } else {
      floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
    }
    floppyImgClose((drive == FLOPPY_DRIVE_A) ? &fobjA : &fobjB);
    return bpbFound;  // Return error if the BPB could not be created
  }

  // Emulation modes:
  // 0: No emulation (00)
  // 1: Emulation mode A, Physical A becomes B (01)
  // 2: Emulation mode B, Physical A no change (10)
  // 3: Emulation mode A, Emulation mode B (11)
  if (drive == FLOPPY_DRIVE_A) {
    BPBData *bPBPtr = &BPBDataA;
    memcpy((void *)(memorySharedAddress + FLOPPYEMUL_BPB_DATA_A), bPBPtr,
           sizeof(BPBDataA));
    SET_SHARED_PRIVATE_VAR_BIT(
        FLOPPYEMUL_SVAR_EMULATION_MODE, 0, memorySharedAddress,
        FLOPPYEMUL_SHARED_VARIABLES_OFFSET);  // Bit 0 = 1: Floppy
                                              // emulation A
    floppyDiskStatus.stateA =
        isRW ? FLOPPY_DISK_MOUNTED_RW
             : FLOPPY_DISK_MOUNTED_RO;  // Set mounted state for A
    DPRINTF("BPB for drive A created successfully.\n");
  } else {
    BPBData *bPBPtr = &BPBDataB;
    memcpy((void *)(memorySharedAddress + FLOPPYEMUL_BPB_DATA_B), bPBPtr,
           sizeof(BPBDataB));
    SET_SHARED_PRIVATE_VAR_BIT(
        FLOPPYEMUL_SVAR_EMULATION_MODE, 1, memorySharedAddress,
        FLOPPYEMUL_SHARED_VARIABLES_OFFSET);  // Bit 1 = 1: Floppy
                                              // emulation B
    floppyDiskStatus.stateB =
        isRW ? FLOPPY_DISK_MOUNTED_RW
             : FLOPPY_DISK_MOUNTED_RO;  // Set mounted state for B
    DPRINTF("BPB for drive B created successfully.\n");
  }
  return FR_OK;  // Return success if the BPB was created successfully
}

bool floppy_canCycleDriveA(void) {
  SettingsConfigEntry *floppyEnabledParam = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (floppyEnabledParam == NULL || !isTrue(floppyEnabledParam->value)) {
    return false;
  }

  return floppyCountConfiguredDriveASlots() >= 2;
}

FRESULT floppy_cycleDriveA(uint8_t *newSlotIndex) {
  if (!floppy_canCycleDriveA()) {
    return FR_INVALID_PARAMETER;
  }

  uint8_t nextSlot = floppyFindNextConfiguredDriveASlot(currentDriveASlot);
  if (nextSlot == 0 || nextSlot == currentDriveASlot) {
    return FR_INVALID_PARAMETER;
  }

  char nextPath[FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH] = {0};
  if (!floppyGetDriveAPathForSlot(nextSlot, nextPath, sizeof(nextPath))) {
    return FR_INVALID_NAME;
  }

  char previousPath[FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH] = {0};
  snprintf(previousPath, sizeof(previousPath), "%s", fullPathA);
  uint8_t previousSlot = currentDriveASlot;
  FloppyDiskState previousState = floppyDiskStatus.stateA;

  FRESULT closeResult = floppyUnmountDrive(FLOPPY_DRIVE_A);
  if (closeResult != FR_OK) {
    return closeResult;
  }

  FRESULT mountResult = floppyMountDrivePath(FLOPPY_DRIVE_A, nextPath);
  if (mountResult != FR_OK) {
    if (previousPath[0] != '\0' && previousState != FLOPPY_DISK_ERROR) {
      FRESULT reopenResult = floppyMountDrivePath(FLOPPY_DRIVE_A, previousPath);
      if (reopenResult == FR_OK) {
        currentDriveASlot = previousSlot;
      }
    }
    return mountResult;
  }

  currentDriveASlot = nextSlot;
  floppySetMediaChange(FLOPPY_DRIVE_A, FLOPPY_MEDIA_CHANGED);
  floppyArmMediaChangeClearOnRootRead(FLOPPY_DRIVE_A);
  if (newSlotIndex != NULL) {
    *newSlotIndex = currentDriveASlot;
  }
  return FR_OK;
}

void __not_in_flash_func(floppy_init)() {
  FRESULT fr; /* FatFs function common result code */

  srand(time(0));
  DPRINTF("Initializing Floppies...\n");  // Print always

  memorySharedAddress = (unsigned int)&__rom_in_ram_start__;
  if ((memorySharedAddress & 0x3u) != 0u) {
    DPRINTF("ERROR: FLOPPY shared memory base 0x%08lx is not 4-byte aligned\n",
            (unsigned long)memorySharedAddress);
    assert((memorySharedAddress & 0x3u) == 0u);
    return;
  }
  memoryRandomTokenAddress =
      memorySharedAddress + FLOPPYEMUL_RANDOM_TOKEN_OFFSET;
  memoryRandomTokenSeedAddress =
      memorySharedAddress + FLOPPYEMUL_RANDOM_TOKEN_SEED_OFFSET;

  memoryFirmwareCode = memorySharedAddress;

  bool floppyXBIOSenabled = false;
  SettingsConfigEntry *floppyXBIOSenabledParam = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_XBIOS_ENABLED);
  if (floppyXBIOSenabledParam != NULL) {
    floppyXBIOSenabled = isTrue(floppyXBIOSenabledParam->value);
  } else {
    floppyXBIOSenabled = false;
  }
  DPRINTF("Floppy XBIOS enabled: %s\n", floppyXBIOSenabled ? "Yes" : "No");

  bool floppyBootEnabled = false;
  SettingsConfigEntry *floppyBootEnabledParam = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_BOOT_ENABLED);
  if (floppyBootEnabledParam != NULL) {
    floppyBootEnabled = isTrue(floppyBootEnabledParam->value);
  } else {
    floppyBootEnabled = false;
  }
  DPRINTF("Floppy Boot enabled: %s\n", floppyBootEnabled ? "Yes" : "No");

  // Mount drive. For testing purposes.
  fr = f_mount(&filesys, "0:", 1);
  bool sdMounted = (fr == FR_OK);
  DPRINTF("SD card mounted: %s\n", sdMounted ? "OK" : "Failed");

  // Read the GEMDRIVE folder from the settings
  SettingsConfigEntry *floppyDriveFolder = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_FOLDER);
  if (floppyDriveFolder != NULL) {
    strncpy(hdFolder, floppyDriveFolder->value, sizeof(hdFolder) - 1);
    hdFolder[sizeof(hdFolder) - 1] = '\0';  // Ensure null-termination
    DPRINTF("Floppy Emulation folder: %s\n", hdFolder);
  } else {
    DPRINTF("Floppy Emulation folder not found. Using default.\n");
    strncpy(hdFolder, "/floppies", sizeof(hdFolder) - 1);
    hdFolder[sizeof(hdFolder) - 1] = '\0';  // Ensure null-termination
  }

  // Enabled?
  bool floppyEnabled = false;
  SettingsConfigEntry *floppyEnabledParam = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (floppyEnabledParam != NULL) {
    floppyEnabled = isTrue(floppyEnabledParam->value);
  } else {
    floppyEnabled = false;
  }
  DPRINTF("Floppy Emulation enabled: %s\n", floppyEnabled ? "Yes" : "No");
  if (!floppyEnabled) {
    DPRINTF("Floppy Emulation is disabled. Exiting initialization.\n");
    return;  // Exit if floppy emulation is not enabled
  }
  SET_SHARED_PRIVATE_VAR(
      FLOPPYEMUL_SVAR_XBIOS_TRAP_ENABLED, floppyXBIOSenabled ? 0xFFFFFFFF : 0,
      memorySharedAddress, FLOPPYEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(
      FLOPPYEMUL_SVAR_BOOT_ENABLED, floppyBootEnabled ? 0xFFFFFFFF : 0,
      memorySharedAddress, FLOPPYEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(FLOPPYEMUL_SVAR_ENABLED,
                         floppyEnabled ? 0xFFFFFFFF : 0, memorySharedAddress,
                         FLOPPYEMUL_SHARED_VARIABLES_OFFSET);
  floppySetMediaChange(FLOPPY_DRIVE_A, FLOPPY_MEDIA_NOCHANGE);
  floppySetMediaChange(FLOPPY_DRIVE_B, FLOPPY_MEDIA_NOCHANGE);
  floppyResetMediaChangeClearOnRootRead(FLOPPY_DRIVE_A);
  floppyResetMediaChangeClearOnRootRead(FLOPPY_DRIVE_B);
  currentDriveASlot = FLOPPY_DRIVE_A_SLOT_MIN;

  fr = vDriveOpen(FLOPPY_DRIVE_A);  // Open floppy drive A
  if (fr != FR_OK) {
    DPRINTF("ERROR: Could not open floppy drive A. Error code: %d\n", fr);
    floppyDiskStatus.stateA =
        FLOPPY_DISK_UNMOUNTED;  // Set unmounted state for A
    CLEAR_SHARED_PRIVATE_VAR_BIT(
        FLOPPYEMUL_SVAR_EMULATION_MODE, 0, memorySharedAddress,
        FLOPPYEMUL_SHARED_VARIABLES_OFFSET);  // Bit 0 = 1: Floppy
                                              // emulation A
  }
  fr = vDriveOpen(FLOPPY_DRIVE_B);  // Open floppy drive B
  if (fr != FR_OK) {
    DPRINTF("ERROR: Could not open floppy drive B. Error code: %d\n", fr);
    floppyDiskStatus.stateB =
        FLOPPY_DISK_UNMOUNTED;  // Set unmounted state for B
    CLEAR_SHARED_PRIVATE_VAR_BIT(
        FLOPPYEMUL_SVAR_EMULATION_MODE, 1, memorySharedAddress,
        FLOPPYEMUL_SHARED_VARIABLES_OFFSET);  // Bit 1 = 1: Floppy
                                              // emulation B
  }

  if (memoryRandomTokenAddress != 0) {
    uint32_t randomToken = 0;
    DPRINTF("Init random token: %08X\n", randomToken);
    // Set the random token in the shared memory
    TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenAddress, randomToken);
    // Init the random token seed in the shared memory for the next command
    uint32_t newRandomSeedToken = get_rand_32();
    DPRINTF("Set the new random token seed: %08X\n", newRandomSeedToken);
    TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenSeedAddress, newRandomSeedToken);
  }

  DPRINTF("Floppy Emulation initialized.\n");
  DPRINTF("Waiting for commands...\n");
}

// Invoke this function to process the commands from the active loop in the
// main function
void __not_in_flash_func(floppy_loop)(TransmissionProtocol *lastProtocol,
                                      uint16_t *payloadPtr) {
  // #if defined(_DEBUG) && (_DEBUG != 0)
  //     uint16_t *ptr = ((uint16_t *)(lastProtocol).payload);

  //     // Jump the random token
  //     TPROTO_NEXT32_PAYLOAD_PTR(ptr);

  //     // Read the payload parameters
  //     uint16_t payloadSizeTmp = 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= GEMDRIVE_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D3: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  //     payloadSizeTmp += 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= GEMDRIVE_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D4: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  //     payloadSizeTmp += 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= GEMDRIVE_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D5: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  //     payloadSizeTmp += 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= GEMDRIVE_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D6: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  // #endif

  // Only check the FLOPPYEMUL commands
  if (((lastProtocol->command_id >> 8) & 0xFF) != APP_FLOPPYEMUL) return;

  // Handle the command
  switch (lastProtocol->command_id) {
    case FLOPPYEMUL_DEBUG: {
      uint32_t d3 = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // Read d3
      DPRINTF("DEBUG D3: %x\n", d3);
      if (lastProtocol->payload_size > 8) {
        uint32_t d4 = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(
            payloadPtr);  // Move pointer and read d4
        DPRINTF("DEBUG D4: %x\n", d4);
      }
      if (lastProtocol->payload_size > 12) {
        uint32_t d5 = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(
            payloadPtr);  // Move pointer and read d5
        DPRINTF("DEBUG D5: %x\n", d5);
      }
      if (lastProtocol->payload_size > 16) {
        uint32_t d6 = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(
            payloadPtr);  // Move pointer and read d6
        DPRINTF("DEBUG D6: %x\n", d6);
      }
      if (lastProtocol->payload_size > 20) {
        TPROTO_NEXT32_PAYLOAD_PTR(
            payloadPtr);  // Move pointer to payload buffer
        uint8_t *payloadShowBytesPtr = (uint8_t *)payloadPtr;
        printPayload(payloadShowBytesPtr);
      }
      break;
    }
    case FLOPPYEMUL_RESET: {
      DPRINTF("Resetting Floppy Emulator\n");
      // Reset the shared variables
      // Set the continue to continue booting
      SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_START);
      break;
    }
    case FLOPPYEMUL_SET_SHARED_VAR: {
      uint32_t sharedVarIdx = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);
      uint32_t sharedVarValue = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
      SET_SHARED_VAR(sharedVarIdx, sharedVarValue, memorySharedAddress,
                     FLOPPYEMUL_SHARED_VARIABLES_OFFSET);
      break;
    }

    case FLOPPYEMUL_SAVE_VECTORS: {
      // Save the vectors needed for the floppy emulation
      DPRINTF("Saving vectors\n");
      diskVectors.XBIOSTrapPayload =
          TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // Read XBIOS trap payload
      WRITE_AND_SWAP_LONGWORD(memorySharedAddress, FLOPPYEMUL_OLD_XBIOS_TRAP,
                              diskVectors.XBIOSTrapPayload);
      DPRINTF("XBIOS Trap Payload: (%08X) = %08X\n", FLOPPYEMUL_OLD_XBIOS_TRAP,
              READ_AND_SWAP_LONGWORD(memorySharedAddress,
                                     FLOPPYEMUL_OLD_XBIOS_TRAP));
      break;
    }
    case FLOPPYEMUL_SAVE_BIOS_VECTOR: {
      // Save the BIOS vector needed for the floppy emulation
      DPRINTF("Saving BIOS vector\n");
      diskVectors.BIOSTrapPayload = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);
      uint32_t biosHandlerAddress =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // Read BIOS handler
      uint32_t newBiosVector =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // Read new BIOS

      // Calculate the address to write the old bios vector in the XBRA bios
      // handler
      uint32_t biosXBRAAddress = (biosHandlerAddress & 0xFFFF);
      // Write the new BIOS vector in the XBRA bios handler
      WRITE_AND_SWAP_LONGWORD(memoryFirmwareCode, biosXBRAAddress,
                              diskVectors.BIOSTrapPayload);
      DPRINTF("Bios vector: (%08X) = %08X\n", biosXBRAAddress,
              READ_AND_SWAP_LONGWORD(memoryFirmwareCode, biosXBRAAddress));
      break;
    }
    case FLOPPYEMUL_SAVE_HARDWARE: {
      uint32_t machine = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);  // d3 register
      uint32_t startFunc =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d4 register
      uint32_t endFunc =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d5 register
      // Self-modifying code to change the speed of the cpu and cache or not.
      // Not strictly needed, but can avoid bus errors Check if the hardware
      // type is 0x00010010 (Atari MegaSTe)
      if (machine != 0x00010010) {
        // write the 0x4E71 opcode (NOP) at the beginning of the function 8
        // times
        MEMSET16BIT(memoryFirmwareCode, (startFunc & 0xFFFF), 8,
                    0x4E71);  // NOP
        // write the 0x4E71 opcode (NOP) at the end of the function 2 times
        MEMSET16BIT(memoryFirmwareCode, (endFunc & 0xFFFF), 2, 0x4E71);  // NOP
        DPRINTF(
            "Floppy Emulator: Self-modifying code to remove MegaSTE specific "
            "code\n");
      } else {
        // Self-modifying code to change the speed of the cpu and cache or not.
        DPRINTF(
            "Floppy Emulator: MegaSTE specific code detected. Do not "
            "modify.\n");
      }
      break;
    }
    case FLOPPYEMUL_READ_SECTORS: {
      uint16_t sSize = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);  // d3.l register
      uint16_t lSector =
          TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);  // d3.h register
      uint16_t diskNum =
          TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);  // d4.l register
      DPRINTF("DISK READ %s (%d) - LSECTOR: %i / SSIZE: %i\n",
              diskNum == 0 ? "A:" : "B:", diskNum, lSector, sSize);
      if (!floppyTransferSizeIsValid(sSize)) {
        return;
      }

      if (diskNum == 0) {
        // Drive A. Check if the disk is mounted
        if (!((floppyDiskStatus.stateA == FLOPPY_DISK_MOUNTED_RW) ||
              (floppyDiskStatus.stateA == FLOPPY_DISK_MOUNTED_RO))) {
          DPRINTF("ERROR: Drive A is not mounted. Retrying mounting...\n");
          FRESULT ferr = vDriveOpen(FLOPPY_DRIVE_A);
          if (ferr != FR_OK) {
            DPRINTF("ERROR: Could not open drive A (%d)\r\n", ferr);
            floppyDiskStatus.stateA =
                FLOPPY_DISK_ERROR;  // Set error state for A
            return;                 // Return if the drive is not mounted
          }
          DPRINTF("Drive A mounted successfully.\n");
        }
      } else {
        // Drive B. Check if the disk is mounted
        if (!((floppyDiskStatus.stateB == FLOPPY_DISK_MOUNTED_RW) ||
              (floppyDiskStatus.stateB == FLOPPY_DISK_MOUNTED_RO))) {
          DPRINTF("ERROR: Drive B is not mounted. Retrying mounting...\n");
          FRESULT ferr = vDriveOpen(FLOPPY_DRIVE_B);
          if (ferr != FR_OK) {
            DPRINTF("ERROR: Could not open drive B (%d)\r\n", ferr);
            floppyDiskStatus.stateB =
                FLOPPY_DISK_ERROR;  // Set error state for B
            return;                 // Return if the drive is not mounted
          }
          DPRINTF("Drive B mounted successfully.\n");
        }
      }

      // If we are here, the disk is mounted and we should be able to read the
      // sectors
      FIL *fobjTmp = NULL;
      char *fullPathTmp = NULL;
      unsigned int bytesRead = {0};
      if (diskNum == 0) {
        fobjTmp = &fobjA;
        fullPathTmp = fullPathA;
      } else {
        fobjTmp = &fobjB;
        fullPathTmp = fullPathB;
      }
      /* Set read/write pointer to logical sector position */
      FRESULT ferr = f_lseek(fobjTmp, lSector * sSize);
      if (ferr) {
        DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\n",
                fullPathTmp, ferr);
        floppyImgClose(fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
        }
        return;  // Return if the seek operation failed
      }
      ferr = f_read(fobjTmp, (void *)(memorySharedAddress + FLOPPYEMUL_IMAGE),
                    sSize,
                    &bytesRead); /* Read a chunk of data from the source file */
      if (ferr) {
        DPRINTF("ERROR: Could not read file %s (%d). Closing file.\n",
                fullPathTmp, ferr);
        floppyImgClose(fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
        }
        return;  // Return if the read operation failed
      }
      if (bytesRead != sSize) {
        DPRINTF("ERROR: Short read from file %s (%u/%u bytes). Closing file.\n",
                fullPathTmp, bytesRead, sSize);
        floppyImgClose(fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;
        }
        return;
      }
      DPRINTF("Read sector %i of size %i bytes to memory address %08X\n",
              lSector, sSize, memorySharedAddress + FLOPPYEMUL_IMAGE);
      CHANGE_ENDIANESS_BLOCK16(memorySharedAddress + FLOPPYEMUL_IMAGE, sSize);
      floppyMaybeClearMediaChangeAfterRead(
          (diskNum == 0) ? FLOPPY_DRIVE_A : FLOPPY_DRIVE_B, lSector);
      break;
    }
    case FLOPPYEMUL_WRITE_SECTORS: {
      uint16_t sSize = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);  // d3.l register
      uint16_t lSector =
          TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);  // d3.h register
      uint16_t diskNum =
          TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);  // d4.l register
      uint32_t addrRemote =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d5 register
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);              // skip d5 register
      DPRINTF("DISK WRITE %s (%d) - LSECTOR: %i / SSIZE: %i at addr: %08X\n",
              diskNum == 0 ? "A:" : "B:", diskNum, lSector, sSize, addrRemote);
      if (!floppyTransferSizeIsValid(sSize)) {
        return;
      }

      if (diskNum == 0) {
        // Drive A. Check if the disk is mounted
        if (!((floppyDiskStatus.stateA == FLOPPY_DISK_MOUNTED_RW) ||
              (floppyDiskStatus.stateA == FLOPPY_DISK_MOUNTED_RO))) {
          DPRINTF("ERROR: Drive A is not mounted. Retrying mounting...\n");
          FRESULT ferr = vDriveOpen(FLOPPY_DRIVE_A);
          if (ferr != FR_OK) {
            DPRINTF("ERROR: Could not open drive A (%d)\r\n", ferr);
            floppyDiskStatus.stateA =
                FLOPPY_DISK_ERROR;  // Set error state for A
            return;                 // Return if the drive is not mounted
          }
          DPRINTF("Drive A mounted successfully.\n");
        }
      } else {
        // Drive B. Check if the disk is mounted
        if (!((floppyDiskStatus.stateB == FLOPPY_DISK_MOUNTED_RW) ||
              (floppyDiskStatus.stateB == FLOPPY_DISK_MOUNTED_RO))) {
          DPRINTF("ERROR: Drive B is not mounted. Retrying mounting...\n");
          FRESULT ferr = vDriveOpen(FLOPPY_DRIVE_B);
          if (ferr != FR_OK) {
            DPRINTF("ERROR: Could not open drive B (%d)\r\n", ferr);
            floppyDiskStatus.stateB =
                FLOPPY_DISK_ERROR;  // Set error state for B
            return;                 // Return if the drive is not mounted
          }
          DPRINTF("Drive B mounted successfully.\n");
        }
      }
      if (diskNum == 0) {
        // Drive A. Use fobjA
        if (floppyDiskStatus.stateA != FLOPPY_DISK_MOUNTED_RW) {
          DPRINTF("ERROR: Drive A is not mounted for writing.\n");
          return;  // Return if the drive is not mounted for writing
        }
      } else {
        // Drive B. Use fobjB
        if (floppyDiskStatus.stateB != FLOPPY_DISK_MOUNTED_RW) {
          DPRINTF("ERROR: Drive B is not mounted for writing.\n");
          return;  // Return if the drive is not mounted for writing
        }
      }

      // Copy shared memory to a local buffer
      uint16_t *target = payloadPtr;
      // Calculate the checksum of the buffer
      // Use a 16 bit checksum to minimize the number of loops
      uint16_t *target16 = (uint16_t *)target;

      // Change the endianness of the bytes read
      CHANGE_ENDIANESS_BLOCK16(target16, sSize);
      FIL *fobjTmp = NULL;
      char *fullPathTmp = NULL;
      unsigned int bytesRead = {0};
      if (diskNum == 0) {
        fobjTmp = &fobjA;
        fullPathTmp = fullPathA;
      } else {
        fobjTmp = &fobjB;
        fullPathTmp = fullPathB;
      }

      /* Set read/write pointer to logical sector position */
      FRESULT ferr = f_lseek(fobjTmp, lSector * sSize);
      if (ferr) {
        DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n",
                fullPathTmp, ferr);
        floppyImgClose(fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
        }
        return;  // Return if the read operation failed
      }
      ferr =
          f_write(fobjTmp, target16, sSize,
                  &bytesRead); /* Write a chunk of data from the source file */
      if (ferr) {
        DPRINTF("ERROR: Could not write file %s (%d). Closing file.\r\n",
                fullPathTmp, ferr);
        floppyImgClose(fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
        }
        return;  // Return if the read operation failed
      }
      if (bytesRead != sSize) {
        DPRINTF("ERROR: Short write to file %s (%u/%u bytes). Closing file.\n",
                fullPathTmp, bytesRead, sSize);
        floppyImgClose(fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;
        }
        return;
      }
      DPRINTF("Wrote sector %i of size %i bytes to file %s\n", lSector, sSize,
              fullPathTmp);
      break;
    }
  }
}
