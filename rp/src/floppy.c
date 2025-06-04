/**
 * File: floppy.c
 * Author: Diego Parrilla Santamaría
 * Date: August 2023-2025
 * Copyright: 2023-2025 - GOODDATA LABS SL
 * Description: Load floppy images files from SD card
 */

#include "floppy.h"

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

// Function to check if there is enough free disk space to create a file of size
// 'nDiskSize'
static FRESULT checkDiskSpace(const char *folder, uint32_t nDiskSize) {
  DWORD fre_clust, fre_sect, tot_sect;
  FATFS *fs;
  FRESULT fr;

  // Get free space
  fr = f_getfree(folder, &fre_clust, &fs);
  if (fr != FR_OK) {
    return fr;  // Return error code if operation is not successful
  }

  // Calculate the total number of free bytes
  uint64_t freeBytes = fre_clust * fs->csize * NUM_BYTES_PER_SECTOR;

  // Check if there is enough space
  if ((uint64_t)nDiskSize > freeBytes) {
    return FR_DENIED;  // Not enough space
  }
  return FR_OK;  // Enough space available
}

/**
 * Removes the .msa/.MSA (case-insensitive) extension from a filename.
 *
 * @param filename A modifiable C string with the file name.
 */
void floppy_removeMSAExtension(char *filename) {
  if (filename == NULL) return;

  size_t len = strlen(filename);
  if (len < 4) return;  // Not enough space for ".msa"

  char *ext = filename + len - 4;

  if (ext[0] == '.' && tolower((unsigned char)ext[1]) == 'm' &&
      tolower((unsigned char)ext[2]) == 's' &&
      tolower((unsigned char)ext[3]) == 'a') {
    *ext = '\0';  // Truncate the string
  }
}

/**
 * @brief Converts an MSA disk image file to an ST disk image file.
 *
 * This function takes a given MSA disk image file,
 * represented by `msaFilename` located within the `folder` directory, and
 * converts it into an ST (Atari ST disk image) file specified by `stFilename`.
 * If the `overwrite` is set to true, any existing file with the same
 * name as `stFilename` will be overwritten.
 *
 * @param folder The directory where the MSA file is located and the ST file
 * will be saved.
 * @param msaFilename The name of the MSA file to convert.
 * @param stFilename The name of the ST file to be created.
 * @param overwrite If true, any existing ST file will be overwritten.
 *
 * @return FRESULT A FatFS result code indicating the status of the operation.
 */
