/**
 * File: gemdrive.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2023 - April 2025
 * Copyright: 2023 2025 - GOODDATA LABS SL
 * Description: Header file for the GEMDRIVE C program.
 */

#ifndef GEMDRIVE_H
#define GEMDRIVE_H

#include <hardware/watchdog.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "../../build/romemul.pio.h"
#include "aconfig.h"
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
#define ADDRESS_HIGH_BIT 0x8000          // High bit of the address
#define GEMDRIVE_PARAMETERS_MAX_SIZE 20  // Max size of the parameters for debug

#define DEFAULT_FOPEN_READ_BUFFER_SIZE 4096
#define DEFAULT_FWRITE_BUFFER_SIZE 1024
#define FIRST_FILE_DESCRIPTOR 16384
#define PRG_STRUCT_SIZE \
  28  // Size of the GEMDOS structure in the executable header file (PRG)
#define SHARED_VARIABLES_MAXSIZE 32
#define SHARED_VARIABLES_SIZE 7
#define DTA_SIZE_ON_ST 44

#define GEMDRIVE_MAX_FOLDER_LENGTH \
  128  // Max length of the folder name in GEMDOS

#define GEMDRIVE_FATFS_MAX_FOLDER_LENGTH \
  192  // Max length of the folder name in FATFS

// 0x8248 ├────────────────────────────────────────────┤
//        │ GEMDRIVE_SHARED_VARIABLE_FIRST_FILE_DES    │
//        │   size 4 bytes                             │
// 0x824C ├────────────────────────────────────────────┤
//        │ GEMDRIVE_SHARED_VARIABLE_DRIVE_LETTER      │
//        │   size 4 bytes                             │
// 0x8250 ├────────────────────────────────────────────┤
//        │ GEMDRIVE_SHARED_VARIABLE_DRIVE_NUMBER      │
//        │   size 4 bytes                             │
// 0x8254 ├────────────────────────────────────────────┤
//        │ GEMDRIVE_SHARED_VARIABLE_PEXEC_RESTORE     │
//        │   size 4 bytes                             │
// 0x8258 ├────────────────────────────────────────────┤
//        │ GEMDRIVE_SHARED_VARIABLE_FAKE_FLOPPY       │
//        │   size 4 bytes                             │
// 0x825C ├────────────────────────────────────────────┤
//        │ Empty space...                             │
//        ...
// 0x8300 ├────────────────────────────────────────────┤
//        │ GEMDRIVE_VARIABLES_OFFSET.                 │
//        ├────────────────────────────────────────────┤

// For GEMDRIVE we need a large memory space for the shared variables
// Starting from 0x8200 we have 32 KB of memory avaiable for the shared
// variables.
#define GEMDRIVE_RANDOM_TOKEN_OFFSET (CHANDLER_RANDOM_TOKEN_OFFSET)
#define GEMDRIVE_RANDOM_TOKEN_SEED_OFFSET (CHANDLER_RANDOM_TOKEN_SEED_OFFSET)

// The shared variables are located after the
// GEMDRIVE_SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE longwords offset
#define GEMDRIVE_SHARED_VARIABLES_OFFSET (CHANDLER_SHARED_VARIABLES_OFFSET)

// Index for the common shared variables
#define GEMDRIVE_HARDWARE_TYPE (CHANDLER_HARDWARE_TYPE)
#define GEMDRIVE_SVERSION (CHANDLER_SVERSION)
#define GEMDRIVE_BUFFER_TYPE (CHANDLER_BUFFER_TYPE)

// Size of the shared variables of the shared functions
#define GEMDRIVE_SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE \
  16  // Leave a gap for the shared variables of the shared functions

// Now the index for the shared variables of the program
#define GEMDRIVE_SHARED_VARIABLE_FIRST_FILE_DESCRIPTOR \
  (GEMDRIVE_SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 0)
#define GEMDRIVE_SHARED_VARIABLE_DRIVE_LETTER \
  (GEMDRIVE_SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 1)
