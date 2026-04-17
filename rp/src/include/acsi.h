/**
 * File: acsi.h
 * Author: Diego Parrilla Santamaria
 * Date: April 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Header file for the placeholder ACSI hard disk emulator.
 */

#ifndef ACSI_H
#define ACSI_H

#include "ff.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "aconfig.h"
#include "chandler.h"
#include "constants.h"
#include "debug.h"
#include "memfunc.h"
#include "pico/stdlib.h"
#include "sdcard.h"
#include "tprotocol.h"

#define ACSIEMUL_RANDOM_TOKEN_OFFSET (CHANDLER_RANDOM_TOKEN_OFFSET)
#define ACSIEMUL_RANDOM_TOKEN_SEED_OFFSET (CHANDLER_RANDOM_TOKEN_SEED_OFFSET)
#define ACSIEMUL_SHARED_VARIABLES_OFFSET (CHANDLER_SHARED_VARIABLES_OFFSET)

// Index for the common shared variables
#define ACSIEMUL_HARDWARE_TYPE (CHANDLER_HARDWARE_TYPE)
#define ACSIEMUL_SVERSION (CHANDLER_SVERSION)
#define ACSIEMUL_BUFFER_TYPE (CHANDLER_BUFFER_TYPE)

// Reserve a placeholder ACSI region after RTC shared-memory usage.
#define ACSIEMUL_GAP_SIZE 0x2200
#define ACSIEMUL_SHARED_VARIABLE_SIZE (ACSIEMUL_GAP_SIZE / 4)
#define ACSIEMUL_SHARED_VARIABLES_COUNT 64

#define ACSIEMUL_SVAR_ENABLED (ACSIEMUL_SHARED_VARIABLE_SIZE + 0)
#define ACSIEMUL_SVAR_FIRST_UNIT (ACSIEMUL_SHARED_VARIABLE_SIZE + 1)
#define ACSIEMUL_SVAR_HOOKS_INSTALLED (ACSIEMUL_SHARED_VARIABLE_SIZE + 2)
#define ACSIEMUL_SVAR_OLD_HDV_INIT (ACSIEMUL_SHARED_VARIABLE_SIZE + 3)
#define ACSIEMUL_SVAR_OLD_HDV_BPB (ACSIEMUL_SHARED_VARIABLE_SIZE + 4)
#define ACSIEMUL_SVAR_OLD_HDV_RW (ACSIEMUL_SHARED_VARIABLE_SIZE + 5)
#define ACSIEMUL_SVAR_OLD_HDV_BOOT (ACSIEMUL_SHARED_VARIABLE_SIZE + 6)
#define ACSIEMUL_SVAR_OLD_HDV_MEDIACH (ACSIEMUL_SHARED_VARIABLE_SIZE + 7)
#define ACSIEMUL_SVAR_FIRST_VOLUME_DRIVE (ACSIEMUL_SHARED_VARIABLE_SIZE + 8)
#define ACSIEMUL_SVAR_LAST_VOLUME_DRIVE (ACSIEMUL_SHARED_VARIABLE_SIZE + 9)
#define ACSIEMUL_SVAR_PUN_INFO_PTR (ACSIEMUL_SHARED_VARIABLE_SIZE + 10)
#define ACSIEMUL_SVAR_RW_STATUS (ACSIEMUL_SHARED_VARIABLE_SIZE + 11)
#define ACSIEMUL_SVAR_MEDIA_CHANGED_MASK (ACSIEMUL_SHARED_VARIABLE_SIZE + 12)

#define ACSIEMUL_VARIABLES_OFFSET \
  (ACSIEMUL_RANDOM_TOKEN_OFFSET + ACSIEMUL_GAP_SIZE + \
   ACSIEMUL_SHARED_VARIABLES_COUNT)

#define ACSI_PUN_INFO_MAXUNITS 16u
#define ACSI_PUN_INFO_SIZE 160u
#define ACSI_BPB_STRUCT_SIZE 34u
#define ACSI_BPB_SLOT_SIZE 40u
#define ACSI_BPB_PTR_TABLE_SIZE (ACSI_PUN_INFO_MAXUNITS * 4u)
#define ACSI_BPB_DATA_TOTAL_SIZE (ACSI_PUN_INFO_MAXUNITS * ACSI_BPB_SLOT_SIZE)