FRESULT floppy_MSA2ST(const char *folder, char *msaFilename, char *stFilename,
                      bool overwrite) {
  MSAHEADERSTRUCT msaHeader;
  uint32_t nBytesLeft = 0;
  uint8_t *pMSAImageBuffer, *pImageBuffer;
  uint8_t Byte, Data;
  uint16_t Track, Side, DataLength, NumBytesUnCompressed, RunLength;
  uint8_t *pBuffer = NULL;
  FRESULT fr;    // FatFS function common result code
  FIL src_file;  // File objects
  FIL dest_file;
  UINT br, bw;  // File read/write count
  BYTE *buffer_in = NULL;
  BYTE *buffer_out = NULL;

  // Check if the folder exists, if not, exit
  DPRINTF("Checking folder %s\n", folder);
  if (f_stat(folder, NULL) != FR_OK) {
    DPRINTF("Folder %s not found!\n", folder);
    return FR_NO_PATH;
  }

  char src_path[256];
  char dest_path[256];

  // Create full paths for source and destination files
  sprintf(src_path, "%s/%s", folder, msaFilename);
  sprintf(dest_path, "%s/%s", folder, stFilename);
  DPRINTF("SRC PATH: %s\n", src_path);
  DPRINTF("DEST PATH: %s\n", dest_path);

  // Check if the destination file already exists
  fr = f_stat(dest_path, NULL);
  if (fr == FR_OK && !overwrite) {
    DPRINTF(
        "Destination file exists and overwrite is false, canceling "
        "operation\n");
    return FR_EXIST;
  }

  // Check if the MSA source file exists in the SD card with FatFS
  if (f_open(&src_file, src_path, FA_READ) != FR_OK) {
    DPRINTF("MSA file not found!\n");
    return FR_NO_FILE;
  }
  // Calculate the size of the MSA file
  nBytesLeft = f_size(&src_file);

  // Check if the ST destination file exists in the SD card with FatFS
  if (f_open(&dest_file, dest_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    DPRINTF("Error creating destination ST file!\n");
    return FR_NO_FILE;
  }

  buffer_in = malloc(sizeof(MSAHEADERSTRUCT) * +sizeof(uint16_t));
  // Read only the memory needed to read the header AND the first track info (a
  // word)
  fr = f_read(&src_file, buffer_in, sizeof(MSAHEADERSTRUCT) + sizeof(uint16_t),
              &br);  // Read a chunk of source file
  if (fr != FR_OK) {
    DPRINTF("Error reading source file!\n");
    if (buffer_in != NULL) {
      free(buffer_in);
    }
    return FR_DISK_ERR;
  }

  memcpy(&msaHeader, buffer_in, sizeof(MSAHEADERSTRUCT));
  /* First swap 'header' words around to PC format - easier later on */
  msaHeader.ID = SWAP_WORD(msaHeader.ID);
  msaHeader.SectorsPerTrack = SWAP_WORD(msaHeader.SectorsPerTrack);
  msaHeader.Sides = SWAP_WORD(msaHeader.Sides);
  msaHeader.StartingTrack = SWAP_WORD(msaHeader.StartingTrack);
  msaHeader.EndingTrack = SWAP_WORD(msaHeader.EndingTrack);
  DPRINTF("MSA Header: ID: %x\n", msaHeader.ID);
  DPRINTF("MSA Header: SectorsPerTrack: %d\n", msaHeader.SectorsPerTrack);
  DPRINTF("MSA Header: Sides: %d\n", msaHeader.Sides);
  DPRINTF("MSA Header: StartingTrack: %d\n", msaHeader.StartingTrack);
  DPRINTF("MSA Header: EndingTrack: %d\n", msaHeader.EndingTrack);

  if (msaHeader.ID != 0x0E0F || msaHeader.EndingTrack > 86 ||
      msaHeader.StartingTrack > msaHeader.EndingTrack ||
      msaHeader.SectorsPerTrack > 56 || msaHeader.Sides > 1 ||
      nBytesLeft <= (long)sizeof(MSAHEADERSTRUCT)) {
    DPRINTF("MSA image has a bad header!\n");
    if (buffer_in != NULL) {
      free(buffer_in);
    }
    return FR_DISK_ERR;
  }

  if (checkDiskSpace(folder, NUM_BYTES_PER_SECTOR * msaHeader.SectorsPerTrack *
                                 (msaHeader.Sides + 1) *
                                 (msaHeader.EndingTrack -
                                  msaHeader.StartingTrack)) != FR_OK) {
    DPRINTF("Not enough space in the SD card!\n");
    if (buffer_in != NULL) {
      free(buffer_in);
    }
    return FR_DENIED;
  }

  nBytesLeft -= sizeof(MSAHEADERSTRUCT);
  // The length of the first track to read
  uint16_t currentTrackDataLength =
      SWAP_WORD((uint16_t)*(uint16_t *)(buffer_in + sizeof(MSAHEADERSTRUCT)));

  /* Uncompress to memory as '.ST' disk image - NOTE: assumes
   * NUM_BYTES_PER_SECTOR bytes per sector (use NUM_BYTES_PER_SECTOR define)!!!
   */
  for (Track = msaHeader.StartingTrack; Track <= msaHeader.EndingTrack;
       Track++) {
    for (Side = 0; Side < (msaHeader.Sides + 1); Side++) {
      uint16_t nBytesPerTrack =
          NUM_BYTES_PER_SECTOR * msaHeader.SectorsPerTrack;
      nBytesLeft -= sizeof(uint16_t);
      DPRINTF("Track: %d\n", Track);
      DPRINTF("Side: %d\n", Side);
      DPRINTF("Current Track Size: %d\n", currentTrackDataLength);
      DPRINTF("Bytes per track: %d\n", nBytesPerTrack);
      DPRINTF("Bytes left: %d\n", nBytesLeft);

      if (nBytesLeft < 0) goto out;

      // Reserve write buffer
      if (buffer_out != NULL) {
        free(buffer_out);
      }
      buffer_out = malloc(nBytesPerTrack);

      if (buffer_in != NULL) {
        free(buffer_in);
      }
      buffer_in = malloc(currentTrackDataLength + sizeof(uint16_t));

      BYTE *buffer_in_tmp = buffer_in;
      fr = f_read(&src_file, buffer_in_tmp,
                  currentTrackDataLength + sizeof(uint16_t),
                  &br);  // Read a chunk of source file
      if (fr != FR_OK) {
        DPRINTF("Error reading source file!\n");
        if (buffer_in != NULL) {
          free(buffer_in);
        }
        if (buffer_out != NULL) {
          free(buffer_out);
        }
        return FR_DISK_ERR;
      }

      // Check if it is not a compressed track
      if (currentTrackDataLength == nBytesPerTrack) {
        nBytesLeft -= currentTrackDataLength;
        if (nBytesLeft < 0) goto out;

        // No compression, read the full track and write it to the destination
        // file
        fr = f_write(&dest_file, buffer_in, nBytesPerTrack,
                     &bw);  // Write it to the destination file
        if (fr != FR_OK) {
          DPRINTF("Error writing destination file!\n");
          if (buffer_in != NULL) {
            free(buffer_in);
          }
          if (buffer_out != NULL) {
            free(buffer_out);
          }
          return FR_DISK_ERR;
        }
        buffer_in_tmp += currentTrackDataLength;
      } else {
        // Compressed track, uncompress it
        NumBytesUnCompressed = 0;
        BYTE *buffer_out_tmp = buffer_out;
        while (NumBytesUnCompressed < nBytesPerTrack) {
          if (--nBytesLeft < 0) goto out;
          Byte = *buffer_in_tmp++;
          if (Byte != 0xE5) /* Compressed header? */
          {
            *buffer_out_tmp++ = Byte; /* No, just copy byte */
            NumBytesUnCompressed++;
          } else {
            nBytesLeft -= 3;
            if (nBytesLeft < 0) goto out;
            Data = *buffer_in_tmp++; /* Byte to copy */
            RunLength = (uint16_t)(buffer_in_tmp[1] | buffer_in_tmp[0] << 8);
            /* Limit length to size of track, incorrect images may overflow */
            if (RunLength + NumBytesUnCompressed > nBytesPerTrack) {
              DPRINTF(
                  "MSA_UnCompress: Illegal run length -> corrupted disk "
                  "image?\n");
              RunLength = nBytesPerTrack - NumBytesUnCompressed;
            }
            buffer_in_tmp += sizeof(uint16_t);
            for (uint16_t i = 0; i < RunLength; i++) {
              *buffer_out_tmp++ = Data; /* Copy byte */
            }
            NumBytesUnCompressed += RunLength;
          }
        }
        // No compression, read the full track and write it to the destination
        // file
        fr = f_write(&dest_file, buffer_out, nBytesPerTrack,
                     &bw);  // Write it to the destination file
        if (fr != FR_OK) {
          DPRINTF("Error writing destination file!\n");
          if (buffer_in != NULL) {
            free(buffer_in);
          }
          if (buffer_out != NULL) {
            free(buffer_out);
          }
          return FR_DISK_ERR;
        }
      }
      if (nBytesLeft > 0) {
        currentTrackDataLength =
            (uint16_t)(buffer_in_tmp[1] | buffer_in_tmp[0] << 8);
      }
    }
  }
out:
  if (nBytesLeft < 0) {
    DPRINTF("MSA error: Premature end of file!\n");
  }

  // Close files
  f_close(&src_file);
  f_close(&dest_file);

  if (buffer_in != NULL) {
    free(buffer_in);
  }
  if (buffer_out != NULL) {
    free(buffer_out);
  }

  return FR_OK;
}

/**
 * Write a short integer to a given address in little endian byte order.
 * This function is primarily used to write 16-bit values into the boot sector
 * of a disk image which requires values to be in little endian format.
 *
 * @param addr Pointer to the address where the short integer should be written.
 * @param val The 16-bit value to be written in little endian byte order.
 */
static inline void writeShortLE(void *addr, uint16_t val) {
  /* Cast the address to a uint8_t pointer and write the value in little endian
   * byte order. */
  uint8_t *p = (uint8_t *)addr;

  p[0] = (uint8_t)val;         // Write the low byte.
  p[1] = (uint8_t)(val >> 8);  // Write the high byte shifted down.
}

/**
 * Create .ST image according to 'Tracks,Sector,Sides' and save
 *
            40 track SS   40 track DS   80 track SS   80 track DS
    0- 1   Branch instruction to boot program if executable
    2- 7   'Loader'
    8-10   24-bit serial number
    11-12   BPS    512           512           512           512
    13      SPC     1             2             2             2
    14-15   RES     1             1             1             1
    16      FAT     2             2             2             2
    17-18   DIR     64           112           112           112
    19-20   SEC    360           720           720          1440
    21      MEDIA  $FC           $FD           $F8           $F9  (isn't used by
 ST-BIOS) 22-23   SPF     2             2             5             5 24-25 SPT
 9             9             9             9 26-27   SIDE    1             2 1 2
    28-29   HID     0             0             0             0
    510-511 CHECKSUM
 */

/**
 * Create a blank Atari ST disk image file.
 *
 * This function creates a blank Atari ST formatted disk image with the
 * specified parameters. It can also set the volume label and allows
 * for the option to overwrite an existing file.
 *
 * @param folder The directory in which to create the disk image.
 * @param stFilename The name of the disk image file to create.
 * @param nTracks Number of tracks on the disk.
 * @param nSectors Number of sectors per track.
 * @param nSides Number of disk sides.
 * @param volLabel Optional volume label for the disk; pass NULL for no label.
 * @param overwrite If true, an existing file with the same name will be
 * overwritten.
 *
 * @return FR_OK if the operation is successful, otherwise an error code.
 */
FRESULT floppy_createSTImage(const char *folder, char *stFilename, int nTracks,
                             int nSectors, int nSides, const char *volLavel,
                             bool overwrite) {
  uint8_t *pDiskHeader;
  uint32_t nDiskSize;
  uint32_t nHeaderSize;
  uint32_t nDiskSizeNoHeader;
  uint16_t SPC, nDir, MediaByte, SPF;
  uint16_t drive;
  uint16_t LabelSize;
  uint8_t *pDirStart;

  FRESULT fr;  // FatFS function common result code
  FIL dest_file;
  UINT bw;  // File write count
  char dest_path[256];
  BYTE zeroBuff[NUM_BYTES_PER_SECTOR];  // Temporary buffer to hold zeros

  /* Calculate size of disk image */
  nDiskSize = nTracks * nSectors * nSides * NUM_BYTES_PER_SECTOR;

  // Calculate size of the header information
  nHeaderSize = 2 * (1 + SPF_MAX) * NUM_BYTES_PER_SECTOR;

  // Calculate the size of the disk without the header
  nDiskSizeNoHeader = nDiskSize - nHeaderSize;

  // Check if the folder exists, if not, exit
  DPRINTF("Checking folder %s\n", folder);
  if (f_stat(folder, NULL) != FR_OK) {
    DPRINTF("Folder %s not found!\n", folder);
    return FR_NO_PATH;
  }

  if (checkDiskSpace(folder, nDiskSize) != FR_OK) {
    DPRINTF("Not enough space in the SD card!\n");
    return FR_DENIED;
  }

  // Create the full path for the destination file
  sprintf(dest_path, "%s/%s", folder, stFilename);
  DPRINTF("DEST PATH: %s\n", dest_path);

  // Check if the destination file already exists
  fr = f_stat(dest_path, NULL);
  if (fr == FR_OK && !overwrite) {
    DPRINTF(
        "Destination file exists and overwrite is false, canceling "
        "operation\n");
    return FR_EXIST;
  }

  /* HD/ED disks are all double sided */
  if (nSectors >= 18) nSides = 2;

  // Allocate space ONLY for the header. We don't have enough space in the
  // RP2040
  pDiskHeader = malloc(nHeaderSize);
  if (pDiskHeader == NULL) {
    DPRINTF("Error while creating blank disk image");
    return FR_DISK_ERR;
  }
  memset(pDiskHeader, 0, nHeaderSize); /* Clear buffer */

  /* Fill in boot-sector */
  pDiskHeader[0] = 0xE9;            /* Needed for MS-DOS compatibility */
  memset(pDiskHeader + 2, 0x4e, 6); /* 2-7 'Loader' */

  writeShortLE(pDiskHeader + 8, rand()); /* 8-10 24-bit serial number */
  pDiskHeader[10] = rand();

  writeShortLE(pDiskHeader + 11, NUM_BYTES_PER_SECTOR); /* 11-12 BPS */

  if ((nTracks == 40) && (nSides == 1))
    SPC = 1;
  else
    SPC = 2;
  pDiskHeader[13] = SPC; /* 13 SPC */

  writeShortLE(pDiskHeader + 14, 1); /* 14-15 RES */
  pDiskHeader[16] = 2;               /* 16 FAT */

  if (SPC == 1)
    nDir = 64;
  else if (nSectors < 18)
    nDir = 112;
  else
    nDir = 224;
  writeShortLE(pDiskHeader + 17, nDir); /* 17-18 DIR */

  writeShortLE(pDiskHeader + 19, nTracks * nSectors * nSides); /* 19-20 SEC */

  if (nSectors >= 18)
    MediaByte = 0xF0;
  else {
    if (nTracks <= 42)
      MediaByte = 0xFC;
    else
      MediaByte = 0xF8;
    if (nSides == 2) MediaByte |= 0x01;
  }
  pDiskHeader[21] = MediaByte; /* 21 MEDIA */

  if (nSectors >= 18)
    SPF = SPF_MAX;
  else if (nTracks >= 80)
    SPF = 5;
  else
    SPF = 2;
  writeShortLE(pDiskHeader + 22, SPF); /* 22-23 SPF */

  writeShortLE(pDiskHeader + 24, nSectors); /* 24-25 SPT */
  writeShortLE(pDiskHeader + 26, nSides);   /* 26-27 SIDE */
  writeShortLE(pDiskHeader + 28, 0);        /* 28-29 HID */

  /* Set correct media bytes in the 1st FAT: */
  pDiskHeader[NUM_BYTES_PER_SECTOR] = MediaByte;
  pDiskHeader[NUM_BYTES_PER_SECTOR + 1] =
      pDiskHeader[NUM_BYTES_PER_SECTOR + 2] = 0xFF;
  /* Set correct media bytes in the 2nd FAT: */
  pDiskHeader[NUM_BYTES_PER_SECTOR + SPF * NUM_BYTES_PER_SECTOR] = MediaByte;
  pDiskHeader[(NUM_BYTES_PER_SECTOR + 1) + SPF * NUM_BYTES_PER_SECTOR] =
      pDiskHeader[(2 + NUM_BYTES_PER_SECTOR) + SPF * NUM_BYTES_PER_SECTOR] =
          0xFF;

  /* Set volume label if needed (in 1st entry of the directory) */
  if (volLavel != NULL) {
    /* Set 1st dir entry as 'volume label' */
    pDirStart = pDiskHeader + (1 + SPF * 2) * NUM_BYTES_PER_SECTOR;
    memset(pDirStart, ' ', 8 + 3);
    LabelSize = strlen(volLavel);
    if (LabelSize <= 8 + 3)
      memcpy(pDirStart, volLavel, LabelSize);
    else
      memcpy(pDirStart, volLavel, 8 + 3);

    pDirStart[8 + 3] = GEMDOS_FILE_ATTRIB_VOLUME_LABEL;
  }

  // Always create a new file. We assume we have already checked if the file
  // exists and the overwrite
  if (f_open(&dest_file, dest_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    DPRINTF("Error creating the destination ST file!\n");
    free(pDiskHeader);
    return FR_NO_FILE;
  }

  // Write the header to the destination file
  fr = f_write(&dest_file, pDiskHeader, nHeaderSize,
               &bw);  // Write it to the destination file
  if (fr != FR_OK) {
    DPRINTF("Error writing the header to the destination ST file!\n");
    free(pDiskHeader);
    return FR_DISK_ERR;
  }

  // Write zeros to the rest of the file

  memset(zeroBuff, 0, sizeof(zeroBuff));  // Set the buffer to zeros
  while (nDiskSizeNoHeader > 0) {
    UINT toWrite = sizeof(zeroBuff);
    if (nDiskSizeNoHeader < toWrite)
      toWrite = nDiskSizeNoHeader;  // Write only as much as needed

    fr = f_write(&dest_file, zeroBuff, toWrite, &bw);  // Write zeros to file
    if (fr != FR_OK || bw < toWrite) {
      fr = (fr == FR_OK)
               ? FR_DISK_ERR
               : fr;  // If no error during write, set the error to disk error
      free(pDiskHeader);
      return fr;
    }
    nDiskSizeNoHeader -= bw;  // Decrement the remaining size
  }

  // Close the file
  f_close(&dest_file);

  // Free buffer
  free(pDiskHeader);
  return FR_OK;
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
  FRESULT fr;    // FatFS function common result code
  FIL src_file;  // File objects
  FIL dest_file;
  UINT br, bw;        // File read/write count
  BYTE buffer[4096];  // File copy buffer

  char src_path[256];
  char dest_path[256];

  // Create full paths for source and destination files
  sprintf(src_path, "%s/%s", folder, src_filename);
  sprintf(dest_path, "%s/%s", folder, dest_filename);

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
    f_close(fsrc);
    return fr;  // Check for error in reading
  }

  fr = f_read(fsrc, buffer, sizeof buffer,
              &br); /* Read a chunk of data from the source file */
  if (fr) {
    DPRINTF("ERROR: Could not read the first boot sector to create the BPBP\n");
    f_close(fsrc);
    return fr;  // Check for error in reading
  }

  BPBData bpb_tmp;  // Temporary BPBData structure

  bpb_tmp.recsize = ((uint16_t)buffer[11]) |
                    ((uint16_t)buffer[12] << 8);     // Sector size in bytes
  bpb_tmp.clsiz = (uint16_t)buffer[13];              // Cluster size
  bpb_tmp.clsizb = bpb_tmp.clsiz * bpb_tmp.recsize;  // Cluster size in bytes
  bpb_tmp.rdlen =
      ((uint16_t)buffer[17] >> 4) |
      ((uint16_t)buffer[18] << 8);      // Root directory length in sectors
  bpb_tmp.fsiz = (uint16_t)buffer[22];  // FAT size in sectors
  bpb_tmp.fatrec = bpb_tmp.fsiz + 1;    // Sector number of second FAT
  bpb_tmp.datrec = bpb_tmp.rdlen + bpb_tmp.fatrec +
                   bpb_tmp.fsiz;  // Sector number of first data cluster
  bpb_tmp.numcl =
      ((((uint16_t)buffer[20] << 8) | (uint16_t)buffer[19]) - bpb_tmp.datrec) /
      bpb_tmp.clsiz;     // Number of data clusters on the disk
  bpb_tmp.bflags = 0;    // Magic flags
  bpb_tmp.trackcnt = 0;  // Track count
  bpb_tmp.sidecnt = (uint16_t)buffer[26];  // Side count
  bpb_tmp.secpcyl =
      (uint16_t)(buffer[24] * bpb_tmp.sidecnt);  // Sectors per cylinder
  bpb_tmp.secptrack = (uint16_t)buffer[24];      // Sectors per track
  bpb_tmp.reserved[0] = 0;                       // Reserved
  bpb_tmp.reserved[1] = 0;                       // Reserved
  bpb_tmp.reserved[2] = 0;                       // Reserved
                            //    bpb_tmp.diskNum = disknumber;

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

FRESULT __not_in_flash_func(vDriveOpen)(uint8_t drive) {
  char *fname = NULL;

  if (drive == FLOPPY_DRIVE_A) {
    SettingsConfigEntry *fnameDriveAParam = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A);
    if (fnameDriveAParam != NULL) {
      // Copy the drive name from the settings
      fname = fnameDriveAParam->value;
    }
  } else {
    SettingsConfigEntry *fnameDriveBParam = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_B);
    if (fnameDriveBParam != NULL) {
      // Copy the drive name from the settings
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

void __not_in_flash_func(floppy_init)() {
  FRESULT fr; /* FatFs function common result code */

  srand(time(0));
  DPRINTF("Initializing Floppies...\n");  // Print always

  memorySharedAddress = (unsigned int)&__rom_in_ram_start__;
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
  if (!(lastProtocol->command_id & (APP_FLOPPYEMUL << 8))) return;

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
      FIL fobjTmp = {0};
      char *fullPathTmp = NULL;
      unsigned int bytesRead = {0};
      if (diskNum == 0) {
        fobjTmp = fobjA;
        fullPathTmp = fullPathA;
      } else {
        fobjTmp = fobjB;
        fullPathTmp = fullPathB;
      }
      /* Set read/write pointer to logical sector position */
      FRESULT ferr = f_lseek(&fobjTmp, lSector * sSize);
      if (ferr) {
        DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\n",
                fullPathTmp, ferr);
        floppyImgClose(&fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
        }
        return;  // Return if the seek operation failed
      }
      ferr = f_read(&fobjTmp, (void *)(memorySharedAddress + FLOPPYEMUL_IMAGE),
                    sSize,
                    &bytesRead); /* Read a chunk of data from the source file */
      if (ferr) {
        DPRINTF("ERROR: Could not read file %s (%d). Closing file.\n",
                fullPathTmp, ferr);
        floppyImgClose(&fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
        }
        return;  // Return if the read operation failed
      }
      DPRINTF("Read sector %i of size %i bytes to memory address %08X\n",
              lSector, sSize, memorySharedAddress + FLOPPYEMUL_IMAGE);
      CHANGE_ENDIANESS_BLOCK16(memorySharedAddress + FLOPPYEMUL_IMAGE, sSize);
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
      FIL fobjTmp = {0};
      char *fullPathTmp = NULL;
      unsigned int bytesRead = {0};
      if (diskNum == 0) {
        fobjTmp = fobjA;
        fullPathTmp = fullPathA;
      } else {
        fobjTmp = fobjB;
        fullPathTmp = fullPathB;
      }

      /* Set read/write pointer to logical sector position */
      FRESULT ferr = f_lseek(&fobjTmp, lSector * sSize);
      if (ferr) {
        DPRINTF("ERROR: Could not seek file %s (%d). Closing file.\r\n",
                fullPathTmp, ferr);
        floppyImgClose(&fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
        }
        return;  // Return if the read operation failed
      }
      ferr =
          f_write(&fobjTmp, target16, sSize,
                  &bytesRead); /* Write a chunk of data from the source file */
      if (ferr) {
        DPRINTF("ERROR: Could not read file %s (%d). Closing file.\r\n",
                fullPathTmp, ferr);
        floppyImgClose(&fobjTmp);
        if (diskNum == 0) {
          floppyDiskStatus.stateA = FLOPPY_DISK_ERROR;  // Set error state for A
        } else {
          floppyDiskStatus.stateB = FLOPPY_DISK_ERROR;  // Set error state for B
        }
        return;  // Return if the read operation failed
      }
      DPRINTF("Wrote sector %i of size %i bytes to file %s\n", lSector, sSize,
              fullPathTmp);
      break;
    }
  }
}