#define GEMDRIVE_SHARED_VARIABLE_DRIVE_NUMBER \
  (GEMDRIVE_SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 2)
#define GEMDRIVE_SHARED_VARIABLE_PEXEC_RESTORE \
  (GEMDRIVE_SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 3)
#define GEMDRIVE_SHARED_VARIABLE_FAKE_FLOPPY \
  (GEMDRIVE_SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 4)
#define GEMDRIVE_SHARED_VARIABLE_ENABLED \
  (GEMDRIVE_SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE + 5)  // enabled flag

#define GEMDRIVE_VARIABLES_OFFSET \
  (GEMDRIVE_RANDOM_TOKEN_OFFSET + \
   0x100)  // The variables used by GEMDRIVE start at 0x8100

#define GEMDRIVE_REENTRY_TRAP (GEMDRIVE_VARIABLES_OFFSET)  // variables start

#define GEMDRIVE_OLD_XBIOS_TRAP \
  (GEMDRIVE_REENTRY_TRAP + 4)  // random_token_seed + 4 bytes
#define GEMDRIVE_DEFAULT_PATH \
  (GEMDRIVE_OLD_XBIOS_TRAP + 4)  // old xbios trap + 4 bytes
#define GEMDRIVE_DTA_F_FOUND \
  (GEMDRIVE_DEFAULT_PATH + 128)  // default path + 128 bytes
#define GEMDRIVE_DTA_TRANSFER (GEMDRIVE_DTA_F_FOUND + 4)  // dta found + 4
#define GEMDRIVE_DTA_EXIST \
  (GEMDRIVE_DTA_TRANSFER + \
   DTA_SIZE_ON_ST)  // dta transfer + DTA_SIZE_ON_ST bytes
#define GEMDRIVE_DTA_RELEASE (GEMDRIVE_DTA_EXIST + 4)  // dta exist + 4 bytes
#define GEMDRIVE_SET_DPATH_STATUS \
  (GEMDRIVE_DTA_RELEASE + 4)  // dta release + 4 bytes
#define GEMDRIVE_FOPEN_HANDLE \
  (GEMDRIVE_SET_DPATH_STATUS + 4)  // set dpath status + 4 bytes

#define GEMDRIVE_READ_BYTES \
  (GEMDRIVE_FOPEN_HANDLE + 4)                         // fopen handle + 4 bytes.
#define GEMDRIVE_READ_BUFF (GEMDRIVE_READ_BYTES + 4)  // read bytes + 4 bytes
#define GEMDRIVE_WRITE_BYTES \
  (GEMDRIVE_READ_BUFF +      \
   DEFAULT_FOPEN_READ_BUFFER_SIZE)  //  GEMDRIVE_READ_BUFFER +
                                    // DEFAULT_FOPEN_READ_BUFFER_SIZE bytes
#define GEMDRIVE_WRITE_CHK \
  (GEMDRIVE_WRITE_BYTES + 4)  //  GEMDRIVE_WRITE_BYTES + 4 bytes
#define GEMDRIVE_WRITE_CONFIRM_STATUS \
  (GEMDRIVE_WRITE_CHK + 4)  // write check + 4 bytes

#define GEMDRIVE_FCLOSE_STATUS \
  (GEMDRIVE_WRITE_CONFIRM_STATUS + 4)  // read buff + 4 bytes
#define GEMDRIVE_DCREATE_STATUS \
  (GEMDRIVE_FCLOSE_STATUS + 4)  // fclose status + 2 bytes + 2 bytes padding
#define GEMDRIVE_DDELETE_STATUS \
  (GEMDRIVE_DCREATE_STATUS + 4)  // dcreate status + 2 bytes + 2 bytes padding
#define GEMDRIVE_EXEC_HEADER \
  (GEMDRIVE_DDELETE_STATUS + 4)  // ddelete status + 2 bytes + 2 bytes padding.
                                 // Must be aligned to 4 bytes/32 bits