#define ACSIEMUL_PUN_INFO_OFFSET (ACSIEMUL_VARIABLES_OFFSET)
#define ACSIEMUL_PUN_INFO_PUNS_OFFSET (ACSIEMUL_PUN_INFO_OFFSET + 0u)
#define ACSIEMUL_PUN_INFO_PUN_OFFSET (ACSIEMUL_PUN_INFO_OFFSET + 2u)
#define ACSIEMUL_PUN_INFO_PRT_START_OFFSET (ACSIEMUL_PUN_INFO_OFFSET + 20u)
#define ACSIEMUL_PUN_INFO_P_COOKIE_OFFSET (ACSIEMUL_PUN_INFO_OFFSET + 84u)
#define ACSIEMUL_PUN_INFO_P_COOKPTR_OFFSET (ACSIEMUL_PUN_INFO_OFFSET + 88u)
#define ACSIEMUL_PUN_INFO_P_VERSION_OFFSET (ACSIEMUL_PUN_INFO_OFFSET + 92u)
#define ACSIEMUL_PUN_INFO_P_MAX_SECTOR_OFFSET (ACSIEMUL_PUN_INFO_OFFSET + 94u)
#define ACSIEMUL_PUN_INFO_RESERVED_OFFSET (ACSIEMUL_PUN_INFO_OFFSET + 96u)
#define ACSIEMUL_BPB_PTR_TABLE_OFFSET \
  (ACSIEMUL_PUN_INFO_OFFSET + ACSI_PUN_INFO_SIZE)
#define ACSIEMUL_BPB_DATA_OFFSET \
  (ACSIEMUL_BPB_PTR_TABLE_OFFSET + ACSI_BPB_PTR_TABLE_SIZE)
#define ACSIEMUL_IMAGE_BUFFER_OFFSET \
  (ACSIEMUL_BPB_DATA_OFFSET + ACSI_BPB_DATA_TOTAL_SIZE)
#define ACSIEMUL_IMAGE_BUFFER_SIZE (ROM_SIZE_BYTES - ACSIEMUL_IMAGE_BUFFER_OFFSET)

#define ACSIEMUL_ST_BASE_ADDRESS 0xFA0000u
#define ACSIEMUL_ST_PUN_INFO_PTR \
  (ACSIEMUL_ST_BASE_ADDRESS + ACSIEMUL_PUN_INFO_OFFSET)
#define ACSIEMUL_ST_PUN_INFO_COOKIE_PTR \
  (ACSIEMUL_ST_PUN_INFO_PTR + 84u)
#define ACSIEMUL_ST_BPB_DATA_BASE \
  (ACSIEMUL_ST_BASE_ADDRESS + ACSIEMUL_BPB_DATA_OFFSET)
#define ACSIEMUL_PUN_INFO_COOKIE_AHDI 0x41484449u

