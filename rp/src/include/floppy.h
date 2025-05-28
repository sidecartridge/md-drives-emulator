/**
 * File: floppy.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023-2025
 * Copyright: 2023-2025 - GOODDATA LABS SL
 * Description: Header file for the Floppy emulator C program.
 */

#ifndef FLOPPY_H
#define FLOPPY_H

#include <hardware/watchdog.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "../../build/romemul.pio.h"
#include "aconfig.h"
#include "blink.h"
#include "chandler.h"
#include "constants.h"
#include "debug.h"
#include "display.h"
#include "f_util.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/rtc.h"
#include "hardware/structs/bus_ctrl.h"
#include "memfunc.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "sdcard.h"
#include "time.h"
#include "tprotocol.h"

#define FLOPPYEMUL_FATFS_MAX_FOLDER_LENGTH \
  192  // Max length of the folder name in FATFS

#define FLOPPYEMUL_RANDOM_TOKEN_OFFSET (CHANDLER_RANDOM_TOKEN_OFFSET)
#define FLOPPYEMUL_RANDOM_TOKEN_SEED_OFFSET (CHANDLER_RANDOM_TOKEN_SEED_OFFSET)

#define FLOPPYEMUL_SHARED_VARIABLES_OFFSET (CHANDLER_SHARED_VARIABLES_OFFSET)

// Index for the common shared variables
#define FLOPPYEMUL_HARDWARE_TYPE (CHANDLER_HARDWARE_TYPE)
#define FLOPPYEMUL_SVERSION (CHANDLER_SVERSION)
#define FLOPPYEMUL_BUFFER_TYPE (CHANDLER_BUFFER_TYPE)

// 0x9A08 ├────────────────────────────────────────────┤
//        │ FLOPPYEMUL_SVAR_XBIOS_TRAP_ENABLED         │
//        │   size 4 bytes                             │
// 0x9A0C ├────────────────────────────────────────────┤
//        │ FLOPPYEMUL_SVAR_BOOT_ENABLED               │
//        │   size 4 bytes                             │
// 0x9A10 ├────────────────────────────────────────────┤
//        │ FLOPPYEMUL_SVAR_EMULATION_MODE             │
//        │   size 4 bytes                             │
// 0x9A14 ├────────────────────────────────────────────┤
//        │ Empty space...                             │
//        ...
// 0x9A28 ├────────────────────────────────────────────┤
//        │ FLOPPYEMUL_VARIABLES_OFFSET.               │
//        ├────────────────────────────────────────────┤

// We need a gap of 6KB after the random token and seed
#define FLOPPYEMUL_GAP_SIZE 0x1800  // 6KB gap
#define FLOPPYEMUL_SHARED_VARIABLE_SIZE \
  (FLOPPYEMUL_GAP_SIZE / 4)  // 6KB gap divided by 4 bytes per longword
#define FLOPPYEMUL_SHARED_VARIABLES_COUNT \
  (32)  // Size of the shared variables of the shared functions

// Now the index for the shared variables of the program
#define FLOPPYEMUL_SVAR_XBIOS_TRAP_ENABLED (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 0)
#define FLOPPYEMUL_SVAR_BOOT_ENABLED (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 1)
#define FLOPPYEMUL_SVAR_EMULATION_MODE (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 2)
#define FLOPPYEMUL_SVAR_ENABLED \
  (FLOPPYEMUL_SHARED_VARIABLE_SIZE + 3)  // enabled flag

// We will need 32 bytes extra for the variables of the floppy emulator
#define FLOPPYEMUL_VARIABLES_OFFSET                       \
  (FLOPPYEMUL_RANDOM_TOKEN_OFFSET + FLOPPYEMUL_GAP_SIZE + \
   FLOPPYEMUL_SHARED_VARIABLES_COUNT)  // random_token + gap + shared
                                       // variables
                                       // size

#define FLOPPYEMUL_OLD_XBIOS_TRAP \
  (FLOPPYEMUL_VARIABLES_OFFSET)  // The old XBIOS trap is at the start of the
                                 // variables

#define FLOPPYEMUL_BPB_DATA_A (FLOPPYEMUL_OLD_XBIOS_TRAP + 4)
#define FLOPPYEMUL_BPB_TRACKCNT_A \
  (FLOPPYEMUL_BPB_DATA_A + 18)  // BPB_data + 18 bytes
#define FLOPPYEMUL_BPB_SIDECNT_A \
  (FLOPPYEMUL_BPB_TRACKCNT_A + 2)  // trackcnt + 2 bytes
#define FLOPPYEMUL_SECPCYL_A \
  (FLOPPYEMUL_BPB_SIDECNT_A + 2)                           // sidecnt + 2 bytes
#define FLOPPYEMUL_SECPTRACK_A (FLOPPYEMUL_SECPCYL_A + 2)  // secpcyl + 2 bytes
#define FLOPPYEMUL_DISK_NUMBER_A (FLOPPYEMUL_SECPTRACK_A + 8)  // BTB + 2 bytes

#define FLOPPYEMUL_BPB_DATA_B \
  (FLOPPYEMUL_DISK_NUMBER_A + 2)  // FLOPPYEMUL_DISK_NUMBER_A + 2 bytes
#define FLOPPYEMUL_BPB_TRACKCNT_B \
  (FLOPPYEMUL_BPB_DATA_B + 18)  // BPB_data + 18 bytes
#define FLOPPYEMUL_BPB_SIDECNT_B \
  (FLOPPYEMUL_BPB_TRACKCNT_B + 2)  // trackcnt + 2 bytes
#define FLOPPYEMUL_SECPCYL_B \
  (FLOPPYEMUL_BPB_SIDECNT_B + 2)                           // sidecnt + 2 bytes