#define GEMDRIVE_FCREATE_HANDLE \
  (GEMDRIVE_EXEC_HEADER + 32)  // exec header + 32 bytes
#define GEMDRIVE_FDELETE_STATUS \
  (GEMDRIVE_FCREATE_HANDLE + 4)  // fcreate handle + 4 bytes
#define GEMDRIVE_FSEEK_STATUS \
  (GEMDRIVE_FDELETE_STATUS + 4)  // fdelete status + 4 bytes
#define GEMDRIVE_FATTRIB_STATUS (GEMDRIVE_FSEEK_STATUS + 4)  // fseek status + 4
#define GEMDRIVE_FRENAME_STATUS \
  (GEMDRIVE_FATTRIB_STATUS + 4)  // fattrib status + 4 bytes
#define GEMDRIVE_FDATETIME_DATE \
  (GEMDRIVE_FRENAME_STATUS + 4)  // frename status + 4 bytes
#define GEMDRIVE_FDATETIME_TIME \
  (GEMDRIVE_FDATETIME_DATE + 4)  // fdatetime date + 4
#define GEMDRIVE_FDATETIME_STATUS \
  (GEMDRIVE_FDATETIME_TIME + 4)  // fdatetime time + 4 bytes
#define GEMDRIVE_DFREE_STATUS \
  (GEMDRIVE_FDATETIME_STATUS + 4)  // fdatetime status + 4 bytes
#define GEMDRIVE_DFREE_STRUCT \
  (GEMDRIVE_DFREE_STATUS + 4)  // dfree status + 4 bytes

#define GEMDRIVE_PEXEC_MODE \
  (GEMDRIVE_DFREE_STRUCT + 32)  // dfree struct + 32 bytes
#define GEMDRIVE_PEXEC_STACK_ADDR \
  (GEMDRIVE_PEXEC_MODE + 4)  // pexec mode + 4 bytes
#define GEMDRIVE_PEXEC_FNAME \
  (GEMDRIVE_PEXEC_STACK_ADDR + 4)  // pexec stack addr + 4 bytes
#define GEMDRIVE_PEXEC_CMDLINE \
  (GEMDRIVE_PEXEC_FNAME + 4)  // pexec fname + 4 bytes
#define GEMDRIVE_PEXEC_ENVSTR \
  (GEMDRIVE_PEXEC_CMDLINE + 4)  // pexec cmd line + 4 bytes

#define GEMDRIVE_EXEC_PD (GEMDRIVE_PEXEC_ENVSTR + 4)  // pexec envstr + 4 bytes

// The commands code is the combinatino of two bytes:
// - The most significant byte is the application code. All the commands of an
// app should have the same code
// - The least significant byte is the command code. Each command of an app
// should have a different code
#define APP_GEMDRVEMUL 0x04  // The GEMDRIVE app.

// APP_GEMDRVEMUL commands
#define GEMDRVEMUL_RESET \
  (APP_GEMDRVEMUL << 8 | 0)  // Reset the GEMDRIVE emulator
#define GEMDRVEMUL_SAVE_VECTORS \
  (APP_GEMDRVEMUL << 8 | 1)  // Save the vectors of the GEMDRIVE emulator
#define GEMDRVEMUL_SHOW_VECTOR_CALL \
  (APP_GEMDRVEMUL << 8 | 2)  // Show the vector call of the GEMDRIVE emulator
#define GEMDRVEMUL_REENTRY_LOCK \
  (APP_GEMDRVEMUL << 8 | 3)  // Lock the reentry of the GEMDRIVE emulator
#define GEMDRVEMUL_REENTRY_UNLOCK \
  (APP_GEMDRVEMUL << 8 | 4)  // Unlock the reentry of the GEMDRIVE emulator
#define GEMDRVEMUL_CANCEL \
  (APP_GEMDRVEMUL << 8 | 5)  // Cancel the current execution