#define ACSI_ASSERT_ALIGNED_4(offset) \
  _Static_assert(((offset) & 0x3u) == 0u, #offset " must stay 4-byte aligned")

ACSI_ASSERT_ALIGNED_4(ACSIEMUL_RANDOM_TOKEN_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_RANDOM_TOKEN_SEED_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_SHARED_VARIABLES_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_VARIABLES_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_PUN_INFO_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_PUN_INFO_PRT_START_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_PUN_INFO_P_COOKIE_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_PUN_INFO_P_COOKPTR_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_PUN_INFO_RESERVED_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_BPB_PTR_TABLE_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_BPB_DATA_OFFSET);
ACSI_ASSERT_ALIGNED_4(ACSIEMUL_IMAGE_BUFFER_OFFSET);
_Static_assert(ACSIEMUL_IMAGE_BUFFER_OFFSET < ROM_SIZE_BYTES,
               "ACSI image buffer must fit in the shared ROM window");
_Static_assert(ACSIEMUL_IMAGE_BUFFER_SIZE >= 512u,
               "ACSI image buffer must hold at least one 512-byte sector");

#define APP_ACSIEMUL 0x05
#define ACSIEMUL_SET_SHARED_VAR \
  (APP_ACSIEMUL << 8 | 0x00)  // Reserved for future shared variable updates
#define ACSIEMUL_READ_SECTOR (APP_ACSIEMUL << 8 | 0x01)
#define ACSIEMUL_READ_SECTOR_BATCH (APP_ACSIEMUL << 8 | 0x02)
#define ACSIEMUL_WRITE_SECTOR (APP_ACSIEMUL << 8 | 0x03)
#define ACSIEMUL_WRITE_SECTOR_BATCH (APP_ACSIEMUL << 8 | 0x04)
#define ACSIEMUL_DEBUG (APP_ACSIEMUL << 8 | 0x0C)

#define ACSIEMUL_DEBUG_HDV_INIT 0x00
#define ACSIEMUL_DEBUG_HDV_BPB 0x01
#define ACSIEMUL_DEBUG_HDV_BOOT 0x03
#define ACSIEMUL_DEBUG_HDV_BPB_MATCH 0x06
#define ACSIEMUL_DEBUG_DRVBITS 0x09
#define ACSIEMUL_DEBUG_FIRST_VOLUME_DRIVE 0x0A
#define ACSIEMUL_DEBUG_LAST_VOLUME_DRIVE 0x0B
#define ACSIEMUL_DEBUG_DRVBITS_MASK 0x0C
#define ACSIEMUL_DEBUG_HDV_MEDIACH_STATUS 0x0D
#define ACSIEMUL_DEBUG_HDV_RW_OLD_HANDLER 0x0E
#define ACSIEMUL_DEBUG_HDV_BPB_DATA 0x0F
#define ACSIEMUL_DEBUG_MEM_MAP_A 0x13
#define ACSIEMUL_DEBUG_MEM_MAP_B 0x14
#define ACSIEMUL_DEBUG_REBIND_DECISION 0x15

#define ACSIEMUL_REBIND_PLACED 0u
#define ACSIEMUL_REBIND_SKIP_NO_PUN 2u
#define ACSIEMUL_REBIND_SKIP_MAX_SECTOR 3u
#define ACSIEMUL_REBIND_SKIP_NO_BCBS 4u
#define ACSIEMUL_REBIND_SKIP_NEED_EXCEEDS 5u
#define ACSIEMUL_REBIND_SKIP_MEMTOP_ZERO 6u

#define ACSI_IMAGE_SECTOR_SIZE 512u
#define ACSI_PARTITION_COUNT 4u
#define ACSI_MAX_PARTITIONS 16u
#define ACSI_ROOT_DIR_ENTRY_SIZE 32u
#define ACSI_TEST_ROOT_ENTRY_LIMIT 64u

// FatFS fastseek (FF_USE_FASTSEEK=1 in ffconf.h) replaces the linear FAT
// walk on every f_lseek with an O(1) cluster-linkmap lookup. The table
// is allocated per open context and holds (count, cluster) pairs plus a
// terminator. Contiguous images need ~3 entries; fragmented ones need
// 2*fragment_count+1. Cap the allocation so a pathologically fragmented
// image falls back to linear lseek instead of exhausting the heap.
#define ACSI_IMAGE_CLTBL_INITIAL 32u
#define ACSI_IMAGE_CLTBL_MAX 512u

typedef struct {
  FIL file;
  char imagePath[MAX_FILENAME_LENGTH + 1];
  FSIZE_t imageSizeBytes;
  uint32_t totalSectors;
  bool isOpen;
  bool readOnly;
  DWORD *cltbl;           // NULL when fastseek is unavailable for this image
  size_t cltblEntries;    // allocated size in DWORDs (0 when cltbl is NULL)
} AcsiImageContext;

typedef struct {
  uint8_t bootIndicator;
  uint8_t partitionType;
  uint32_t firstLBA;
  uint32_t sectorCount;
  bool isPresent;
  bool isFat16;
  bool isExtended;
} AcsiPartitionEntry;

typedef struct {
  uint16_t bytesPerSector;
  uint8_t sectorsPerCluster;
  uint16_t reservedSectorCount;
  uint8_t fatCount;
  uint16_t rootEntryCount;
  uint32_t totalSectors;
  uint16_t sectorsPerFat;
  uint16_t sectorsPerTrack;
  uint16_t headCount;
  uint32_t hiddenSectors;
  uint8_t mediaDescriptor;
  uint32_t fatStartLBA;
  uint32_t rootDirStartLBA;
  uint32_t rootDirSectorCount;
  uint32_t dataStartLBA;
  uint32_t dataSectorCount;
  uint32_t clusterCount;
} AcsiFat16Geometry;

FRESULT acsi_image_open(AcsiImageContext *context, const char *imagePath,
                        bool readOnly);
void acsi_image_close(AcsiImageContext *context);
FRESULT acsi_image_read_sectors(AcsiImageContext *context, uint32_t lba,
                                uint16_t sectorCount, void *buffer,
                                size_t bufferSize);
FRESULT acsi_image_write_sectors(AcsiImageContext *context, uint32_t lba,
                                 uint16_t sectorCount, const void *buffer,
                                 size_t bufferSize);
FRESULT acsi_parse_mbr(AcsiImageContext *context,
                       AcsiPartitionEntry partitions[ACSI_PARTITION_COUNT],
                       uint8_t *partitionCountOut);
FRESULT acsi_enumerate_partitions(AcsiImageContext *context,
                                  AcsiPartitionEntry partitions[ACSI_MAX_PARTITIONS],
                                  uint8_t *partitionCountOut);
FRESULT acsi_parse_fat16_geometry(AcsiImageContext *context,
                                  const AcsiPartitionEntry *partition,
                                  AcsiFat16Geometry *geometry);

// Write-behind flush window for acsi_tick(). Defaults to the shared
// DRIVES_FLUSH_INTERVAL_MS in constants.h; override here if ACSI ever
// needs a different window from the floppy path.
#define ACSI_FLUSH_INTERVAL_MS DRIVES_FLUSH_INTERVAL_MS

void __not_in_flash_func(acsi_init)();
void acsi_preInit(void);
void __not_in_flash_func(acsi_loop)(TransmissionProtocol *lastProtocol,
                                    uint16_t *payloadPtr);
void __not_in_flash_func(acsi_tick)(void);

#endif  // ACSI_H