#define FLOPPYEMUL_SECPTRACK_B (FLOPPYEMUL_SECPCYL_B + 2)  // secpcyl + 2 bytes
#define FLOPPYEMUL_DISK_NUMBER_B (FLOPPYEMUL_SECPTRACK_B + 8)  // BTB + 2 bytes

// The buffer for the read of the images
#define FLOPPYEMUL_IMAGE (FLOPPYEMUL_VARIABLES_OFFSET + 256)

// Media type changed flags
#define MED_NOCHANGE 0
#define MED_UNKNOWN 1
#define MED_CHANGED 2

// BPB fields
#define BPB_RECSIZE 0
#define BPB_CLSIZ 1
#define BPB_CLSIZB 2
#define BPB_RDLEN 3
#define BPB_FSIZ 4
#define BPB_FATREC 5
#define BPB_DATREC 6
#define BPB_NUMCL 7
#define BPB_BFLAGS 8
#define BPB_TRACKCNT 9
#define SIDE_COUNT 10
#define SEC_CYL 11
#define SEC_TRACK 12
#define DISK_NUMBER 16
#define DISK_NUMBER_A 0
#define DISK_NUMBER_B 1

// APP_ROMEMUL commands
#define APP_FLOPPYEMUL 0x02  // The floppy emulator app

// APP_FLOPPYEMUL commands
#define FLOPPYEMUL_SAVE_VECTORS \
  (APP_FLOPPYEMUL << 8 | 0)  // Save the vectors of the floppy emulator
#define FLOPPYEMUL_READ_SECTORS \
  (APP_FLOPPYEMUL << 8 | 1)  // Read sectors from the floppy emulator
#define FLOPPYEMUL_WRITE_SECTORS \
  (APP_FLOPPYEMUL << 8 | 2)  // Write sectors to the floppy emulator
#define FLOPPYEMUL_PING (APP_FLOPPYEMUL << 8 | 3)  // Ping the floppy emulator
#define FLOPPYEMUL_SAVE_HARDWARE \
  (APP_FLOPPYEMUL << 8 | 4)  // Save the hardware of the floppy emulator
#define FLOPPYEMUL_SET_SHARED_VAR \
  (APP_FLOPPYEMUL << 8 | 5)                         // Set a shared variable
#define FLOPPYEMUL_RESET (APP_FLOPPYEMUL << 8 | 6)  // Reset the floppy emulator
#define FLOPPYEMUL_SAVE_BIOS_VECTOR \
  (APP_FLOPPYEMUL << 8 | 7)  // Save the BIOS vector of the floppy emulator
#define FLOPPYEMUL_SHOW_VECTOR_CALL \
  (APP_FLOPPYEMUL << 8 | 11)  // Show the vector call of the floppy emulator
#define FLOPPYEMUL_DEBUG \
  (APP_FLOPPYEMUL << 8 | 12)  // Show the debug info of the floppy emulator

#define FLOPPY_SECTOR_SIZE 512  // Default sector size for floppy disks
#define GEMDOS_FILE_ATTRIB_VOLUME_LABEL 8
#define SPF_MAX 9

typedef struct {
  uint16_t ID;              /* Word : ID marker, should be $0E0F */
  uint16_t SectorsPerTrack; /* Word : Sectors per track */
  uint16_t Sides; /* Word : Sides (0 or 1; add 1 to this to get correct number
                     of sides) */
  uint16_t StartingTrack; /* Word : Starting track (0-based) */
  uint16_t EndingTrack;   /* Word : Ending track (0-based) */
} MSAHEADERSTRUCT;

typedef struct {
  uint16_t recsize;     /* 0: Sector size in bytes                */
  uint16_t clsiz;       /* 1: Cluster size in sectors             */
  uint16_t clsizb;      /* 2: Cluster size in bytes               */
  uint16_t rdlen;       /* 3: Root Directory length in sectors    */
  uint16_t fsiz;        /* 4: FAT size in sectors                 */
  uint16_t fatrec;      /* 5: Sector number of second FAT         */
  uint16_t datrec;      /* 6: Sector number of first data cluster */
  uint16_t numcl;       /* 7: Number of data clusters on the disk */
  uint16_t bflags;      /* 8: Magic flags                         */
  uint16_t trackcnt;    /* 9: Track count                         */
  uint16_t sidecnt;     /* 10: Side count                         */
  uint16_t secpcyl;     /* 11: Sectors per cylinder               */
  uint16_t secptrack;   /* 12: Sectors per track                  */
  uint16_t reserved[3]; /* 13-15: Reserved                        */
  uint16_t disk_number; /* 16: Disk number                        */
} BPBData;

typedef struct {
  uint32_t BIOSTrapPayload;
  uint32_t XBIOSTrapPayload;
} DiskVectors;

typedef void (*IRQInterceptionCallback)();

extern int read_addr_rom_dma_channel;
extern int lookup_data_rom_dma_channel;

// Interrupt handler callback for DMA completion
void __not_in_flash_func(floppy_dma_irq_handler_lookup_callback)(void);
void __not_in_flash_func(floppy_dma_irq_handler_lookup)(void);

// Function Prototypes
void __not_in_flash_func(floppy_init)();
void __not_in_flash_func(floppy_loop)();
void floppy_removeMSAExtension(char *filename);
FRESULT floppy_createSTImage(const char *folder, char *stFilename, int nTracks,
                             int nSectors, int nSides, const char *volLavel,
                             bool overwrite);
FRESULT floppy_MSA2ST(const char *folder, char *msaFilename, char *stFilename,
                      bool overwrite);

#endif  // FLOPPY_H