#define GEMDRVEMUL_RTC_START (APP_GEMDRVEMUL << 8 | 6)  // Start RTC emulator
#define GEMDRVEMUL_RTC_STOP (APP_GEMDRVEMUL << 8 | 7)   // Stop RTC emulator
#define GEMDRVEMUL_NETWORK_START \
  (APP_GEMDRVEMUL << 8 | 8)  // Start the network emulator
#define GEMDRVEMUL_NETWORK_STOP \
  (APP_GEMDRVEMUL << 8 | 9)  // Stop the network emulator

#define GEMDRVEMUL_SAVE_XBIOS_VECTOR \
  (APP_GEMDRVEMUL << 8 | 10)  // Save the XBIOS vector in the Sidecart
#define GEMDRVEMUL_REENTRY_XBIOS_LOCK \
  (APP_GEMDRVEMUL << 8 | 11)  // Enable reentry XBIOS calls
#define GEMDRVEMUL_REENTRY_XBIOS_UNLOCK \
  (APP_GEMDRVEMUL << 8 | 12)  // Disable reentry XBIOS calls

#define GEMDRVEMUL_DGETDRV_CALL \
  (APP_GEMDRVEMUL << 8 | 0x19)  // Show the Dgetdrv call
#define GEMDRVEMUL_FSETDTA_CALL \
  (APP_GEMDRVEMUL << 8 | 0x1A)  // Show the Fsetdta call
#define GEMDRVEMUL_DFREE_CALL \
  (APP_GEMDRVEMUL << 8 | 0x36)  // Show the Dfree call
#define GEMDRVEMUL_DCREATE_CALL \
  (APP_GEMDRVEMUL << 8 | 0x39)  // Show the Dcreate call
#define GEMDRVEMUL_DDELETE_CALL \
  (APP_GEMDRVEMUL << 8 | 0x3A)  // Show the Ddelete call
#define GEMDRVEMUL_DSETPATH_CALL \
  (APP_GEMDRVEMUL << 8 | 0x3B)  // Show the Dgetpath call
#define GEMDRVEMUL_FCREATE_CALL \
  (APP_GEMDRVEMUL << 8 | 0x3C)  // Show the Fcreate call
#define GEMDRVEMUL_FOPEN_CALL \
  (APP_GEMDRVEMUL << 8 | 0x3D)  // Show the Fopen call
#define GEMDRVEMUL_FCLOSE_CALL \
  (APP_GEMDRVEMUL << 8 | 0x3E)  // Show the Fclose call
#define GEMDRVEMUL_FDELETE_CALL \
  (APP_GEMDRVEMUL << 8 | 0x41)  // Show the Fdelete call
#define GEMDRVEMUL_FSEEK_CALL \
  (APP_GEMDRVEMUL << 8 | 0x42)  // Show the Fseek call
#define GEMDRVEMUL_FATTRIB_CALL \
  (APP_GEMDRVEMUL << 8 | 0x43)  // Show the Fattrib call
#define GEMDRVEMUL_DGETPATH_CALL \
  (APP_GEMDRVEMUL << 8 | 0x47)  // Show the Dgetpath call
#define GEMDRVEMUL_FSFIRST_CALL \
  (APP_GEMDRVEMUL << 8 | 0x4E)  // Show the Fsfirst call
#define GEMDRVEMUL_FSNEXT_CALL \
  (APP_GEMDRVEMUL << 8 | 0x4F)  // Show the Fsnext call
#define GEMDRVEMUL_FRENAME_CALL \
  (APP_GEMDRVEMUL << 8 | 0x56)  // Show the Frename call
#define GEMDRVEMUL_FDATETIME_CALL \
  (APP_GEMDRVEMUL << 8 | 0x57)  // Show the Fdatetime call

#define GEMDRVEMUL_PEXEC_CALL \
  (APP_GEMDRVEMUL << 8 | 0x4B)  // Show the Pexec call
#define GEMDRVEMUL_MALLOC_CALL \
  (APP_GEMDRVEMUL << 8 | 0x48)  // Show the Malloc call

#define GEMDRVEMUL_READ_BUFF_CALL \
  (APP_GEMDRVEMUL << 8 | 0x81)  // Read from sdCard the read buffer call
#define GEMDRVEMUL_DEBUG (APP_GEMDRVEMUL << 8 | 0x82)  // Show the debug info
#define GEMDRVEMUL_SAVE_BASEPAGE \
  (APP_GEMDRVEMUL << 8 | 0x83)  // Save a basepage
#define GEMDRVEMUL_SAVE_EXEC_HEADER \
  (APP_GEMDRVEMUL << 8 | 0x84)  // Save an exec header

#define GEMDRVEMUL_SET_SHARED_VAR \
  (APP_GEMDRVEMUL << 8 | 0x87)  // Set a shared variable
#define GEMDRVEMUL_WRITE_BUFF_CALL \
  (APP_GEMDRVEMUL << 8 | 0x88)  // Write to sdCard the write buffer call
#define GEMDRVEMUL_WRITE_BUFF_CHECK \
  (APP_GEMDRVEMUL << 8 | 0x89)  // Write to sdCard the write buffer check call
#define GEMDRVEMUL_DTA_EXIST_CALL \
  (APP_GEMDRVEMUL << 8 | 0x8A)  // Check if the DTA exists in the rp2040 memory
#define GEMDRVEMUL_DTA_RELEASE_CALL \
  (APP_GEMDRVEMUL << 8 | 0x8B)  // Release the DTA from the rp2040 memory

// Atari ST FATTRIB flag
#define FATTRIB_INQUIRE 0x00
#define FATTRIB_SET 0x01

// Atari ST FDATETIME flag
#define FDATETIME_INQUIRE 0x00
#define FDATETIME_SET 0x01

// Atari ST GEMDOS error codes
#define GEMDOS_EOK 0        // OK
#define GEMDOS_ERROR -1     // Generic error
#define GEMDOS_EDRVNR -2    // Drive not ready
#define GEMDOS_EUNCMD -3    // Unknown command
#define GEMDOS_E_CRC -4     // CRC error
#define GEMDOS_EBADRQ -5    // Bad request
#define GEMDOS_E_SEEK -6    // Seek error
#define GEMDOS_EMEDIA -7    // Unknown media
#define GEMDOS_ESECNF -8    // Sector not found
#define GEMDOS_EPAPER -9    // Out of paper
#define GEMDOS_EWRITF -10   // Write fault
#define GEMDOS_EREADF -11   // Read fault
#define GEMDOS_EWRPRO -13   // Device is write protected
#define GEMDOS_E_CHNG -14   // Media change detected
#define GEMDOS_EUNDEV -15   // Unknown device
#define GEMDOS_EINVFN -32   // Invalid function
#define GEMDOS_EFILNF -33   // File not found
#define GEMDOS_EPTHNF -34   // Path not found
#define GEMDOS_ENHNDL -35   // No more handles
#define GEMDOS_EACCDN -36   // Access denied
#define GEMDOS_EIHNDL -37   // Invalid handle
#define GEMDOS_ENSMEM -39   // Insufficient memory
#define GEMDOS_EIMBA -40    // Invalid memory block address
#define GEMDOS_EDRIVE -46   // Invalid drive specification
#define GEMDOS_ENSAME -48   // Cross device rename
#define GEMDOS_ENMFIL -49   // No more files
#define GEMDOS_ELOCKED -58  // Record is already locked
#define GEMDOS_ENSLOCK -59  // Invalid lock removal request
#define GEMDOS_ERANGE -64   // Range error
#define GEMDOS_EINTRN -65   // Internal error
#define GEMDOS_EPLFMT -66   // Invalid program load format
#define GEMDOS_EGSBF -67    // Memory block growth failure
#define GEMDOS_ELOOP -80    // Too many symbolic links
#define GEMDOS_EMOUNT -200  // Mount point crossed (indicator)

#define DTA_HASH_TABLE_SIZE 128
#define DTA_POOL_SIZE 32

#define PDCLSIZE 0x80 /*  size of command line in bytes  */
#define MAXDEVS 16    /* max number of block devices */

typedef struct {
  /* No. of Free Clusters */
  uint32_t b_free;
  /* Clusters per Drive */
  uint32_t b_total;
  /* Bytes per Sector */
  uint32_t b_secsize;
  /* Sectors per Cluster */
  uint32_t b_clsize;
} TOS_DISKINFO;

typedef struct {
  char d_name[12];         /* file name: filename.typ     00-11   */
  uint32_t d_offset_drive; /* dir position                12-15   */
  uint16_t d_curbyt;       /* byte pointer within current cluster 16-17 */
  uint16_t d_curcl;        /* current cluster number for file	   18-19 */
  uint8_t d_attr;          /* attributes of file          20      */
  uint8_t d_attrib;        /* attributes of f file 21 */
  uint16_t d_time;         /* time from file date 22-23 */
  uint16_t d_date;         /* date from file date 24-25 */
  uint32_t d_length;       /* file length in bytes 26-29 */
  char d_fname[14];        /* file name: filename.typ 30-43 */
} DTA;

typedef struct __attribute__((aligned(4))) DTANode {
  uint32_t key;
  uint32_t attribs;
  TCHAR fname[14];
  DTA data;
  DIR *dj;
  TCHAR *pat; /* Pointer to name matching pattern. Hack for dir_findfirst(). */
  struct DTANode *next;
} DTANode;

typedef struct __attribute__((aligned(4))) FileDescriptors {
  char fpath[GEMDRIVE_MAX_FOLDER_LENGTH];
  int fd;
  uint32_t offset;
  FIL fobject;
  struct FileDescriptors *next;
} FileDescriptors;

typedef struct _pd PD;
struct _pd {
  /* 0x00 */
  char *p_lowtpa;  /* pointer to start of TPA */
  char *p_hitpa;   /* pointer to end of TPA+1 */
  char *p_tbase;   /* pointer to base of text segment */
  uint32_t p_tlen; /* length of text segment */

  /* 0x10 */
  char *p_dbase;   /* pointer to base of data segment */
  uint32_t p_dlen; /* length of data segment */
  char *p_bbase;   /* pointer to base of bss segment */
  uint32_t p_blen; /* length of bss segment */

  /* 0x20 */
  DTA *p_xdta;
  PD *p_parent;      /* parent PD */
  uint32_t p_hflags; /* see below */
  char *p_env;       /* pointer to environment string */

  /* 0x30 */
  uint32_t p_1fill[2]; /* (junk) */
  uint16_t p_curdrv;   /* current drive */
  uint16_t p_uftsize;  /* number of OFD pointers at p_uft */
  void **p_uft;        /* ptr to my uft (allocated after env.) */

  /* 0x40 */
  uint p_curdir[MAXDEVS]; /* startcl of cur dir on each drive */

  /* 0x60 */
  ulong p_3fill[2]; /* (junk) */
  ulong p_dreg[1];  /* dreg[0] */
  ulong p_areg[5];  /* areg[3..7] */

  /* 0x80 */
  char p_cmdlin[PDCLSIZE]; /* command line image */
};

typedef struct ExecHeader {
  uint16_t magic;
  uint16_t text_h;
  uint16_t text_l;
  uint16_t data_h;
  uint16_t data_l;
  uint16_t bss_h;
  uint16_t bss_l;
  uint16_t syms_h;
  uint16_t syms_l;
  uint16_t reserved1_h;
  uint16_t reserved1_l;
  uint16_t prgflags_h;
  uint16_t prgflags_l;
  uint16_t absflag;
} ExecHeader;

// Function Prototypes
void __not_in_flash_func(gemdrive_init)();
void __not_in_flash_func(gemdrive_loop)(TransmissionProtocol *protocol,
                                        uint16_t *payloadPtr);
#endif  // GEMDRIVE_H
