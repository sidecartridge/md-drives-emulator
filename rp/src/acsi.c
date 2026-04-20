/**
 * File: acsi.c
 * Author: Diego Parrilla Santamaria
 * Date: April 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Placeholder ACSI hard disk emulator and FAT16 image tests.
 */

#include "include/acsi.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static uint32_t memorySharedAddress = 0;
static uint32_t memoryRandomTokenAddress = 0;
static uint32_t memoryRandomTokenSeedAddress = 0;
static char acsiImagePath[MAX_FILENAME_LENGTH + 1] = {0};
// Set whenever acsiImagePath is (re)written; cleared after
// acsiEnsureRuntimeImageOpen confirms the currently-open image still
// matches the configured path. Avoids a per-I/O strncmp on the hot path.
static bool acsiImagePathDirty = true;
static FATFS acsiFilesys = {0};
static uint32_t acsiFirstVolumeDrive = 0;
static uint32_t acsiLastVolumeDrive = 0;
static bool acsiVolumeDriveRangeValid = false;
// Largest logical sector size we saw during the most recent partition scan
// that was rejected because it exceeds what the BCB rebind pool can back
// (currently 8192 bytes — see ACSIEMUL_BCB_POOL_BYTES in acsi.s / main.s).
// Surfaced in the boot summary so the user gets a clear "this image is too
// big for the current build" message instead of a silent empty drive list.
static uint16_t acsiScanOversizedSectorSize = 0;
static uint32_t acsiLastLoggedHooksInstalled = 0;
static uint32_t acsiLastLoggedOldHdvInit = 0;
static uint32_t acsiLastLoggedOldHdvBpb = 0;
static uint32_t acsiLastLoggedOldHdvRw = 0;
static uint32_t acsiLastLoggedOldHdvBoot = 0;
static uint32_t acsiLastLoggedOldHdvMediach = 0;
static bool acsiPunInfoValid = false;
static uint16_t acsiPunInfoPuns = 0;
static uint8_t acsiPunInfoUnits[ACSI_PUN_INFO_MAXUNITS] = {0};
static uint32_t acsiPunInfoStartSectors[ACSI_PUN_INFO_MAXUNITS] = {0};
static uint32_t acsiPartitionSectorCounts[ACSI_PUN_INFO_MAXUNITS] = {0};
static uint16_t acsiLogicalSectorSizes[ACSI_PUN_INFO_MAXUNITS] = {0};
static uint16_t acsiLogicalToPhysicalRatios[ACSI_PUN_INFO_MAXUNITS] = {0};
// Per-drive layout metadata captured during acsiBuildAnnouncedVolumeData,
// surfaced to the setup-screen boot summary via acsi_printBootInfo.
static uint8_t acsiPartitionStyle[ACSI_PUN_INFO_MAXUNITS] = {0};
static bool acsiPartitionViewIsTos[ACSI_PUN_INFO_MAXUNITS] = {0};
static AcsiImageContext acsiRuntimeImage = {0};

// Write-behind flush state. Writes mark the image dirty and record when
// the dirty window opened; acsi_tick() flushes via f_sync once the window
// has aged past ACSI_FLUSH_INTERVAL_MS with no further writes resetting it.
static volatile bool acsiWriteDirty = false;
static volatile uint32_t acsiWriteDirtyAtMs = 0;

static inline void __not_in_flash_func(acsiMarkWriteDirty)(void) {
  if (!acsiWriteDirty) {
    acsiWriteDirtyAtMs = to_ms_since_boot(get_absolute_time());
    acsiWriteDirty = true;
  }
}

typedef struct {
  uint16_t recsize;
  uint16_t clsiz;
  uint16_t clsizb;
  uint16_t rdlen;
  uint16_t fsiz;
  uint16_t fatrec;
  uint16_t datrec;
  uint16_t numcl;
  uint16_t bflags;
  uint16_t trackcnt;
  uint16_t sidecnt;
  uint16_t secpcyl;
  uint16_t secptrack;
  uint16_t reserved[3];
  uint16_t diskNumber;
} AcsiBPBData;

_Static_assert(sizeof(AcsiBPBData) == ACSI_BPB_STRUCT_SIZE,
               "ACSI BPB layout must match the Atari BPB structure");

typedef enum {
  ACSI_TOSDOS_STYLE_NONE = 0,
  ACSI_TOSDOS_STYLE_PPDRIVER,
  ACSI_TOSDOS_STYLE_HDDRIVER,
} AcsiTosDosStyle;

typedef struct {
  uint16_t bytesPerSector;
  uint8_t sectorsPerCluster;
  uint16_t reservedSectorCount;
  uint8_t fatCount;
  uint16_t rootEntryCount;
  uint16_t totalLogicalSectors;
  uint16_t sectorsPerFat;
  uint8_t mediaDescriptor;
  uint16_t sectorsPerTrack;
  uint16_t headCount;
  uint32_t fatStartLBA;
  uint32_t rootDirStartLBA;
  uint32_t rootDirSectorCount;
  uint32_t dataStartLBA;
  uint32_t totalPhysicalSectors;
} AcsiTosBootSectorInfo;

typedef struct {
  uint8_t status;
  char idText[4];
  uint32_t firstLBA;
  uint32_t sectorCount;
  bool isPresent;
  bool isBootable;
  bool isStandard;
  bool isExtended;
} AcsiTosPartitionEntry;

static AcsiBPBData acsiBpbData[ACSI_PUN_INFO_MAXUNITS] = {0};
static uint32_t acsiBpbPointers[ACSI_PUN_INFO_MAXUNITS] = {0};

static bool acsiIsEnabledSetting(void);
static uint8_t acsiGetIdSetting(void);
static uint8_t acsiGetStartDriveSetting(void);
static char acsiDriveNumberToLetter(uint32_t driveNumber);
static inline uint16_t acsiReadLe16(const BYTE *buffer, size_t offset);
static void acsiTestLog(const char *fmt, ...);
static void acsiFormatSize(uint64_t bytes, char *output, size_t outputSize);
static AcsiTosDosStyle acsiDetectTosDosStyle(
    AcsiImageContext *context, const AcsiPartitionEntry *partition,
    const AcsiFat16Geometry *dosGeometry, AcsiTosBootSectorInfo *tosInfoOut);

static void acsiResetPartitionSectorCounts(void) {
  memset(acsiPartitionSectorCounts, 0, sizeof(acsiPartitionSectorCounts));
  memset(acsiLogicalSectorSizes, 0, sizeof(acsiLogicalSectorSizes));
  memset(acsiLogicalToPhysicalRatios, 0, sizeof(acsiLogicalToPhysicalRatios));
  memset(acsiPartitionStyle, 0, sizeof(acsiPartitionStyle));
  memset(acsiPartitionViewIsTos, 0, sizeof(acsiPartitionViewIsTos));
}

static void __not_in_flash_func(acsiSetRwStatus)(int32_t status) {
  uint32_t rawStatus = (uint32_t)status;
  uint32_t offset =
      ACSIEMUL_SHARED_VARIABLES_OFFSET + (ACSIEMUL_SVAR_RW_STATUS * 4u);

  WRITE_WORD(memorySharedAddress, offset + 2u, rawStatus & 0xFFFFu);
  WRITE_WORD(memorySharedAddress, offset, rawStatus >> 16);
}

static bool __not_in_flash_func(acsiDriveIsOwned)(uint16_t driveNumber) {
  return driveNumber < ACSI_PUN_INFO_MAXUNITS &&
         acsiBpbPointers[driveNumber] != 0u &&
         acsiPartitionSectorCounts[driveNumber] != 0u &&
         acsiLogicalSectorSizes[driveNumber] != 0u &&
         acsiLogicalToPhysicalRatios[driveNumber] != 0u;
}

static uint16_t acsiGetLogicalToPhysicalRatio(uint32_t logicalSectorSize) {
  switch (logicalSectorSize) {
    case 512u:
      return 1u;
    case 1024u:
      return 2u;
    case 2048u:
      return 4u;
    case 4096u:
      return 8u;
    case 8192u:
      return 16u;
    default:
      return 0u;
  }
}

static uint32_t acsiBuildInitialMediaChangedMask(void) {
  uint32_t mask = 0u;

  for (uint16_t driveNumber = 0; driveNumber < ACSI_PUN_INFO_MAXUNITS;
       ++driveNumber) {
    if (acsiDriveIsOwned(driveNumber)) {
      mask |= (1u << driveNumber);
    }
  }

  return mask;
}

static FRESULT __not_in_flash_func(acsiEnsureRuntimeImageOpen)(void) {
  if (acsiImagePath[0] == '\0') {
    return FR_INVALID_OBJECT;
  }

  // Fast path: image already open and the configured path hasn't been
  // touched since the last successful open. Skips the per-I/O strncmp.
  if (acsiRuntimeImage.isOpen && !acsiImagePathDirty) {
    return FR_OK;
  }

  if (acsiRuntimeImage.isOpen) {
    if (strncmp(acsiRuntimeImage.imagePath, acsiImagePath,
                sizeof(acsiRuntimeImage.imagePath)) == 0) {
      acsiImagePathDirty = false;
      return FR_OK;
    }
    acsi_image_close(&acsiRuntimeImage);
  }

  FRESULT fr = acsi_image_open(&acsiRuntimeImage, acsiImagePath, false);
  if (fr == FR_OK) {
    acsiImagePathDirty = false;
    return FR_OK;
  }

  if (fr != FR_NOT_ENABLED) {
    return fr;
  }

  fr = f_mount(&acsiFilesys, "0:", 1);
  if (fr != FR_OK) {
    return fr;
  }

  return acsi_image_open(&acsiRuntimeImage, acsiImagePath, false);
}

static void acsiResetBpbCache(void) {
  memset(acsiBpbData, 0, sizeof(acsiBpbData));
  memset(acsiBpbPointers, 0, sizeof(acsiBpbPointers));
}

static bool acsiBuildBpbData(uint16_t driveNumber,
                             const AcsiFat16Geometry *geometry,
                             AcsiBPBData *bpb) {
  if (geometry == NULL || bpb == NULL) {
    return false;
  }

  uint32_t clsizb = (uint32_t)geometry->bytesPerSector *
                    (uint32_t)geometry->sectorsPerCluster;
  uint32_t fatrec = (uint32_t)geometry->reservedSectorCount +
                    (uint32_t)geometry->sectorsPerFat;
  uint32_t datrec =
      (uint32_t)geometry->reservedSectorCount +
      ((uint32_t)geometry->fatCount * (uint32_t)geometry->sectorsPerFat) +
      (uint32_t)geometry->rootDirSectorCount;
  uint32_t secpcyl =
      (uint32_t)geometry->sectorsPerTrack * (uint32_t)geometry->headCount;
  uint32_t trackcnt = 0u;

  if (secpcyl != 0u) {
    trackcnt = geometry->totalSectors / secpcyl;
    if (trackcnt > 0xFFFFu) {
      DPRINTF(
          "ACSI BPB drive %c track count overflow: %lu, clamping to 65535\n",
          acsiDriveNumberToLetter(driveNumber), (unsigned long)trackcnt);
      trackcnt = 0xFFFFu;
    }
  }

  if (geometry->bytesPerSector > 0xFFFFu ||
      geometry->sectorsPerCluster > 0xFFFFu || clsizb > 0xFFFFu ||
      geometry->rootDirSectorCount > 0xFFFFu ||
      geometry->sectorsPerFat > 0xFFFFu || fatrec > 0xFFFFu ||
      datrec > 0xFFFFu || geometry->clusterCount > 0xFFFFu ||
      geometry->headCount > 0xFFFFu || secpcyl > 0xFFFFu ||
      geometry->sectorsPerTrack > 0xFFFFu || driveNumber > 0xFFFFu) {
    DPRINTF("ACSI BPB drive %c geometry exceeds Atari BPB limits\n",
            acsiDriveNumberToLetter(driveNumber));
    return false;
  }

  memset(bpb, 0, sizeof(*bpb));
  bpb->recsize = (uint16_t)geometry->bytesPerSector;
  bpb->clsiz = (uint16_t)geometry->sectorsPerCluster;
  bpb->clsizb = (uint16_t)clsizb;
  bpb->rdlen = (uint16_t)geometry->rootDirSectorCount;
  bpb->fsiz = (uint16_t)geometry->sectorsPerFat;
  bpb->fatrec = (uint16_t)fatrec;
  bpb->datrec = (uint16_t)datrec;
  bpb->numcl = (uint16_t)geometry->clusterCount;
  // bflags bit 0 = B_16TOS: 1 => 16-bit FAT, 0 => 12-bit FAT. Without this,
  // TOS walks the FAT as FAT12 and truncates cluster numbers to 12 bits,
  // so any cluster > 4095 gets mangled on the first FAT lookup.
  bpb->bflags = 1u;
  bpb->trackcnt = (uint16_t)trackcnt;
  bpb->sidecnt = (uint16_t)geometry->headCount;
  bpb->secpcyl = (uint16_t)secpcyl;
  bpb->secptrack = (uint16_t)geometry->sectorsPerTrack;
  bpb->diskNumber = driveNumber;
  return true;
}

static bool acsiBuildTosBpbData(uint16_t driveNumber,
                                const AcsiTosBootSectorInfo *tosInfo,
                                AcsiBPBData *bpb) {
  if (tosInfo == NULL || bpb == NULL) {
    return false;
  }

  uint32_t clsizb =
      (uint32_t)tosInfo->bytesPerSector * (uint32_t)tosInfo->sectorsPerCluster;
  uint32_t rootDirLogicalSectorCount =
      (((uint32_t)tosInfo->rootEntryCount * ACSI_ROOT_DIR_ENTRY_SIZE) +
       (uint32_t)tosInfo->bytesPerSector - 1u) /
      (uint32_t)tosInfo->bytesPerSector;
  uint32_t fatrec =
      (uint32_t)tosInfo->reservedSectorCount + (uint32_t)tosInfo->sectorsPerFat;
  uint32_t datrec =
      (uint32_t)tosInfo->reservedSectorCount +
      ((uint32_t)tosInfo->fatCount * (uint32_t)tosInfo->sectorsPerFat) +
      rootDirLogicalSectorCount;
  uint32_t secpcyl =
      (uint32_t)tosInfo->sectorsPerTrack * (uint32_t)tosInfo->headCount;
  uint32_t trackcnt = 0u;
  uint32_t clusterCount = 0u;

  if (secpcyl != 0u) {
    trackcnt = (uint32_t)tosInfo->totalLogicalSectors / secpcyl;
    if (trackcnt > 0xFFFFu) {
      trackcnt = 0xFFFFu;
    }
  }

  if (tosInfo->sectorsPerCluster != 0u &&
      (uint32_t)tosInfo->totalLogicalSectors >= datrec) {
    clusterCount = ((uint32_t)tosInfo->totalLogicalSectors - datrec) /
                   (uint32_t)tosInfo->sectorsPerCluster;
  }

  if (tosInfo->bytesPerSector > 0xFFFFu ||
      tosInfo->sectorsPerCluster > 0xFFFFu || clsizb > 0xFFFFu ||
      rootDirLogicalSectorCount > 0xFFFFu || tosInfo->sectorsPerFat > 0xFFFFu ||
      fatrec > 0xFFFFu || datrec > 0xFFFFu || clusterCount > 0xFFFFu ||
      tosInfo->headCount > 0xFFFFu || secpcyl > 0xFFFFu ||
      tosInfo->sectorsPerTrack > 0xFFFFu || driveNumber > 0xFFFFu) {
    return false;
  }

  memset(bpb, 0, sizeof(*bpb));
  bpb->recsize = tosInfo->bytesPerSector;
  bpb->clsiz = (uint16_t)tosInfo->sectorsPerCluster;
  bpb->clsizb = (uint16_t)clsizb;
  bpb->rdlen = (uint16_t)rootDirLogicalSectorCount;
  bpb->fsiz = tosInfo->sectorsPerFat;
  bpb->fatrec = (uint16_t)fatrec;
  bpb->datrec = (uint16_t)datrec;
  bpb->numcl = (uint16_t)clusterCount;
  // bflags bit 0 = B_16TOS: 1 => 16-bit FAT, 0 => 12-bit FAT.
  bpb->bflags = 1u;
  bpb->trackcnt = (uint16_t)trackcnt;
  bpb->sidecnt = tosInfo->headCount;
  bpb->secpcyl = (uint16_t)secpcyl;
  bpb->secptrack = tosInfo->sectorsPerTrack;
  bpb->diskNumber = driveNumber;
  return true;
}

static void acsiTestLogBpbData(const char *label, const AcsiBPBData *bpb) {
  if (label == NULL || bpb == NULL) {
    return;
  }

  acsiTestLog(
      "  %s: recsize=%u clsiz=%u clsizb=%u rdlen=%u fsiz=%u fatrec=%u "
      "datrec=%u numcl=%u bflags=%u trackcnt=%u sidecnt=%u secpcyl=%u "
      "secptrack=%u disk=%u\n",
      label, (unsigned int)bpb->recsize, (unsigned int)bpb->clsiz,
      (unsigned int)bpb->clsizb, (unsigned int)bpb->rdlen,
      (unsigned int)bpb->fsiz, (unsigned int)bpb->fatrec,
      (unsigned int)bpb->datrec, (unsigned int)bpb->numcl,
      (unsigned int)bpb->bflags, (unsigned int)bpb->trackcnt,
      (unsigned int)bpb->sidecnt, (unsigned int)bpb->secpcyl,
      (unsigned int)bpb->secptrack, (unsigned int)bpb->diskNumber);
}

static void acsiResetPunInfoCache(void) {
  acsiPunInfoValid = false;
  acsiPunInfoPuns = 0;
  memset(acsiPunInfoUnits, 0x80, sizeof(acsiPunInfoUnits));
  memset(acsiPunInfoStartSectors, 0, sizeof(acsiPunInfoStartSectors));
  acsiResetPartitionSectorCounts();
}

static void acsiWritePunInfoToSharedMemory(void) {
  uint32_t punInfoPtr = acsiPunInfoValid ? ACSIEMUL_ST_PUN_INFO_PTR : 0u;
  uint16_t maxSectorSize = 0u;

  for (uint32_t offset = 0; offset < ACSI_PUN_INFO_SIZE; ++offset) {
    WRITE_BYTE(memorySharedAddress, ACSIEMUL_PUN_INFO_OFFSET + offset, 0u);
  }

  if (!acsiPunInfoValid) {
    DPRINTF("ACSI PUN_INFO disabled: no announced ACSI volumes.\n");
    return;
  }

  WRITE_WORD(memorySharedAddress, ACSIEMUL_PUN_INFO_PUNS_OFFSET,
             acsiPunInfoPuns);

  for (uint32_t index = 0; index < ACSI_PUN_INFO_MAXUNITS; ++index) {
    WRITE_BYTE(memorySharedAddress, ACSIEMUL_PUN_INFO_PUN_OFFSET + index,
               acsiPunInfoUnits[index]);
    WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                            ACSIEMUL_PUN_INFO_PRT_START_OFFSET + (index * 4u),
                            acsiPunInfoStartSectors[index]);
    WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                            ACSIEMUL_PUN_INFO_RESERVED_OFFSET + (index * 4u),
                            0u);
    if (acsiBpbPointers[index] != 0u &&
        acsiBpbData[index].recsize > maxSectorSize) {
      maxSectorSize = acsiBpbData[index].recsize;
    }
  }

  WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                          ACSIEMUL_PUN_INFO_P_COOKIE_OFFSET,
                          ACSIEMUL_PUN_INFO_COOKIE_AHDI);
  WRITE_AND_SWAP_LONGWORD(memorySharedAddress,
                          ACSIEMUL_PUN_INFO_P_COOKPTR_OFFSET,
                          ACSIEMUL_ST_PUN_INFO_COOKIE_PTR);
  WRITE_WORD(memorySharedAddress, ACSIEMUL_PUN_INFO_P_VERSION_OFFSET, 0x0300u);
  WRITE_WORD(memorySharedAddress, ACSIEMUL_PUN_INFO_P_MAX_SECTOR_OFFSET,
             (maxSectorSize != 0u) ? maxSectorSize : 512u);

  DPRINTF(
      "ACSI PUN_INFO ready: ptr=%08lX puns=%u first=%c (%lu) last=%c (%lu)\n",
      (unsigned long)punInfoPtr, (unsigned int)acsiPunInfoPuns,
      acsiDriveNumberToLetter(acsiFirstVolumeDrive),
      (unsigned long)acsiFirstVolumeDrive,
      acsiDriveNumberToLetter(acsiLastVolumeDrive),
      (unsigned long)acsiLastVolumeDrive);
}

static void acsiWriteBpbDataToSharedMemory(void) {
  memset(
      (void *)(uintptr_t)(memorySharedAddress + ACSIEMUL_BPB_PTR_TABLE_OFFSET),
      0, ACSI_BPB_PTR_TABLE_SIZE + ACSI_BPB_DATA_TOTAL_SIZE);

  for (uint32_t driveNumber = 0; driveNumber < ACSI_PUN_INFO_MAXUNITS;
       ++driveNumber) {
    uint32_t pointerOffset = ACSIEMUL_BPB_PTR_TABLE_OFFSET + (driveNumber * 4u);
    WRITE_AND_SWAP_LONGWORD(memorySharedAddress, pointerOffset,
                            acsiBpbPointers[driveNumber]);
    if (acsiBpbPointers[driveNumber] == 0u) {
      continue;
    }

    uint32_t bpbOffset =
        ACSIEMUL_BPB_DATA_OFFSET + (driveNumber * ACSI_BPB_SLOT_SIZE);
    memcpy((void *)(uintptr_t)(memorySharedAddress + bpbOffset),
           &acsiBpbData[driveNumber], sizeof(AcsiBPBData));
  }
}

static const char *acsiDebugHookName(uint32_t hookId) {
  switch (hookId) {
    case ACSIEMUL_DEBUG_HDV_INIT:
      return "hdv_init";
    case ACSIEMUL_DEBUG_HDV_BPB:
      return "hdv_bpb";
    case ACSIEMUL_DEBUG_HDV_BOOT:
      return "hdv_boot";
    case ACSIEMUL_DEBUG_HDV_BPB_MATCH:
      return "hdv_bpb_match";
    case ACSIEMUL_DEBUG_HDV_RW_OLD_HANDLER:
      return "hdv_rw_old_handler";
    case ACSIEMUL_DEBUG_HDV_BPB_DATA:
      return "hdv_bpb_data";
    case ACSIEMUL_DEBUG_DRVBITS:
      return "drvbits";
    case ACSIEMUL_DEBUG_FIRST_VOLUME_DRIVE:
      return "first_volume_drive";
    case ACSIEMUL_DEBUG_LAST_VOLUME_DRIVE:
      return "last_volume_drive";
    case ACSIEMUL_DEBUG_DRVBITS_MASK:
      return "drvbits_mask";
    case ACSIEMUL_DEBUG_HDV_MEDIACH_STATUS:
      return "hdv_mediach_status";
    case ACSIEMUL_DEBUG_MEM_MAP_A:
      return "mem_map_a";
    case ACSIEMUL_DEBUG_MEM_MAP_B:
      return "mem_map_b";
    case ACSIEMUL_DEBUG_REBIND_DECISION:
      return "rebind_decision";
    default:
      return "unknown";
  }
}

static const char *acsiRebindDecisionName(uint32_t decision) {
  switch (decision) {
    case ACSIEMUL_REBIND_PLACED:
      return "placed";
    case ACSIEMUL_REBIND_SKIP_NO_PUN:
      return "skipped-no-pun-ptr";
    case ACSIEMUL_REBIND_SKIP_MAX_SECTOR:
      return "skipped-max-sector-too-small";
    case ACSIEMUL_REBIND_SKIP_NO_BCBS:
      return "skipped-no-bcbs";
    case ACSIEMUL_REBIND_SKIP_NEED_EXCEEDS:
      return "skipped-need-exceeds-reserve";
    case ACSIEMUL_REBIND_SKIP_MEMTOP_ZERO:
      return "skipped-memtop-zero";
    default:
      return "unknown";
  }
}

static void acsiDumpBytesHexAscii(const char *label, const uint8_t *buffer,
                                  size_t length) {
  if (label == NULL || buffer == NULL || length == 0u) {
    return;
  }

  DPRINTFRAW("%s\n", label);

  for (size_t offset = 0; offset < length; offset += 16u) {
    char hexPart[16u * 3u + 1u];
    char asciiPart[16u + 1u];
    size_t hexPos = 0u;
    size_t chunkLength = length - offset;
    if (chunkLength > 16u) {
      chunkLength = 16u;
    }

    for (size_t index = 0; index < 16u; ++index) {
      uint8_t value = 0u;
      bool inRange = index < chunkLength;
      if (inRange) {
        size_t displayIndex = index ^ 1u;
        if (displayIndex >= chunkLength) {
          displayIndex = index;
        }
        value = buffer[offset + displayIndex];
        hexPos += (size_t)snprintf(&hexPart[hexPos], sizeof(hexPart) - hexPos,
                                   "%02X ", (unsigned int)value);
        asciiPart[index] = (value >= 32u && value <= 126u) ? (char)value : '.';
      } else {
        hexPos +=
            (size_t)snprintf(&hexPart[hexPos], sizeof(hexPart) - hexPos, "   ");
        asciiPart[index] = ' ';
      }
    }

    if (hexPos > 0u && hexPos < sizeof(hexPart)) {
      hexPart[hexPos - 1u] = '\0';
    } else {
      hexPart[sizeof(hexPart) - 1u] = '\0';
    }
    asciiPart[16u] = '\0';

    DPRINTFRAW("  %04X: %-47s |%s|\n", (unsigned int)offset, hexPart,
               asciiPart);
  }
}

static char acsiDriveNumberToLetter(uint32_t driveNumber) {
  if (driveNumber > 25u) {
    return '?';
  }

  return (char)('A' + (char)driveNumber);
}

static void acsiResetVolumeDriveRange(void) {
  acsiFirstVolumeDrive = 0;
  acsiLastVolumeDrive = 0;
  acsiVolumeDriveRangeValid = false;
}

static void acsiSetVolumeDriveRange(uint32_t firstDrive, uint32_t lastDrive) {
  acsiResetVolumeDriveRange();

  if (firstDrive > lastDrive) {
    return;
  }

  acsiFirstVolumeDrive = firstDrive;
  acsiLastVolumeDrive = lastDrive;
  acsiVolumeDriveRangeValid = true;
}

static void acsiBuildAnnouncedVolumeData(
    AcsiImageContext *context, uint8_t acsiId, uint8_t firstDrive,
    const AcsiPartitionEntry *usablePartitions, uint8_t usablePartitionCount) {
  acsiResetVolumeDriveRange();
  acsiResetPunInfoCache();
  acsiResetBpbCache();
  acsiScanOversizedSectorSize = 0;

  if (context == NULL || usablePartitions == NULL ||
      usablePartitionCount == 0u) {
    return;
  }

  // firstDrive is the drive-letter slot (2='C' .. 15='P') for the FIRST
  // announced partition. Subsequent partitions take the next consecutive
  // letter. acsiId is the physical ACSI ID tag stored in pun_info for
  // every owned slot — independent of the letter range.
  uint32_t driveNumber = (uint32_t)firstDrive;
  if (driveNumber < 2u || driveNumber >= ACSI_PUN_INFO_MAXUNITS) {
    DPRINTF("ACSI cannot announce logical drive %lu (out of C..P range)\n",
            (unsigned long)driveNumber);
    return;
  }

  bool haveFirstDrive = false;
  uint32_t lastDrive = 0u;

  for (uint8_t partitionIndex = 0; partitionIndex < usablePartitionCount;
       ++partitionIndex) {
    const AcsiPartitionEntry *partition = &usablePartitions[partitionIndex];
    AcsiFat16Geometry geometry = {0};
    AcsiTosBootSectorInfo tosInfo = {0};
    AcsiBPBData bpb = {0};
    AcsiTosDosStyle tosDosStyle = ACSI_TOSDOS_STYLE_NONE;
    uint32_t physicalStartSector = 0u;
    uint32_t logicalSectorCount = 0u;
    uint16_t logicalSectorSize = 0u;
    uint16_t logicalToPhysicalRatio = 0u;
    const char *viewName = "DOS";

    if (!partition->isFat16) {
      continue;
    }

    if (driveNumber >= ACSI_PUN_INFO_MAXUNITS) {
      DPRINTF("ACSI announced volume list reached drive P:, truncating.\n");
      break;
    }

    if (acsi_parse_fat16_geometry(context, partition, &geometry) != FR_OK) {
      // If the BPB is otherwise sane but its logical sector size is bigger
      // than our BCB pool can back (> 8192 bytes, e.g. Hatari 512 MB images
      // that use 16384-byte sectors), peek at the raw bytesPerSec so the
      // setup screen can explain the failure instead of showing an empty
      // partition list.
      BYTE probeSector[ACSI_IMAGE_SECTOR_SIZE] = {0};
      if (acsi_image_read_sectors(context, partition->firstLBA, 1, probeSector,
                                  sizeof(probeSector)) == FR_OK) {
        uint16_t probeBytesPerSec = acsiReadLe16(probeSector, 11);
        if (probeBytesPerSec > 8192u && probeBytesPerSec <= 65536u &&
            (probeBytesPerSec & (probeBytesPerSec - 1u)) == 0u &&
            probeBytesPerSec > acsiScanOversizedSectorSize) {
          acsiScanOversizedSectorSize = probeBytesPerSec;
        }
      }
      DPRINTF("ACSI drive %c skipped: invalid FAT16 geometry at LBA %lu\n",
              acsiDriveNumberToLetter(driveNumber),
              (unsigned long)partition->firstLBA);
      continue;
    }

    tosDosStyle =
        acsiDetectTosDosStyle(context, partition, &geometry, &tosInfo);
    physicalStartSector = partition->firstLBA;
    // geometry.totalSectors is already in logical units; match it with the
    // logical sector size so downstream sector-count math (announced size,
    // per-sector reads/writes) works when bytesPerSector > 512.
    logicalSectorCount = geometry.totalSectors;
    logicalSectorSize = geometry.bytesPerSector;
    logicalToPhysicalRatio =
        acsiGetLogicalToPhysicalRatio((uint32_t)geometry.bytesPerSector);

    if (tosDosStyle != ACSI_TOSDOS_STYLE_NONE &&
        acsiBuildTosBpbData((uint16_t)driveNumber, &tosInfo, &bpb)) {
      uint16_t ratio =
          acsiGetLogicalToPhysicalRatio((uint32_t)tosInfo.bytesPerSector);
      uint32_t physicalStart = partition->firstLBA + 1u;
      uint64_t physicalEnd =
          (uint64_t)physicalStart + (uint64_t)tosInfo.totalPhysicalSectors;
      uint64_t partitionEnd =
          (uint64_t)partition->firstLBA + (uint64_t)partition->sectorCount;
      if (ratio == 0u || physicalEnd > partitionEnd) {
        DPRINTF(
            "ACSI drive %c hybrid TOS view rejected: recsize=%u ratio=%lu "
            "logical=%u\n",
            acsiDriveNumberToLetter(driveNumber), (unsigned int)bpb.recsize,
            (unsigned long)ratio, (unsigned int)tosInfo.totalLogicalSectors);
        if (!acsiBuildBpbData((uint16_t)driveNumber, &geometry, &bpb)) {
          continue;
        }
      } else {
        physicalStartSector = physicalStart;
        logicalSectorCount = tosInfo.totalLogicalSectors;
        logicalSectorSize = tosInfo.bytesPerSector;
        logicalToPhysicalRatio = ratio;
        viewName = "TOS";
      }
    } else if (!acsiBuildBpbData((uint16_t)driveNumber, &geometry, &bpb)) {
      continue;
    }

    if (logicalToPhysicalRatio == 0u ||
        logicalSectorSize > ACSIEMUL_IMAGE_BUFFER_SIZE) {
      DPRINTF("ACSI drive %c skipped: unsupported logical sector size %u\n",
              acsiDriveNumberToLetter(driveNumber),
              (unsigned int)logicalSectorSize);
      continue;
    }

    acsiBpbData[driveNumber] = bpb;
    acsiBpbPointers[driveNumber] =
        ACSIEMUL_ST_BPB_DATA_BASE + (driveNumber * ACSI_BPB_SLOT_SIZE);
    acsiPunInfoUnits[driveNumber] = acsiId & 0x07u;
    acsiPunInfoStartSectors[driveNumber] = physicalStartSector;
    acsiPartitionSectorCounts[driveNumber] = logicalSectorCount;
    acsiLogicalSectorSizes[driveNumber] = logicalSectorSize;
    acsiLogicalToPhysicalRatios[driveNumber] = logicalToPhysicalRatio;
    acsiPartitionStyle[driveNumber] = (uint8_t)tosDosStyle;
    acsiPartitionViewIsTos[driveNumber] = (strcmp(viewName, "TOS") == 0);
    DPRINTF(
        "ACSI drive %c view=%s start=%lu recsize=%u logical_sectors=%lu "
        "ratio=%u\n",
        acsiDriveNumberToLetter(driveNumber), viewName,
        (unsigned long)physicalStartSector, (unsigned int)logicalSectorSize,
        (unsigned long)logicalSectorCount,
        (unsigned int)logicalToPhysicalRatio);

    if (!haveFirstDrive) {
      acsiFirstVolumeDrive = driveNumber;
      haveFirstDrive = true;
    }
    lastDrive = driveNumber;
    driveNumber++;
  }

  if (!haveFirstDrive) {
    return;
  }

  acsiSetVolumeDriveRange(acsiFirstVolumeDrive, lastDrive);
  acsiPunInfoPuns = (uint16_t)(lastDrive + 1u);
  acsiPunInfoValid = true;
}

static void acsiRefreshVolumeDriveRange(uint8_t acsiId, uint8_t firstDrive) {
  AcsiImageContext context = {0};
  AcsiPartitionEntry usablePartitions[ACSI_MAX_PARTITIONS] = {0};
  uint8_t usablePartitionCount = 0;

  acsiResetVolumeDriveRange();
  acsiResetPunInfoCache();
  acsiResetBpbCache();

  if (acsiImagePath[0] == '\0') {
    return;
  }

  FRESULT fr = acsi_image_open(&context, acsiImagePath, true);
  if (fr != FR_OK) {
    DPRINTF("ACSI volume range: cannot open image (%d)\n", (int)fr);
    return;
  }

  fr = acsi_enumerate_partitions(&context, usablePartitions,
                                 &usablePartitionCount);
  if (fr != FR_OK) {
    acsi_image_close(&context);
    DPRINTF("ACSI volume range: cannot enumerate partitions (%d)\n", (int)fr);
    return;
  }

  acsiBuildAnnouncedVolumeData(&context, acsiId, firstDrive,
                               usablePartitions, usablePartitionCount);
  acsi_image_close(&context);
  if (!acsiVolumeDriveRangeValid) {
    DPRINTF("ACSI volume range: no FAT16 volumes with valid BPB found\n");
    return;
  }

  DPRINTF("ACSI volume range: first=%c (%lu) last=%c (%lu) count=%u\n",
          acsiDriveNumberToLetter(acsiFirstVolumeDrive),
          (unsigned long)acsiFirstVolumeDrive,
          acsiDriveNumberToLetter(acsiLastVolumeDrive),
          (unsigned long)acsiLastVolumeDrive,
          (unsigned int)(acsiLastVolumeDrive - acsiFirstVolumeDrive + 1u));
}

static void acsiResetHookTraceState(void) {
  acsiLastLoggedHooksInstalled = 0;
  acsiLastLoggedOldHdvInit = 0;
  acsiLastLoggedOldHdvBpb = 0;
  acsiLastLoggedOldHdvRw = 0;
  acsiLastLoggedOldHdvBoot = 0;
  acsiLastLoggedOldHdvMediach = 0;
}

static void acsiTraceHookInstallSummary(void) {
  uint32_t hooksInstalled = READ_AND_SWAP_LONGWORD(
      memorySharedAddress,
      ACSIEMUL_SHARED_VARIABLES_OFFSET + (ACSIEMUL_SVAR_HOOKS_INSTALLED * 4u));

  if (hooksInstalled == 0u) {
    return;
  }

  uint32_t oldHdvInit = READ_AND_SWAP_LONGWORD(
      memorySharedAddress,
      ACSIEMUL_SHARED_VARIABLES_OFFSET + (ACSIEMUL_SVAR_OLD_HDV_INIT * 4u));
  uint32_t oldHdvBpb = READ_AND_SWAP_LONGWORD(
      memorySharedAddress,
      ACSIEMUL_SHARED_VARIABLES_OFFSET + (ACSIEMUL_SVAR_OLD_HDV_BPB * 4u));
  uint32_t oldHdvRw = READ_AND_SWAP_LONGWORD(
      memorySharedAddress,
      ACSIEMUL_SHARED_VARIABLES_OFFSET + (ACSIEMUL_SVAR_OLD_HDV_RW * 4u));
  uint32_t oldHdvBoot = READ_AND_SWAP_LONGWORD(
      memorySharedAddress,
      ACSIEMUL_SHARED_VARIABLES_OFFSET + (ACSIEMUL_SVAR_OLD_HDV_BOOT * 4u));
  uint32_t oldHdvMediach = READ_AND_SWAP_LONGWORD(
      memorySharedAddress,
      ACSIEMUL_SHARED_VARIABLES_OFFSET + (ACSIEMUL_SVAR_OLD_HDV_MEDIACH * 4u));

  if (hooksInstalled == acsiLastLoggedHooksInstalled &&
      oldHdvInit == acsiLastLoggedOldHdvInit &&
      oldHdvBpb == acsiLastLoggedOldHdvBpb &&
      oldHdvRw == acsiLastLoggedOldHdvRw &&
      oldHdvBoot == acsiLastLoggedOldHdvBoot &&
      oldHdvMediach == acsiLastLoggedOldHdvMediach) {
    return;
  }

  acsiLastLoggedHooksInstalled = hooksInstalled;
  acsiLastLoggedOldHdvInit = oldHdvInit;
  acsiLastLoggedOldHdvBpb = oldHdvBpb;
  acsiLastLoggedOldHdvRw = oldHdvRw;
  acsiLastLoggedOldHdvBoot = oldHdvBoot;
  acsiLastLoggedOldHdvMediach = oldHdvMediach;

  DPRINTF(
      "ACSI hooks installed: init=%08lX bpb=%08lX rw=%08lX boot=%08lX "
      "mediach=%08lX\n",
      (unsigned long)oldHdvInit, (unsigned long)oldHdvBpb,
      (unsigned long)oldHdvRw, (unsigned long)oldHdvBoot,
      (unsigned long)oldHdvMediach);
}

static void acsiTraceHookDrive(uint32_t hookId, uint32_t driveNumber) {
  if (driveNumber <= 25u) {
    DPRINTF("ACSI hook %s drive=%c (%lu)\n", acsiDebugHookName(hookId),
            acsiDriveNumberToLetter(driveNumber), (unsigned long)driveNumber);
    return;
  }

  DPRINTF("ACSI hook %s drive=? (%08lX)\n", acsiDebugHookName(hookId),
          (unsigned long)driveNumber);
}

static void acsiTraceDrvbits(uint32_t drvbitsValue) {
  DPRINTF("ACSI hdv_init drvbits=%08lX\n", (unsigned long)drvbitsValue);
}

static void acsiTraceDebugValue(uint32_t hookId, uint32_t value) {
  if ((hookId == ACSIEMUL_DEBUG_FIRST_VOLUME_DRIVE) ||
      (hookId == ACSIEMUL_DEBUG_LAST_VOLUME_DRIVE)) {
    DPRINTF("ACSI hdv_init %s=%c (%lu)\n", acsiDebugHookName(hookId),
            acsiDriveNumberToLetter(value), (unsigned long)value);
    return;
  }

  DPRINTF("ACSI hdv_init %s=%08lX\n", acsiDebugHookName(hookId),
          (unsigned long)value);
}

static const char *acsiMediaChangeStatusName(uint16_t status) {
  switch (status) {
    case 0:
      return "MED_NOCHANGE";
    case 1:
      return "MED_UNKNOWN";
    case 2:
      return "MED_CHANGED";
    default:
      return "MED_INVALID";
  }
}

static void acsiTraceMediachStatus(uint16_t driveNumber, uint16_t status) {
  DPRINTF("ACSI hdv_mediach drive=%c (%u) status=%s (%u)\n",
          acsiDriveNumberToLetter(driveNumber), (unsigned int)driveNumber,
          acsiMediaChangeStatusName(status), (unsigned int)status);
}

static void acsiTraceBpbData(uint32_t driveAndPhase, uint32_t value1,
                             uint32_t value2) {
  uint16_t driveNumber = (uint16_t)(driveAndPhase >> 16);
  uint16_t phase = (uint16_t)(driveAndPhase & 0xFFFFu);
  uint16_t fieldA = (uint16_t)(value1 >> 16);
  uint16_t fieldB = (uint16_t)(value1 & 0xFFFFu);
  uint16_t fieldC = (uint16_t)(value2 >> 16);
  uint16_t fieldD = (uint16_t)(value2 & 0xFFFFu);

  switch (phase) {
    case 0:
      DPRINTF("ACSI hdv_bpb drive=%c (%u) ptr=%08lX recsize=%u clsiz=%u\n",
              acsiDriveNumberToLetter(driveNumber), (unsigned int)driveNumber,
              (unsigned long)value1, (unsigned int)fieldC,
              (unsigned int)fieldD);
      break;
    case 1:
      DPRINTF(
          "ACSI hdv_bpb geometry drive=%c (%u) clsizb=%u rdlen=%u fsiz=%u "
          "fatrec=%u\n",
          acsiDriveNumberToLetter(driveNumber), (unsigned int)driveNumber,
          (unsigned int)fieldA, (unsigned int)fieldB, (unsigned int)fieldC,
          (unsigned int)fieldD);
      break;
    case 2:
      DPRINTF(
          "ACSI hdv_bpb geometry drive=%c (%u) datrec=%u numcl=%u bflags=%u "
          "trackcnt=%u\n",
          acsiDriveNumberToLetter(driveNumber), (unsigned int)driveNumber,
          (unsigned int)fieldA, (unsigned int)fieldB, (unsigned int)fieldC,
          (unsigned int)fieldD);
      break;
    case 3:
      DPRINTF(
          "ACSI hdv_bpb geometry drive=%c (%u) sidecnt=%u secpcyl=%u "
          "secptrack=%u disk=%u\n",
          acsiDriveNumberToLetter(driveNumber), (unsigned int)driveNumber,
          (unsigned int)fieldA, (unsigned int)fieldB, (unsigned int)fieldC,
          (unsigned int)fieldD);
      break;
    default:
      DPRINTF(
          "ACSI hdv_bpb data drive=%c (%u) phase=%u value1=%08lX "
          "value2=%08lX\n",
          acsiDriveNumberToLetter(driveNumber), (unsigned int)driveNumber,
          (unsigned int)phase, (unsigned long)value1, (unsigned long)value2);
      break;
  }
}

static void acsiLoadConfiguredState(bool *enabledOut, uint8_t *acsiIdOut,
                                    uint8_t *firstDriveOut) {
  acsiImagePath[0] = '\0';
  acsiImagePathDirty = true;

  SettingsConfigEntry *image = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_ACSI_IMAGE);
  if ((image != NULL) && (image->value[0] != '\0')) {
    strncpy(acsiImagePath, image->value, MAX_FILENAME_LENGTH);
    acsiImagePath[MAX_FILENAME_LENGTH] = '\0';
  }

  if (enabledOut != NULL) {
    *enabledOut = acsiIsEnabledSetting();
  }
  if (acsiIdOut != NULL) {
    *acsiIdOut = acsiGetIdSetting();
  }
  if (firstDriveOut != NULL) {
    *firstDriveOut = acsiGetStartDriveSetting();
  }
}

static bool acsiIsEnabledSetting(void) {
  SettingsConfigEntry *enabled = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_ACSI_ENABLED);
  if ((enabled == NULL) || (enabled->value[0] == '\0')) {
    return false;
  }

  return (enabled->value[0] == 't') || (enabled->value[0] == 'T') ||
         (enabled->value[0] == 'y') || (enabled->value[0] == 'Y') ||
         (enabled->value[0] == '1');
}

static uint8_t acsiGetIdSetting(void) {
  SettingsConfigEntry *acsiId = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_ACSI_ID);
  if ((acsiId == NULL) || (acsiId->value[0] == '\0')) {
    return 7;
  }

  char *endptr = NULL;
  long value = strtol(acsiId->value, &endptr, 10);
  if ((acsiId->value == endptr) || (*endptr != '\0') || (value < 0) ||
      (value > 7)) {
    return 7;
  }

  return (uint8_t)value;
}

// Return the starting drive-letter slot (2='C' .. 15='P') for the first
// partition the RP announces. Defaults to 2 ('C') when the setting is
// missing or malformed so existing installs keep their old behavior.
static uint8_t acsiGetStartDriveSetting(void) {
  SettingsConfigEntry *entry = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_ACSI_DRIVE);
  if ((entry == NULL) || (entry->value[0] == '\0')) {
    return 2u;  // 'C'
  }
  char c = entry->value[0];
  if (c >= 'a' && c <= 'z') {
    c = (char)(c - ('a' - 'A'));
  }
  if (c < 'C' || c > 'P') {
    DPRINTF("ACSI drive letter '%c' out of range [C..P]; falling back to C\n",
            c);
    return 2u;
  }
  return (uint8_t)(c - 'A');
}

static inline uint16_t acsiReadLe16(const BYTE *buffer, size_t offset) {
  return (uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1] << 8);
}

static inline uint32_t acsiReadLe32(const BYTE *buffer, size_t offset) {
  return (uint32_t)buffer[offset] | ((uint32_t)buffer[offset + 1] << 8) |
         ((uint32_t)buffer[offset + 2] << 16) |
         ((uint32_t)buffer[offset + 3] << 24);
}

static inline uint32_t acsiReadBe32(const BYTE *buffer, size_t offset) {
  return ((uint32_t)buffer[offset] << 24) |
         ((uint32_t)buffer[offset + 1] << 16) |
         ((uint32_t)buffer[offset + 2] << 8) | (uint32_t)buffer[offset + 3];
}

static bool acsiPartitionIsFat16Type(uint8_t partitionType) {
  return partitionType == 0x04 || partitionType == 0x06 ||
         partitionType == 0x0E;
}

static bool acsiPartitionIsExtendedType(uint8_t partitionType) {
  return partitionType == 0x05 || partitionType == 0x0F;
}

static const char *acsiPartitionTypeName(uint8_t partitionType) {
  switch (partitionType) {
    case 0x00:
      return "Unused";
    case 0x04:
      return "FAT16<32M";
    case 0x05:
      return "Extended";
    case 0x06:
      return "FAT16";
    case 0x0E:
      return "FAT16 LBA";
    case 0x0F:
      return "Extended LBA";
    default:
      return "Unsupported";
  }
}

static bool acsiBytesAreAsciiId(const BYTE *buffer, size_t offset,
                                const char *idText) {
  return buffer != NULL && idText != NULL && strlen(idText) == 3u &&
         buffer[offset] == (BYTE)idText[0] &&
         buffer[offset + 1] == (BYTE)idText[1] &&
         buffer[offset + 2] == (BYTE)idText[2];
}

static bool acsiBootSectorHasPpdriverOem(const BYTE *sector) {
  return acsiBytesAreAsciiId(sector, 3u, "PPG") && sector[6] == 'D' &&
         sector[7] == 'O' && sector[8] == 'D' && sector[9] == 'B' &&
         sector[10] == 'C';
}

static bool acsiIsTosStandardId(const BYTE *buffer, size_t offset) {
  return acsiBytesAreAsciiId(buffer, offset, "GEM") ||
         acsiBytesAreAsciiId(buffer, offset, "BGM");
}

static bool acsiIsTosExtendedId(const BYTE *buffer, size_t offset) {
  return acsiBytesAreAsciiId(buffer, offset, "XGM");
}

static bool acsiSectorHasHddriverTosDosMarkers(const BYTE *sector) {
  if (sector == NULL || sector[510] != 0x55 || sector[511] != 0xAA) {
    return false;
  }

  bool firstTosEntry = (sector[0x01DE] & 0x01u) != 0u &&
                       (acsiBytesAreAsciiId(sector, 0x01DFu, "GEM") ||
                        acsiBytesAreAsciiId(sector, 0x01DFu, "BGM")) &&
                       acsiReadBe32(sector, 0x01E2u) != 0u &&
                       acsiReadBe32(sector, 0x01E6u) != 0u;

  bool nextLinkEntry = (sector[0x01EA] == 0x01u || sector[0x01EA] == 0x81u) &&
                       acsiBytesAreAsciiId(sector, 0x01EBu, "XGM") &&
                       acsiReadBe32(sector, 0x01EEu) != 0u &&
                       acsiReadBe32(sector, 0x01F2u) != 0u;

  return firstTosEntry || nextLinkEntry;
}

static bool acsiImageHasHddriverTosDosMarkers(AcsiImageContext *context) {
  BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
  if (acsi_image_read_sectors(context, 0, 1, sector, sizeof(sector)) != FR_OK) {
    return false;
  }

  return acsiSectorHasHddriverTosDosMarkers(sector);
}

static FRESULT acsiParseTosBootSector(AcsiImageContext *context,
                                      uint32_t bootSectorLBA,
                                      AcsiTosBootSectorInfo *info,
                                      BYTE *sectorOut, size_t sectorOutSize) {
  if (context == NULL || info == NULL) {
    return FR_INVALID_PARAMETER;
  }

  BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
  FRESULT fr = acsi_image_read_sectors(context, bootSectorLBA, 1, sector,
                                       sizeof(sector));
  if (fr != FR_OK) {
    return fr;
  }

  uint16_t bytesPerSector = acsiReadLe16(sector, 11);
  uint8_t sectorsPerCluster = sector[13];
  uint16_t reservedSectorCount = acsiReadLe16(sector, 14);
  uint8_t fatCount = sector[16];
  uint16_t rootEntryCount = acsiReadLe16(sector, 17);
  uint16_t totalLogicalSectors = acsiReadLe16(sector, 19);
  uint8_t mediaDescriptor = sector[21];
  uint16_t sectorsPerFat = acsiReadLe16(sector, 22);
  uint16_t sectorsPerTrack = acsiReadLe16(sector, 24);
  uint16_t headCount = acsiReadLe16(sector, 26);

  if (bytesPerSector < ACSI_IMAGE_SECTOR_SIZE ||
      (bytesPerSector % ACSI_IMAGE_SECTOR_SIZE) != 0u ||
      sectorsPerCluster == 0u ||
      (sectorsPerCluster & (sectorsPerCluster - 1u)) != 0u ||
      reservedSectorCount == 0u || fatCount == 0u || rootEntryCount == 0u ||
      totalLogicalSectors == 0u || sectorsPerFat == 0u) {
    return FR_INVALID_OBJECT;
  }

  uint32_t logicalToPhysicalRatio =
      (uint32_t)bytesPerSector / ACSI_IMAGE_SECTOR_SIZE;
  if (logicalToPhysicalRatio == 0u) {
    return FR_INVALID_OBJECT;
  }

  uint32_t rootDirLogicalSectorCount =
      (((uint32_t)rootEntryCount * ACSI_ROOT_DIR_ENTRY_SIZE) +
       (uint32_t)bytesPerSector - 1u) /
      (uint32_t)bytesPerSector;

  AcsiTosBootSectorInfo tmp = {0};
  tmp.bytesPerSector = bytesPerSector;
  tmp.sectorsPerCluster = sectorsPerCluster;
  tmp.reservedSectorCount = reservedSectorCount;
  tmp.fatCount = fatCount;
  tmp.rootEntryCount = rootEntryCount;
  tmp.totalLogicalSectors = totalLogicalSectors;
  tmp.sectorsPerFat = sectorsPerFat;
  tmp.mediaDescriptor = mediaDescriptor;
  tmp.sectorsPerTrack = sectorsPerTrack;
  tmp.headCount = headCount;
  tmp.fatStartLBA =
      bootSectorLBA + ((uint32_t)reservedSectorCount * logicalToPhysicalRatio);
  tmp.rootDirStartLBA =
      tmp.fatStartLBA +
      ((uint32_t)fatCount * (uint32_t)sectorsPerFat * logicalToPhysicalRatio);
  tmp.rootDirSectorCount = rootDirLogicalSectorCount * logicalToPhysicalRatio;
  tmp.dataStartLBA = tmp.rootDirStartLBA + tmp.rootDirSectorCount;
  tmp.totalPhysicalSectors =
      (uint32_t)totalLogicalSectors * logicalToPhysicalRatio;

  *info = tmp;

  if (sectorOut != NULL && sectorOutSize >= sizeof(sector)) {
    memcpy(sectorOut, sector, sizeof(sector));
  }

  return FR_OK;
}

static AcsiTosDosStyle acsiDetectTosDosStyle(
    AcsiImageContext *context, const AcsiPartitionEntry *partition,
    const AcsiFat16Geometry *dosGeometry, AcsiTosBootSectorInfo *tosInfoOut) {
  if (context == NULL || partition == NULL || dosGeometry == NULL ||
      !partition->isPresent || !partition->isFat16) {
    return ACSI_TOSDOS_STYLE_NONE;
  }

  if ((uint64_t)partition->firstLBA + 1u >= context->totalSectors) {
    return ACSI_TOSDOS_STYLE_NONE;
  }

  AcsiTosBootSectorInfo tosInfo = {0};
  BYTE tosSector[ACSI_IMAGE_SECTOR_SIZE] = {0};
  if (acsiParseTosBootSector(context, partition->firstLBA + 1u, &tosInfo,
                             tosSector, sizeof(tosSector)) != FR_OK) {
    return ACSI_TOSDOS_STYLE_NONE;
  }

  if (dosGeometry->fatStartLBA != tosInfo.fatStartLBA ||
      dosGeometry->rootDirStartLBA != tosInfo.rootDirStartLBA ||
      dosGeometry->dataStartLBA != tosInfo.dataStartLBA) {
    return ACSI_TOSDOS_STYLE_NONE;
  }

  if (tosInfoOut != NULL) {
    *tosInfoOut = tosInfo;
  }

  if (acsiImageHasHddriverTosDosMarkers(context)) {
    return ACSI_TOSDOS_STYLE_HDDRIVER;
  }

  BYTE dosSector[ACSI_IMAGE_SECTOR_SIZE] = {0};
  if (acsi_image_read_sectors(context, partition->firstLBA, 1, dosSector,
                              sizeof(dosSector)) != FR_OK) {
    return ACSI_TOSDOS_STYLE_NONE;
  }

  if (acsiBootSectorHasPpdriverOem(dosSector) &&
      acsiBootSectorHasPpdriverOem(tosSector)) {
    return ACSI_TOSDOS_STYLE_PPDRIVER;
  }

  return ACSI_TOSDOS_STYLE_NONE;
}

static const char *acsiTosDosStyleName(AcsiTosDosStyle style) {
  switch (style) {
    case ACSI_TOSDOS_STYLE_PPDRIVER:
      return "PPDRIVER";
    case ACSI_TOSDOS_STYLE_HDDRIVER:
      return "HDDRIVER";
    case ACSI_TOSDOS_STYLE_NONE:
    default:
      return "None";
  }
}

static void acsiParsePartitionEntry(const BYTE *sector, size_t offset,
                                    uint32_t lbaBase,
                                    AcsiPartitionEntry *partition) {
  if (sector == NULL || partition == NULL) {
    return;
  }

  memset(partition, 0, sizeof(*partition));
  partition->bootIndicator = sector[offset];
  partition->partitionType = sector[offset + 4];
  partition->sectorCount = acsiReadLe32(sector, offset + 12);

  if (partition->partitionType == 0u || partition->sectorCount == 0u) {
    return;
  }

  uint64_t absoluteLBA =
      (uint64_t)lbaBase + (uint64_t)acsiReadLe32(sector, offset + 8);
  if (absoluteLBA > 0xFFFFFFFFu) {
    partition->partitionType = 0u;
    partition->sectorCount = 0u;
    return;
  }

  partition->firstLBA = (uint32_t)absoluteLBA;
  partition->isPresent = true;
  partition->isFat16 = acsiPartitionIsFat16Type(partition->partitionType);
  partition->isExtended = acsiPartitionIsExtendedType(partition->partitionType);
}

static bool acsiPartitionRangeIsValid(const AcsiImageContext *context,
                                      const AcsiPartitionEntry *partition) {
  if (context == NULL || partition == NULL || !partition->isPresent ||
      partition->sectorCount == 0u) {
    return false;
  }

  return (uint64_t)partition->firstLBA + (uint64_t)partition->sectorCount <=
         (uint64_t)context->totalSectors;
}

static FRESULT acsiAppendPartition(
    const AcsiPartitionEntry *partition,
    AcsiPartitionEntry partitions[ACSI_MAX_PARTITIONS],
    uint8_t *partitionCount) {
  if (partition == NULL || partitions == NULL || partitionCount == NULL) {
    return FR_INVALID_PARAMETER;
  }

  if (*partitionCount >= ACSI_MAX_PARTITIONS) {
    return FR_INVALID_OBJECT;
  }

  partitions[*partitionCount] = *partition;
  (*partitionCount)++;
  return FR_OK;
}

static FRESULT acsiAppendExtendedPartitions(
    AcsiImageContext *context, const AcsiPartitionEntry *extendedPartition,
    AcsiPartitionEntry partitions[ACSI_MAX_PARTITIONS],
    uint8_t *partitionCount) {
  if (context == NULL || extendedPartition == NULL || partitions == NULL ||
      partitionCount == NULL || !extendedPartition->isPresent ||
      !extendedPartition->isExtended) {
    return FR_INVALID_PARAMETER;
  }

  uint32_t extendedBaseLBA = extendedPartition->firstLBA;
  uint32_t currentEbrLBA = extendedBaseLBA;

  for (uint8_t linkIndex = 0; linkIndex < ACSI_MAX_PARTITIONS; ++linkIndex) {
    BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
    FRESULT fr = acsi_image_read_sectors(context, currentEbrLBA, 1, sector,
                                         sizeof(sector));
    if (fr != FR_OK) {
      return fr;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
      return FR_INVALID_OBJECT;
    }

    AcsiPartitionEntry logicalPartition = {0};
    AcsiPartitionEntry nextLink = {0};
    acsiParsePartitionEntry(sector, 446u, currentEbrLBA, &logicalPartition);
    acsiParsePartitionEntry(sector, 462u, extendedBaseLBA, &nextLink);

    if (logicalPartition.isPresent) {
      if (logicalPartition.isExtended ||
          !acsiPartitionRangeIsValid(context, &logicalPartition)) {
        return FR_INVALID_OBJECT;
      }

      fr = acsiAppendPartition(&logicalPartition, partitions, partitionCount);
      if (fr != FR_OK) {
        return fr;
      }
    }

    if (!nextLink.isPresent) {
      break;
    }

    if (!nextLink.isExtended ||
        !acsiPartitionRangeIsValid(context, &nextLink) ||
        nextLink.firstLBA == currentEbrLBA) {
      return FR_INVALID_OBJECT;
    }

    currentEbrLBA = nextLink.firstLBA;
  }

  return FR_OK;
}

// AHDI (Atari Hard Disk Interface) partition table. Four 12-byte entries
// packed into the root sector starting at 0x1C6. Each entry is:
//   +0: flag (bit 0 = existent, bit 7 = bootable)
//   +1..+3: 3-char ASCII id ("GEM"/"BGM" = FAT, "XGM" = extended link,
//           others like "RAW"/"F32"/"MIX" are not mounted by the emulator).
//   +4..+7: start LBA, big-endian
//   +8..+11: sector count, big-endian
enum {
  ACSI_AHDI_ENTRY_SIZE = 12,
  ACSI_AHDI_SLOT0_OFFSET = 0x01C6,
  ACSI_AHDI_SLOT1_OFFSET = 0x01D2,
  ACSI_AHDI_SLOT2_OFFSET = 0x01DE,
  ACSI_AHDI_SLOT3_OFFSET = 0x01EA,
  ACSI_AHDI_FLAG_EXISTENT = 0x01,
};

static bool acsiAhdiIdIsFat(const BYTE *id3) {
  // GEM = partition ≤ 16 MB; BGM = big partition. Both are FAT-style volumes
  // we can try to mount.
  return (id3[0] == 'G' && id3[1] == 'E' && id3[2] == 'M') ||
         (id3[0] == 'B' && id3[1] == 'G' && id3[2] == 'M');
}

static bool acsiAhdiIdIsExtended(const BYTE *id3) {
  return id3[0] == 'X' && id3[1] == 'G' && id3[2] == 'M';
}

static void acsiParseAhdiEntry(const BYTE *sector, size_t offset,
                               uint32_t lbaBase,
                               AcsiPartitionEntry *partition) {
  if (sector == NULL || partition == NULL) {
    return;
  }

  memset(partition, 0, sizeof(*partition));

  uint8_t flag = sector[offset];
  if ((flag & ACSI_AHDI_FLAG_EXISTENT) == 0u) {
    return;
  }

  const BYTE *id3 = sector + offset + 1u;
  bool isFat = acsiAhdiIdIsFat(id3);
  bool isExtended = acsiAhdiIdIsExtended(id3);
  if (!isFat && !isExtended) {
    // Unknown id (RAW/F32/MIX/etc.) — not mountable by this driver.
    return;
  }

  uint32_t start = acsiReadBe32(sector, offset + 4u);
  uint32_t size = acsiReadBe32(sector, offset + 8u);
  if (size == 0u) {
    return;
  }

  uint64_t absoluteLBA = (uint64_t)lbaBase + (uint64_t)start;
  if (absoluteLBA > 0xFFFFFFFFu) {
    return;
  }

  partition->bootIndicator = flag;
  // Synthesize an MBR-style type byte so the downstream code has something
  // meaningful in partitionType: FAT16 (>=32 MB) for BGM/GEM, MS extended
  // for XGM. The real partition-type info is carried by isFat16/isExtended.
  partition->partitionType = isExtended ? 0x05u : 0x06u;
  partition->firstLBA = (uint32_t)absoluteLBA;
  partition->sectorCount = size;
  partition->isPresent = true;
  partition->isFat16 = isFat;
  partition->isExtended = isExtended;
}

static FRESULT acsiAppendAhdiExtendedPartitions(
    AcsiImageContext *context, const AcsiPartitionEntry *extendedPartition,
    AcsiPartitionEntry partitions[ACSI_MAX_PARTITIONS],
    uint8_t *partitionCount) {
  if (context == NULL || extendedPartition == NULL || partitions == NULL ||
      partitionCount == NULL || !extendedPartition->isPresent ||
      !extendedPartition->isExtended) {
    return FR_INVALID_PARAMETER;
  }

  uint32_t xgmBaseLBA = extendedPartition->firstLBA;
  uint32_t currentDescriptorLBA = xgmBaseLBA;

  for (uint8_t linkIndex = 0; linkIndex < ACSI_MAX_PARTITIONS; ++linkIndex) {
    BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
    FRESULT fr = acsi_image_read_sectors(context, currentDescriptorLBA, 1,
                                         sector, sizeof(sector));
    if (fr != FR_OK) {
      return fr;
    }

    // Sub-descriptor layout: slot 0 holds the actual partition (its start
    // is RELATIVE to this descriptor's LBA); slot 1 holds the XGM link to
    // the next descriptor (its start is RELATIVE to the XGM base — the
    // root's original XGM start LBA). Other slots are ignored.
    AcsiPartitionEntry logicalPartition = {0};
    AcsiPartitionEntry nextLink = {0};
    acsiParseAhdiEntry(sector, ACSI_AHDI_SLOT0_OFFSET, currentDescriptorLBA,
                       &logicalPartition);
    acsiParseAhdiEntry(sector, ACSI_AHDI_SLOT1_OFFSET, xgmBaseLBA, &nextLink);

    if (logicalPartition.isPresent) {
      if (logicalPartition.isExtended ||
          !acsiPartitionRangeIsValid(context, &logicalPartition)) {
        return FR_INVALID_OBJECT;
      }
      fr = acsiAppendPartition(&logicalPartition, partitions, partitionCount);
      if (fr != FR_OK) {
        return fr;
      }
    }

    if (!nextLink.isPresent) {
      break;
    }

    if (!nextLink.isExtended ||
        !acsiPartitionRangeIsValid(context, &nextLink) ||
        nextLink.firstLBA == currentDescriptorLBA) {
      return FR_INVALID_OBJECT;
    }

    currentDescriptorLBA = nextLink.firstLBA;
  }

  return FR_OK;
}

static void acsiFormat83Name(const BYTE *entry, char *output,
                             size_t outputSize) {
  size_t out = 0;

  if (output == NULL || outputSize == 0) {
    return;
  }

  output[0] = '\0';

  for (size_t i = 0; i < 8 && out + 1 < outputSize; ++i) {
    char chr = (char)entry[i];
    if (chr == ' ') {
      break;
    }
    output[out++] = chr;
  }

  bool hasExtension = false;
  for (size_t i = 8; i < 11; ++i) {
    if (entry[i] != ' ') {
      hasExtension = true;
      break;
    }
  }

  if (hasExtension && out + 1 < outputSize) {
    output[out++] = '.';
    for (size_t i = 8; i < 11 && out + 1 < outputSize; ++i) {
      char chr = (char)entry[i];
      if (chr == ' ') {
        break;
      }
      output[out++] = chr;
    }
  }

  output[out] = '\0';
}

static void acsiFormatAttributes(uint8_t attributes, char *output,
                                 size_t outputSize) {
  int written = snprintf(
      output, outputSize, "%c%c%c%c%c%c", (attributes & 0x01u) ? 'R' : '-',
      (attributes & 0x02u) ? 'H' : '-', (attributes & 0x04u) ? 'S' : '-',
      (attributes & 0x08u) ? 'V' : '-', (attributes & 0x10u) ? 'D' : '-',
      (attributes & 0x20u) ? 'A' : '-');
  if (written < 0 && outputSize > 0) {
    output[0] = '\0';
  }
}

typedef enum {
  ACSI_FAT16_DIR_ENTRY_SKIP = 0,
  ACSI_FAT16_DIR_ENTRY_VALID,
  ACSI_FAT16_DIR_ENTRY_END,
} AcsiFat16DirEntryState;

typedef struct {
  char name[20];
  uint8_t attributes;
  uint16_t firstCluster;
  uint32_t fileSize;
} AcsiFat16DirectoryEntry;

typedef FRESULT (*AcsiFat16DirectoryVisitor)(
    const AcsiFat16DirectoryEntry *entry, bool *stop, void *context);

typedef struct {
  const char *label;
  uint32_t printedEntries;
  uint32_t totalEntries;
  bool truncated;
} AcsiFat16DirectoryListContext;

typedef struct {
  const char *name;
  AcsiFat16DirectoryEntry *entry;
  bool found;
} AcsiFat16DirectoryFindContext;

static AcsiFat16DirEntryState acsiFat16DecodeDirectoryEntry(
    const BYTE *entry, AcsiFat16DirectoryEntry *decodedEntry) {
  if (entry == NULL || decodedEntry == NULL) {
    return ACSI_FAT16_DIR_ENTRY_SKIP;
  }

  uint8_t firstByte = entry[0];
  uint8_t attributes = entry[11];

  if (firstByte == 0x00u) {
    return ACSI_FAT16_DIR_ENTRY_END;
  }

  if (firstByte == 0xE5u || attributes == 0x0Fu) {
    return ACSI_FAT16_DIR_ENTRY_SKIP;
  }

  memset(decodedEntry, 0, sizeof(*decodedEntry));
  acsiFormat83Name(entry, decodedEntry->name, sizeof(decodedEntry->name));
  decodedEntry->attributes = attributes;
  decodedEntry->firstCluster = acsiReadLe16(entry, 26);
  decodedEntry->fileSize = acsiReadLe32(entry, 28);
  return ACSI_FAT16_DIR_ENTRY_VALID;
}

static FRESULT acsiFat16ClusterToLba(const AcsiFat16Geometry *geometry,
                                     uint16_t cluster,
                                     uint32_t *clusterStartLba) {
  if (geometry == NULL || clusterStartLba == NULL || cluster < 2u ||
      cluster >= (geometry->clusterCount + 2u)) {
    return FR_INVALID_PARAMETER;
  }

  uint64_t lba =
      (uint64_t)geometry->dataStartLBA +
      (((uint64_t)cluster - 2u) * (uint64_t)geometry->sectorsPerCluster);
  if (lba > 0xFFFFFFFFu) {
    return FR_INVALID_OBJECT;
  }

  *clusterStartLba = (uint32_t)lba;
  return FR_OK;
}

static FRESULT acsiFat16ReadClusterLink(AcsiImageContext *context,
                                        const AcsiFat16Geometry *geometry,
                                        uint16_t cluster,
                                        uint16_t *nextCluster) {
  if (context == NULL || geometry == NULL || nextCluster == NULL ||
      cluster < 2u) {
    return FR_INVALID_PARAMETER;
  }

  uint32_t fatOffset = (uint32_t)cluster * 2u;
  uint32_t fatSectorIndex = fatOffset / ACSI_IMAGE_SECTOR_SIZE;
  uint32_t fatSectorOffset = fatOffset % ACSI_IMAGE_SECTOR_SIZE;
  BYTE fatSector[ACSI_IMAGE_SECTOR_SIZE] = {0};

  FRESULT fr =
      acsi_image_read_sectors(context, geometry->fatStartLBA + fatSectorIndex,
                              1, fatSector, sizeof(fatSector));
  if (fr != FR_OK) {
    return fr;
  }

  if (fatSectorOffset < (ACSI_IMAGE_SECTOR_SIZE - 1u)) {
    *nextCluster = acsiReadLe16(fatSector, fatSectorOffset);
    return FR_OK;
  }

  BYTE nextFatSector[ACSI_IMAGE_SECTOR_SIZE] = {0};
  fr = acsi_image_read_sectors(context,
                               geometry->fatStartLBA + fatSectorIndex + 1u, 1,
                               nextFatSector, sizeof(nextFatSector));
  if (fr != FR_OK) {
    return fr;
  }

  *nextCluster =
      (uint16_t)fatSector[fatSectorOffset] | ((uint16_t)nextFatSector[0] << 8);
  return FR_OK;
}

static FRESULT acsiFat16VisitSectorEntries(const BYTE *sector,
                                           AcsiFat16DirectoryVisitor visitor,
                                           void *context,
                                           bool *reachedDirectoryEnd,
                                           bool *stopRequested) {
  if (sector == NULL || visitor == NULL || reachedDirectoryEnd == NULL ||
      stopRequested == NULL) {
    return FR_INVALID_PARAMETER;
  }

  *reachedDirectoryEnd = false;
  *stopRequested = false;

  for (size_t offset = 0; offset < ACSI_IMAGE_SECTOR_SIZE;
       offset += ACSI_ROOT_DIR_ENTRY_SIZE) {
    AcsiFat16DirectoryEntry entry = {0};
    AcsiFat16DirEntryState state =
        acsiFat16DecodeDirectoryEntry(&sector[offset], &entry);

    if (state == ACSI_FAT16_DIR_ENTRY_END) {
      *reachedDirectoryEnd = true;
      return FR_OK;
    }

    if (state != ACSI_FAT16_DIR_ENTRY_VALID) {
      continue;
    }

    bool stop = false;
    FRESULT fr = visitor(&entry, &stop, context);
    if (fr != FR_OK) {
      return fr;
    }
    if (stop) {
      *stopRequested = true;
      return FR_OK;
    }
  }

  return FR_OK;
}

static FRESULT acsiFat16ScanDirectory(AcsiImageContext *context,
                                      const AcsiFat16Geometry *geometry,
                                      bool isRoot, uint16_t startCluster,
                                      AcsiFat16DirectoryVisitor visitor,
                                      void *visitorContext) {
  if (context == NULL || geometry == NULL || visitor == NULL) {
    return FR_INVALID_PARAMETER;
  }

  BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};

  if (isRoot) {
    for (uint32_t sectorIndex = 0; sectorIndex < geometry->rootDirSectorCount;
         ++sectorIndex) {
      FRESULT fr = acsi_image_read_sectors(
          context, geometry->rootDirStartLBA + sectorIndex, 1, sector,
          sizeof(sector));
      if (fr != FR_OK) {
        return fr;
      }

      bool reachedDirectoryEnd = false;
      bool stopRequested = false;
      fr = acsiFat16VisitSectorEntries(sector, visitor, visitorContext,
                                       &reachedDirectoryEnd, &stopRequested);
      if (fr != FR_OK || reachedDirectoryEnd || stopRequested) {
        return fr;
      }
    }

    return FR_OK;
  }

  if (startCluster < 2u) {
    return FR_INVALID_PARAMETER;
  }

  uint16_t currentCluster = startCluster;
  uint32_t visitedClusters = 0;

  while (true) {
    if (visitedClusters++ >= geometry->clusterCount) {
      return FR_INVALID_OBJECT;
    }

    uint32_t clusterStartLba = 0;
    FRESULT fr =
        acsiFat16ClusterToLba(geometry, currentCluster, &clusterStartLba);
    if (fr != FR_OK) {
      return fr;
    }

    for (uint32_t sectorIndex = 0; sectorIndex < geometry->sectorsPerCluster;
         ++sectorIndex) {
      fr = acsi_image_read_sectors(context, clusterStartLba + sectorIndex, 1,
                                   sector, sizeof(sector));
      if (fr != FR_OK) {
        return fr;
      }

      bool reachedDirectoryEnd = false;
      bool stopRequested = false;
      fr = acsiFat16VisitSectorEntries(sector, visitor, visitorContext,
                                       &reachedDirectoryEnd, &stopRequested);
      if (fr != FR_OK || reachedDirectoryEnd || stopRequested) {
        return fr;
      }
    }

    uint16_t nextCluster = 0;
    fr = acsiFat16ReadClusterLink(context, geometry, currentCluster,
                                  &nextCluster);
    if (fr != FR_OK) {
      return fr;
    }

    if (nextCluster >= 0xFFF8u) {
      return FR_OK;
    }

    if (nextCluster == 0xFFF7u || nextCluster < 2u) {
      return FR_INVALID_OBJECT;
    }

    currentCluster = nextCluster;
  }
}

static FRESULT acsiFat16ListDirectoryVisitor(
    const AcsiFat16DirectoryEntry *entry, bool *stop, void *context) {
  if (entry == NULL || stop == NULL || context == NULL) {
    return FR_INVALID_PARAMETER;
  }

  AcsiFat16DirectoryListContext *listContext =
      (AcsiFat16DirectoryListContext *)context;
  listContext->totalEntries++;

  if (listContext->printedEntries >= ACSI_TEST_ROOT_ENTRY_LIMIT) {
    listContext->truncated = true;
    *stop = true;
    return FR_OK;
  }

  char attrText[8];
  acsiFormatAttributes(entry->attributes, attrText, sizeof(attrText));

  if ((entry->attributes & 0x08u) != 0u) {
    acsiTestLog("    VOL  %-12s attr=%s\n", entry->name, attrText);
  } else if ((entry->attributes & 0x10u) != 0u) {
    acsiTestLog("    DIR  %-12s cl=%u attr=%s\n", entry->name,
                (unsigned int)entry->firstCluster, attrText);
  } else {
    acsiTestLog("    FILE %-12s cl=%u sz=%lu attr=%s\n", entry->name,
                (unsigned int)entry->firstCluster,
                (unsigned long)entry->fileSize, attrText);
  }

  listContext->printedEntries++;
  (void)stop;
  return FR_OK;
}

static FRESULT acsiFat16ListDirectory(AcsiImageContext *context,
                                      const AcsiFat16Geometry *geometry,
                                      bool isRoot, uint16_t startCluster,
                                      const char *label) {
  if (context == NULL || geometry == NULL) {
    return FR_INVALID_PARAMETER;
  }

  AcsiFat16DirectoryListContext listContext = {.label = label,
                                               .printedEntries = 0u,
                                               .totalEntries = 0u,
                                               .truncated = false};

  acsiTestLog("  %s:\n",
              (listContext.label != NULL) ? listContext.label : "directory");
  FRESULT fr =
      acsiFat16ScanDirectory(context, geometry, isRoot, startCluster,
                             acsiFat16ListDirectoryVisitor, &listContext);
  if (fr != FR_OK) {
    return fr;
  }

  if (listContext.totalEntries == 0u) {
    acsiTestLog("    <empty>\n");
  }
  if (listContext.truncated) {
    acsiTestLog("    ... truncated after %u entries\n",
                (unsigned int)ACSI_TEST_ROOT_ENTRY_LIMIT);
  }

  return FR_OK;
}

static FRESULT acsiFat16FindDirectoryVisitor(
    const AcsiFat16DirectoryEntry *entry, bool *stop, void *context) {
  if (entry == NULL || stop == NULL || context == NULL) {
    return FR_INVALID_PARAMETER;
  }

  AcsiFat16DirectoryFindContext *findContext =
      (AcsiFat16DirectoryFindContext *)context;
  if (findContext->name != NULL &&
      strcmp(entry->name, findContext->name) == 0) {
    if (findContext->entry != NULL) {
      *findContext->entry = *entry;
    }
    findContext->found = true;
    *stop = true;
  }

  return FR_OK;
}

static FRESULT acsiFat16FindEntry(AcsiImageContext *context,
                                  const AcsiFat16Geometry *geometry,
                                  bool isRoot, uint16_t startCluster,
                                  const char *name,
                                  AcsiFat16DirectoryEntry *entry) {
  if (context == NULL || geometry == NULL || name == NULL || entry == NULL) {
    return FR_INVALID_PARAMETER;
  }

  AcsiFat16DirectoryFindContext findContext = {
      .name = name, .entry = entry, .found = false};
  memset(entry, 0, sizeof(*entry));

  FRESULT fr =
      acsiFat16ScanDirectory(context, geometry, isRoot, startCluster,
                             acsiFat16FindDirectoryVisitor, &findContext);
  if (fr != FR_OK) {
    return fr;
  }

  return findContext.found ? FR_OK : FR_NO_FILE;
}

// Build the fastseek cluster link map for an already-opened image.
// Sets context->file.cltbl on success so subsequent f_lseek() calls skip
// the linear FAT walk. Non-fatal on failure — the context is still usable
// via regular lseek; only the optimisation is lost.
static FRESULT acsiSetupFastseek(AcsiImageContext *context) {
  DWORD *tbl = (DWORD *)malloc(ACSI_IMAGE_CLTBL_INITIAL * sizeof(DWORD));
  if (tbl == NULL) {
    return FR_NOT_ENOUGH_CORE;
  }
  tbl[0] = (DWORD)ACSI_IMAGE_CLTBL_INITIAL;
  context->file.cltbl = tbl;

  FRESULT fr = f_lseek(&context->file, CREATE_LINKMAP);
  if (fr == FR_NOT_ENOUGH_CORE) {
    DWORD needed = tbl[0];  // FatFS writes the required size here.
    free(tbl);
    context->file.cltbl = NULL;
    if (needed == 0 || needed > ACSI_IMAGE_CLTBL_MAX) {
      DPRINTF("ACSI fastseek skipped: image too fragmented (%lu DWORDs > %u)\n",
              (unsigned long)needed, (unsigned)ACSI_IMAGE_CLTBL_MAX);
      return FR_NOT_ENOUGH_CORE;
    }
    tbl = (DWORD *)malloc((size_t)needed * sizeof(DWORD));
    if (tbl == NULL) {
      return FR_NOT_ENOUGH_CORE;
    }
    tbl[0] = needed;
    context->file.cltbl = tbl;
    fr = f_lseek(&context->file, CREATE_LINKMAP);
  }

  if (fr != FR_OK) {
    DPRINTF("ACSI fastseek setup failed (%d)\n", (int)fr);
    context->file.cltbl = NULL;
    free(tbl);
    return fr;
  }

  context->cltbl = tbl;
  context->cltblEntries = (size_t)tbl[0];
  DPRINTF("ACSI fastseek enabled: %u DWORDs (~%u fragments)\n",
          (unsigned)context->cltblEntries,
          (unsigned)((context->cltblEntries > 0u)
                         ? (context->cltblEntries - 1u) / 2u
                         : 0u));
  return FR_OK;
}

FRESULT acsi_image_open(AcsiImageContext *context, const char *imagePath,
                        bool readOnly) {
  if (context == NULL || imagePath == NULL || imagePath[0] == '\0') {
    return FR_INVALID_PARAMETER;
  }

  memset(context, 0, sizeof(*context));
  snprintf(context->imagePath, sizeof(context->imagePath), "%s", imagePath);

  BYTE mode = FA_OPEN_EXISTING | FA_READ;
  if (!readOnly) {
    mode |= FA_WRITE;
  }

  FRESULT fr = f_open(&context->file, context->imagePath, mode);
  if (fr != FR_OK) {
    return fr;
  }

  context->imageSizeBytes = f_size(&context->file);
  if (context->imageSizeBytes < (FSIZE_t)ACSI_IMAGE_SECTOR_SIZE) {
    acsi_image_close(context);
    return FR_INVALID_OBJECT;
  }

  uint64_t totalSectors64 =
      ((uint64_t)context->imageSizeBytes) / ACSI_IMAGE_SECTOR_SIZE;
  if (totalSectors64 > 0xFFFFFFFFu) {
    acsi_image_close(context);
    return FR_INVALID_OBJECT;
  }

  context->totalSectors = (uint32_t)totalSectors64;
  context->readOnly = readOnly;
  context->isOpen = true;

  // Best-effort fastseek setup. Failure is non-fatal: we fall back to
  // linear-walk lseek automatically when context->file.cltbl is NULL.
  (void)acsiSetupFastseek(context);

  return FR_OK;
}

void acsi_image_close(AcsiImageContext *context) {
  if (context == NULL) {
    return;
  }

  if (context->isOpen) {
    (void)f_close(&context->file);
  }

  if (context->cltbl != NULL) {
    free(context->cltbl);
    context->cltbl = NULL;
    context->cltblEntries = 0;
  }

  memset(&context->file, 0, sizeof(context->file));
  context->imageSizeBytes = 0;
  context->totalSectors = 0;
  context->isOpen = false;
  context->readOnly = false;
}

FRESULT __not_in_flash_func(acsi_image_read_sectors)(AcsiImageContext *context,
                                                     uint32_t lba,
                                                     uint16_t sectorCount,
                                                     void *buffer,
                                                     size_t bufferSize) {
  if (context == NULL || buffer == NULL || !context->isOpen ||
      sectorCount == 0) {
    return FR_INVALID_PARAMETER;
  }

  size_t bytesToRead = (size_t)sectorCount * ACSI_IMAGE_SECTOR_SIZE;
  if (bufferSize < bytesToRead) {
    return FR_INVALID_PARAMETER;
  }

  if (lba >= context->totalSectors ||
      (uint64_t)lba + (uint64_t)sectorCount > context->totalSectors) {
    return FR_INVALID_PARAMETER;
  }

  FSIZE_t offset = (FSIZE_t)lba * (FSIZE_t)ACSI_IMAGE_SECTOR_SIZE;
  FRESULT fr = f_lseek(&context->file, offset);
  if (fr != FR_OK) {
    return fr;
  }

  unsigned int bytesRead = 0;
  fr = f_read(&context->file, buffer, (UINT)bytesToRead, &bytesRead);
  if (fr != FR_OK) {
    return fr;
  }

  return (bytesRead == (UINT)bytesToRead) ? FR_OK : FR_INT_ERR;
}

FRESULT __not_in_flash_func(acsi_image_write_sectors)(
    AcsiImageContext *context, uint32_t lba, uint16_t sectorCount,
    const void *buffer, size_t bufferSize) {
  if (context == NULL || buffer == NULL || !context->isOpen ||
      sectorCount == 0) {
    return FR_INVALID_PARAMETER;
  }

  if (context->readOnly) {
    return FR_DENIED;
  }

  size_t bytesToWrite = (size_t)sectorCount * ACSI_IMAGE_SECTOR_SIZE;
  if (bufferSize < bytesToWrite) {
    return FR_INVALID_PARAMETER;
  }

  if (lba >= context->totalSectors ||
      (uint64_t)lba + (uint64_t)sectorCount > context->totalSectors) {
    return FR_INVALID_PARAMETER;
  }

  FSIZE_t offset = (FSIZE_t)lba * (FSIZE_t)ACSI_IMAGE_SECTOR_SIZE;
  FRESULT fr = f_lseek(&context->file, offset);
  if (fr != FR_OK) {
    return fr;
  }

  unsigned int bytesWritten = 0;
  fr = f_write(&context->file, buffer, (UINT)bytesToWrite, &bytesWritten);
  if (fr != FR_OK) {
    return fr;
  }
  if (bytesWritten != (UINT)bytesToWrite) {
    return FR_INT_ERR;
  }

  // f_sync is deferred to acsi_tick() so that bursts of writes pay the
  // directory-update cost only once per idle window (~1 s) instead of once
  // per sector. Data sits in FatFS's window buffer in the meantime; a power
  // yank before the next tick costs the last ≤1 s of writes.
  return FR_OK;
}

FRESULT acsi_parse_mbr(AcsiImageContext *context,
                       AcsiPartitionEntry partitions[ACSI_PARTITION_COUNT],
                       uint8_t *partitionCountOut) {
  if (context == NULL || partitions == NULL) {
    return FR_INVALID_PARAMETER;
  }

  BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
  FRESULT fr = acsi_image_read_sectors(context, 0, 1, sector, sizeof(sector));
  if (fr != FR_OK) {
    return fr;
  }

  if (sector[510] != 0x55 || sector[511] != 0xAA) {
    return FR_INVALID_OBJECT;
  }

  memset(partitions, 0, sizeof(AcsiPartitionEntry) * ACSI_PARTITION_COUNT);

  uint8_t foundPartitions = 0;
  for (uint8_t index = 0; index < ACSI_PARTITION_COUNT; ++index) {
    size_t offset = 446u + ((size_t)index * 16u);
    AcsiPartitionEntry *partition = &partitions[index];
    acsiParsePartitionEntry(sector, offset, 0u, partition);

    if (partition->isPresent) {
      foundPartitions++;
    }
  }

  if (partitionCountOut != NULL) {
    *partitionCountOut = foundPartitions;
  }

  return FR_OK;
}

// Scan the root sector as an Atari AHDI partition table. Walks all four
// AHDI slots at 0x1C6/0x1D2/0x1DE/0x1EA, follows any XGM extended chains,
// and produces FAT-compatible AcsiPartitionEntry records. Used as a
// fallback when the image has no valid MBR (pure Atari-native images such
// as those written by ICD, HDDRIVER's AHDI-only mode, etc.).
static FRESULT acsiEnumerateAhdiPartitions(
    AcsiImageContext *context,
    AcsiPartitionEntry partitions[ACSI_MAX_PARTITIONS],
    uint8_t *partitionCountOut) {
  if (context == NULL || partitions == NULL) {
    return FR_INVALID_PARAMETER;
  }

  BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
  FRESULT fr = acsi_image_read_sectors(context, 0, 1, sector, sizeof(sector));
  if (fr != FR_OK) {
    return fr;
  }

  memset(partitions, 0, sizeof(AcsiPartitionEntry) * ACSI_MAX_PARTITIONS);
  uint8_t foundPartitions = 0;

  static const size_t slotOffsets[4] = {
      ACSI_AHDI_SLOT0_OFFSET, ACSI_AHDI_SLOT1_OFFSET, ACSI_AHDI_SLOT2_OFFSET,
      ACSI_AHDI_SLOT3_OFFSET};

  for (uint8_t index = 0; index < 4u; ++index) {
    AcsiPartitionEntry primary = {0};
    // Root AHDI entries carry absolute LBAs, so lbaBase = 0.
    acsiParseAhdiEntry(sector, slotOffsets[index], 0u, &primary);
    if (!primary.isPresent) {
      continue;
    }

    if (primary.isExtended) {
      fr = acsiAppendAhdiExtendedPartitions(context, &primary, partitions,
                                            &foundPartitions);
      if (fr != FR_OK) {
        return fr;
      }
      continue;
    }

    if (!acsiPartitionRangeIsValid(context, &primary)) {
      return FR_INVALID_OBJECT;
    }

    fr = acsiAppendPartition(&primary, partitions, &foundPartitions);
    if (fr != FR_OK) {
      return fr;
    }
  }

  if (partitionCountOut != NULL) {
    *partitionCountOut = foundPartitions;
  }

  return (foundPartitions > 0u) ? FR_OK : FR_INVALID_OBJECT;
}

FRESULT acsi_enumerate_partitions(
    AcsiImageContext *context,
    AcsiPartitionEntry partitions[ACSI_MAX_PARTITIONS],
    uint8_t *partitionCountOut) {
  if (context == NULL || partitions == NULL) {
    return FR_INVALID_PARAMETER;
  }

  AcsiPartitionEntry primaryPartitions[ACSI_PARTITION_COUNT] = {0};
  FRESULT fr = acsi_parse_mbr(context, primaryPartitions, NULL);
  if (fr != FR_OK) {
    // No usable MBR (signature missing or unreadable). Try the Atari AHDI
    // layout as a fallback — pure Atari-native images (ICD, HDDRIVER in
    // AHDI-only mode, etc.) don't carry a 0x55AA MBR signature.
    DPRINTF("ACSI MBR parse failed (%d); trying AHDI fallback.\n", (int)fr);
    return acsiEnumerateAhdiPartitions(context, partitions, partitionCountOut);
  }

  memset(partitions, 0, sizeof(AcsiPartitionEntry) * ACSI_MAX_PARTITIONS);

  uint8_t foundPartitions = 0;
  for (uint8_t index = 0; index < ACSI_PARTITION_COUNT; ++index) {
    const AcsiPartitionEntry *partition = &primaryPartitions[index];
    if (!partition->isPresent) {
      continue;
    }

    if (partition->isExtended) {
      fr = acsiAppendExtendedPartitions(context, partition, partitions,
                                        &foundPartitions);
      if (fr != FR_OK) {
        return fr;
      }
      continue;
    }

    if (!acsiPartitionRangeIsValid(context, partition)) {
      return FR_INVALID_OBJECT;
    }

    fr = acsiAppendPartition(partition, partitions, &foundPartitions);
    if (fr != FR_OK) {
      return fr;
    }
  }

  // If the MBR is valid but it describes no usable FAT partitions, give the
  // AHDI layout a chance — some hybrid images keep a dummy MBR header while
  // the real partition table lives in the AHDI slots.
  if (foundPartitions == 0u) {
    DPRINTF("ACSI MBR parsed but no FAT partitions; trying AHDI fallback.\n");
    return acsiEnumerateAhdiPartitions(context, partitions, partitionCountOut);
  }

  if (partitionCountOut != NULL) {
    *partitionCountOut = foundPartitions;
  }

  return FR_OK;
}

FRESULT acsi_parse_fat16_geometry(AcsiImageContext *context,
                                  const AcsiPartitionEntry *partition,
                                  AcsiFat16Geometry *geometry) {
  if (context == NULL || partition == NULL || geometry == NULL ||
      !partition->isPresent || !partition->isFat16) {
    return FR_INVALID_PARAMETER;
  }

  BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
  FRESULT fr = acsi_image_read_sectors(context, partition->firstLBA, 1, sector,
                                       sizeof(sector));
  if (fr != FR_OK) {
    return fr;
  }

  // Native Atari boot sectors often lack the 0x55AA signature at physical
  // byte 510 — the checksum lives at logical byte (bytesPerSector - 2) so
  // for a ≥1024-byte logical sector the physical byte 510 is inside the
  // boot code, not the signature. Only require 0x55AA when the logical
  // sector is the same as the physical one (512 bytes).
  uint16_t rawBytesPerSector = acsiReadLe16(sector, 11);
  if (rawBytesPerSector == ACSI_IMAGE_SECTOR_SIZE &&
      (sector[510] != 0x55 || sector[511] != 0xAA)) {
    return FR_INVALID_OBJECT;
  }

  AcsiFat16Geometry tmp = {0};
  tmp.bytesPerSector = rawBytesPerSector;
  tmp.sectorsPerCluster = sector[13];
  tmp.reservedSectorCount = acsiReadLe16(sector, 14);
  tmp.fatCount = sector[16];
  tmp.rootEntryCount = acsiReadLe16(sector, 17);
  tmp.mediaDescriptor = sector[21];
  tmp.sectorsPerFat = acsiReadLe16(sector, 22);
  tmp.sectorsPerTrack = acsiReadLe16(sector, 24);
  tmp.headCount = acsiReadLe16(sector, 26);
  tmp.hiddenSectors = acsiReadLe32(sector, 28);
  tmp.totalSectors = acsiReadLe16(sector, 19);
  if (tmp.totalSectors == 0) {
    tmp.totalSectors = acsiReadLe32(sector, 32);
  }

  // bytesPerSector must be a supported power-of-two multiple of the physical
  // 512-byte block size. The supported set is anchored by
  // acsiGetLogicalToPhysicalRatio; sizes outside that set (e.g. 16384) would
  // overflow the 22 KB image buffer and the 4 KB BCB pool slot size.
  uint16_t logicalToPhysicalRatio =
      acsiGetLogicalToPhysicalRatio((uint32_t)tmp.bytesPerSector);
  if (logicalToPhysicalRatio == 0u || tmp.sectorsPerCluster == 0 ||
      tmp.reservedSectorCount == 0 || tmp.fatCount == 0 ||
      tmp.rootEntryCount == 0 || tmp.totalSectors == 0 ||
      tmp.sectorsPerFat == 0) {
    return FR_INVALID_OBJECT;
  }

  tmp.rootDirSectorCount =
      (((uint32_t)tmp.rootEntryCount * ACSI_ROOT_DIR_ENTRY_SIZE) +
       (uint32_t)tmp.bytesPerSector - 1u) /
      (uint32_t)tmp.bytesPerSector;

  uint32_t nonDataSectors =
      (uint32_t)tmp.reservedSectorCount +
      ((uint32_t)tmp.fatCount * (uint32_t)tmp.sectorsPerFat) +
      tmp.rootDirSectorCount;

  if (tmp.totalSectors <= nonDataSectors) {
    return FR_INVALID_OBJECT;
  }

  // partition->sectorCount is in physical 512-byte units; the BPB's
  // totalSectors is in logical sectors. Normalize to the same (physical)
  // yardstick before comparing.
  if (partition->sectorCount != 0) {
    uint64_t physicalTotal =
        (uint64_t)tmp.totalSectors * (uint64_t)logicalToPhysicalRatio;
    if (physicalTotal > (uint64_t)partition->sectorCount) {
      return FR_INVALID_OBJECT;
    }
  }

  tmp.dataSectorCount = tmp.totalSectors - nonDataSectors;
  tmp.clusterCount = tmp.dataSectorCount / (uint32_t)tmp.sectorsPerCluster;
  if (tmp.clusterCount < 4085u || tmp.clusterCount >= 65525u) {
    return FR_INVALID_OBJECT;
  }

  // All LBA offsets live in the physical 512-byte sector namespace, so scale
  // the BPB's logical offsets by the ratio before adding to firstLBA.
  tmp.fatStartLBA = partition->firstLBA +
                    ((uint32_t)tmp.reservedSectorCount *
                     (uint32_t)logicalToPhysicalRatio);
  tmp.rootDirStartLBA =
      tmp.fatStartLBA + ((uint32_t)tmp.fatCount * (uint32_t)tmp.sectorsPerFat *
                         (uint32_t)logicalToPhysicalRatio);
  tmp.dataStartLBA =
      tmp.rootDirStartLBA +
      (tmp.rootDirSectorCount * (uint32_t)logicalToPhysicalRatio);

  *geometry = tmp;
  return FR_OK;
}

static void acsiParseTosPartitionEntry(const BYTE *sector, size_t offset,
                                       uint32_t lbaBase,
                                       AcsiTosPartitionEntry *entry) {
  if (sector == NULL || entry == NULL) {
    return;
  }

  memset(entry, 0, sizeof(*entry));
  entry->status = sector[offset];
  entry->idText[0] = (char)sector[offset + 1u];
  entry->idText[1] = (char)sector[offset + 2u];
  entry->idText[2] = (char)sector[offset + 3u];
  entry->idText[3] = '\0';

  if ((entry->status & 0x01u) == 0u) {
    return;
  }

  uint32_t relativeLBA = acsiReadBe32(sector, offset + 4u);
  entry->sectorCount = acsiReadBe32(sector, offset + 8u);
  entry->isStandard = acsiIsTosStandardId(sector, offset + 1u);
  entry->isExtended = acsiIsTosExtendedId(sector, offset + 1u);
  entry->isPresent =
      (entry->sectorCount != 0u) && (entry->isStandard || entry->isExtended);
  entry->isBootable = (entry->status & 0x80u) != 0u;

  if (!entry->isPresent) {
    memset(entry, 0, sizeof(*entry));
    return;
  }

  uint64_t absoluteLBA = (uint64_t)lbaBase + (uint64_t)relativeLBA;
  if (absoluteLBA > 0xFFFFFFFFu) {
    memset(entry, 0, sizeof(*entry));
    return;
  }

  entry->firstLBA = (uint32_t)absoluteLBA;
}

static bool acsiTosPartitionRangeIsValid(const AcsiImageContext *context,
                                         const AcsiTosPartitionEntry *entry) {
  if (context == NULL || entry == NULL || !entry->isPresent ||
      entry->sectorCount == 0u) {
    return false;
  }

  return (uint64_t)entry->firstLBA + (uint64_t)entry->sectorCount <=
         (uint64_t)context->totalSectors;
}

static bool acsiSectorLooksLikeNativeTosRoot(const BYTE *sector,
                                             uint32_t totalSectors) {
  if (sector == NULL) {
    return false;
  }

  bool foundKnownEntry = false;
  for (uint8_t index = 0; index < ACSI_PARTITION_COUNT; ++index) {
    size_t offset = 0x01C6u + ((size_t)index * 12u);
    uint8_t status = sector[offset];

    if ((status & 0x01u) == 0u) {
      continue;
    }

    if (!acsiIsTosStandardId(sector, offset + 1u) &&
        !acsiIsTosExtendedId(sector, offset + 1u)) {
      return false;
    }

    uint32_t startLBA = acsiReadBe32(sector, offset + 4u);
    uint32_t size = acsiReadBe32(sector, offset + 8u);
    if (startLBA == 0u || size == 0u ||
        (uint64_t)startLBA + (uint64_t)size > (uint64_t)totalSectors) {
      return false;
    }

    foundKnownEntry = true;
  }

  if (!foundKnownEntry) {
    return false;
  }

  uint32_t advertisedSize = acsiReadBe32(sector, 0x01C2u);
  return advertisedSize == 0u || advertisedSize <= totalSectors;
}

static void acsiValidateNativeTosExtendedChain(
    AcsiImageContext *context, const AcsiTosPartitionEntry *extendedEntry,
    uint8_t rootIndex) {
  if (context == NULL || extendedEntry == NULL || !extendedEntry->isPresent ||
      !extendedEntry->isExtended) {
    return;
  }

  uint32_t extendedBaseLBA = extendedEntry->firstLBA;
  uint32_t currentErsLBA = extendedBaseLBA;

  acsiTestLog("T%u ERS chain:\n", (unsigned int)(rootIndex + 1u));

  for (uint8_t linkIndex = 0; linkIndex < ACSI_MAX_PARTITIONS; ++linkIndex) {
    BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
    FRESULT fr = acsi_image_read_sectors(context, currentErsLBA, 1, sector,
                                         sizeof(sector));
    if (fr != FR_OK) {
      acsiTestLog("  R%u: lba=%lu read error (%d)\n",
                  (unsigned int)(linkIndex + 1u), (unsigned long)currentErsLBA,
                  (int)fr);
      return;
    }

    int standardIndex = -1;
    int linkEntryIndex = -1;
    int presentCount = 0;
    AcsiTosPartitionEntry standardEntry = {0};
    AcsiTosPartitionEntry nextEntry = {0};

    for (uint8_t entryIndex = 0; entryIndex < ACSI_PARTITION_COUNT;
         ++entryIndex) {
      size_t offset = 0x01C6u + ((size_t)entryIndex * 12u);
      AcsiTosPartitionEntry candidate = {0};

      acsiParseTosPartitionEntry(sector, offset,
                                 acsiIsTosExtendedId(sector, offset + 1u)
                                     ? extendedBaseLBA
                                     : currentErsLBA,
                                 &candidate);
      if (!candidate.isPresent) {
        continue;
      }

      presentCount++;
      if (candidate.isStandard && standardIndex < 0) {
        standardIndex = (int)entryIndex;
        standardEntry = candidate;
      } else if (candidate.isExtended && linkEntryIndex < 0) {
        linkEntryIndex = (int)entryIndex;
        nextEntry = candidate;
      } else {
        acsiTestLog("  R%u: lba=%lu invalid ERS entry mix\n",
                    (unsigned int)(linkIndex + 1u),
                    (unsigned long)currentErsLBA);
        return;
      }
    }

    bool ersOk = standardIndex >= 0 &&
                 acsiTosPartitionRangeIsValid(context, &standardEntry) &&
                 (!nextEntry.isPresent ||
                  (acsiTosPartitionRangeIsValid(context, &nextEntry) &&
                   linkEntryIndex == (standardIndex + 1) &&
                   nextEntry.firstLBA != currentErsLBA)) &&
                 presentCount <= 2;

    char nextText[24];
    if (nextEntry.isPresent) {
      (void)snprintf(nextText, sizeof(nextText), "%lu",
                     (unsigned long)nextEntry.firstLBA);
    } else {
      (void)snprintf(nextText, sizeof(nextText), "-");
    }

    acsiTestLog("  R%u: lba=%lu ers=%s part=%s %lu/%lu next=%s\n",
                (unsigned int)(linkIndex + 1u), (unsigned long)currentErsLBA,
                ersOk ? "OK" : "BAD", standardEntry.idText,
                (unsigned long)standardEntry.firstLBA,
                (unsigned long)standardEntry.sectorCount, nextText);

    if (!ersOk) {
      acsiTestLog("    invalid native TOS extended root sector layout\n");
      return;
    }

    if (!nextEntry.isPresent) {
      break;
    }

    currentErsLBA = nextEntry.firstLBA;
  }
}

static bool acsiRunNativeTosImageTests(AcsiImageContext *context) {
  if (context == NULL) {
    return false;
  }

  BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
  if (acsi_image_read_sectors(context, 0, 1, sector, sizeof(sector)) != FR_OK ||
      !acsiSectorLooksLikeNativeTosRoot(sector, context->totalSectors)) {
    return false;
  }

  uint32_t advertisedSize = acsiReadBe32(sector, 0x01C2u);
  acsiTestLog("Native TOS root sector detected\n");
  acsiTestLog("TOS disk size: %lu sectors%s\n",
              (unsigned long)((advertisedSize != 0u) ? advertisedSize
                                                     : context->totalSectors),
              (advertisedSize != 0u) ? "" : " (from image size)");

  uint8_t partitionCount = 0;
  for (uint8_t index = 0; index < ACSI_PARTITION_COUNT; ++index) {
    size_t offset = 0x01C6u + ((size_t)index * 12u);
    AcsiTosPartitionEntry entry = {0};
    acsiParseTosPartitionEntry(sector, offset, 0u, &entry);

    if (!entry.isPresent) {
      acsiTestLog("T%u: unused\n", (unsigned int)(index + 1u));
      continue;
    }

    partitionCount++;
    uint64_t partitionBytes =
        (uint64_t)entry.sectorCount * ACSI_IMAGE_SECTOR_SIZE;
    char partitionSizeText[32];
    acsiFormatSize(partitionBytes, partitionSizeText,
                   sizeof(partitionSizeText));

    acsiTestLog("T%u: id=%s boot=0x%02X start=%lu sectors=%lu (%s)\n",
                (unsigned int)(index + 1u), entry.idText,
                (unsigned int)entry.status, (unsigned long)entry.firstLBA,
                (unsigned long)entry.sectorCount, partitionSizeText);

    if (!acsiTosPartitionRangeIsValid(context, &entry)) {
      acsiTestLog("  ERROR: partition is outside the image bounds\n");
      continue;
    }

    if (entry.isExtended) {
      acsiValidateNativeTosExtendedChain(context, &entry, index);
    }
  }

  acsiTestLog("Native TOS partitions found: %u\n",
              (unsigned int)partitionCount);
  return true;
}

static void acsiTestLog(const char *fmt, ...) {
  char buffer[256];
  va_list args;

  va_start(args, fmt);
  int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (written < 0) {
    return;
  }

  DPRINTFRAW("%s", buffer);
}

static void acsiFormatSize(uint64_t bytes, char *output, size_t outputSize) {
  if (bytes >= (1024ull * 1024ull)) {
    (void)snprintf(output, outputSize, "%llu MiB",
                   (unsigned long long)(bytes / (1024ull * 1024ull)));
    return;
  }

  if (bytes >= 1024ull) {
    (void)snprintf(output, outputSize, "%llu KiB",
                   (unsigned long long)(bytes / 1024ull));
    return;
  }

  (void)snprintf(output, outputSize, "%llu B", (unsigned long long)bytes);
}

static bool acsiBytesAreZero(const BYTE *buffer, size_t offset, size_t length) {
  if (buffer == NULL) {
    return false;
  }

  for (size_t index = 0; index < length; ++index) {
    if (buffer[offset + index] != 0u) {
      return false;
    }
  }

  return true;
}

static bool acsiSectorHasStandardDosPadding(const BYTE *sector) {
  return acsiBytesAreZero(sector, 0x01DEu, 16u) &&
         acsiBytesAreZero(sector, 0x01EEu, 16u);
}

static bool acsiSectorHasHddriverTosEntry(
    const BYTE *sector, const AcsiPartitionEntry *logicalPartition) {
  if (sector == NULL || logicalPartition == NULL ||
      !logicalPartition->isPresent || logicalPartition->sectorCount <= 1u) {
    return false;
  }

  uint8_t state = sector[0x01DEu];
  if ((state & 0x01u) == 0u) {
    return false;
  }

  if (!acsiBytesAreAsciiId(sector, 0x01DFu, "GEM") &&
      !acsiBytesAreAsciiId(sector, 0x01DFu, "BGM")) {
    return false;
  }

  uint32_t tosStartLBA = acsiReadBe32(sector, 0x01E2u);
  uint32_t tosSize = acsiReadBe32(sector, 0x01E6u);
  return tosStartLBA == (logicalPartition->firstLBA + 1u) &&
         tosSize == (logicalPartition->sectorCount - 1u);
}

static bool acsiSectorHasHddriverNextEntry(const BYTE *sector,
                                           uint32_t extendedBaseLBA,
                                           const AcsiPartitionEntry *nextLink) {
  if (sector == NULL || nextLink == NULL) {
    return false;
  }

  if (!nextLink->isPresent) {
    return acsiBytesAreZero(sector, 0x01EAu, 20u);
  }

  uint8_t state = sector[0x01EAu];
  if ((state & 0x01u) == 0u || !acsiBytesAreAsciiId(sector, 0x01EBu, "XGM")) {
    return false;
  }

  uint32_t nextRelativeLBA = acsiReadBe32(sector, 0x01EEu);
  uint32_t nextSize = acsiReadBe32(sector, 0x01F2u);
  return nextLink->firstLBA >= extendedBaseLBA &&
         nextRelativeLBA == (nextLink->firstLBA - extendedBaseLBA) &&
         nextSize == nextLink->sectorCount &&
         acsiBytesAreZero(sector, 0x01F6u, 8u);
}

static void acsiValidateExtendedChain(
    AcsiImageContext *context, const AcsiPartitionEntry *extendedPartition,
    uint8_t primaryIndex) {
  if (context == NULL || extendedPartition == NULL ||
      !extendedPartition->isPresent || !extendedPartition->isExtended) {
    return;
  }

  uint32_t extendedBaseLBA = extendedPartition->firstLBA;
  uint32_t currentEbrLBA = extendedBaseLBA;
  bool foundAnyLink = false;

  acsiTestLog("P%u EMBR chain:\n", (unsigned int)(primaryIndex + 1u));

  for (uint8_t linkIndex = 0; linkIndex < ACSI_MAX_PARTITIONS; ++linkIndex) {
    BYTE sector[ACSI_IMAGE_SECTOR_SIZE] = {0};
    FRESULT fr = acsi_image_read_sectors(context, currentEbrLBA, 1, sector,
                                         sizeof(sector));
    if (fr != FR_OK) {
      acsiTestLog("  E%u: lba=%lu read error (%d)\n",
                  (unsigned int)(linkIndex + 1u), (unsigned long)currentEbrLBA,
                  (int)fr);
      return;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
      acsiTestLog("  E%u: lba=%lu invalid signature\n",
                  (unsigned int)(linkIndex + 1u), (unsigned long)currentEbrLBA);
      return;
    }

    AcsiPartitionEntry logicalPartition = {0};
    AcsiPartitionEntry nextLink = {0};
    acsiParsePartitionEntry(sector, 446u, currentEbrLBA, &logicalPartition);
    acsiParsePartitionEntry(sector, 462u, extendedBaseLBA, &nextLink);

    bool dosLayoutOk = logicalPartition.isPresent &&
                       !logicalPartition.isExtended &&
                       acsiPartitionRangeIsValid(context, &logicalPartition) &&
                       (!nextLink.isPresent ||
                        (nextLink.isExtended &&
                         acsiPartitionRangeIsValid(context, &nextLink) &&
                         nextLink.firstLBA != currentEbrLBA));

    AcsiTosDosStyle detectedStyle = ACSI_TOSDOS_STYLE_NONE;
    if (logicalPartition.isFat16) {
      AcsiFat16Geometry geometry = {0};
      AcsiTosBootSectorInfo tosInfo = {0};
      if (acsi_parse_fat16_geometry(context, &logicalPartition, &geometry) ==
          FR_OK) {
        detectedStyle = acsiDetectTosDosStyle(context, &logicalPartition,
                                              &geometry, &tosInfo);
      }
    }

    bool styleLayoutOk = false;
    char nextText[24];
    if (detectedStyle == ACSI_TOSDOS_STYLE_HDDRIVER) {
      styleLayoutOk =
          acsiSectorHasHddriverTosEntry(sector, &logicalPartition) &&
          acsiSectorHasHddriverNextEntry(sector, extendedBaseLBA, &nextLink);
    } else {
      styleLayoutOk = acsiSectorHasStandardDosPadding(sector);
    }

    if (nextLink.isPresent) {
      (void)snprintf(nextText, sizeof(nextText), "%lu",
                     (unsigned long)nextLink.firstLBA);
    } else {
      (void)snprintf(nextText, sizeof(nextText), "-");
    }

    foundAnyLink = true;
    acsiTestLog("  E%u: lba=%lu dos=%s style=%s embr=%s part=%lu/%lu next=%s\n",
                (unsigned int)(linkIndex + 1u), (unsigned long)currentEbrLBA,
                dosLayoutOk ? "OK" : "BAD", acsiTosDosStyleName(detectedStyle),
                styleLayoutOk ? "OK" : "BAD",
                (unsigned long)logicalPartition.firstLBA,
                (unsigned long)logicalPartition.sectorCount, nextText);

    if (!dosLayoutOk) {
      acsiTestLog("    chain entry is not a valid DOS EMBR link\n");
      return;
    }

    if (detectedStyle == ACSI_TOSDOS_STYLE_HDDRIVER && !styleLayoutOk) {
      acsiTestLog("    HDDRIVER hybrid markers do not match this EMBR\n");
    } else if (detectedStyle == ACSI_TOSDOS_STYLE_PPDRIVER && !styleLayoutOk) {
      acsiTestLog("    PPDRIVER EMBR should keep third/fourth entries empty\n");
    } else if (detectedStyle == ACSI_TOSDOS_STYLE_NONE && !styleLayoutOk) {
      acsiTestLog("    EMBR uses unexpected extra entries\n");
    }

    if (!nextLink.isPresent) {
      break;
    }

    currentEbrLBA = nextLink.firstLBA;
  }

  if (!foundAnyLink) {
    acsiTestLog("  <empty>\n");
  }
}

static FRESULT acsiListRootDirectory(AcsiImageContext *context,
                                     const AcsiFat16Geometry *geometry,
                                     const char *partitionLabel) {
  if (context == NULL || geometry == NULL) {
    return FR_INVALID_PARAMETER;
  }

  char label[32];
  (void)snprintf(label, sizeof(label), "%s root directory",
                 (partitionLabel != NULL) ? partitionLabel : "VOL");
  return acsiFat16ListDirectory(context, geometry, true, 0u, label);
}

static FRESULT acsiListNamedRootSubdirectory(AcsiImageContext *context,
                                             const AcsiFat16Geometry *geometry,
                                             const char *partitionLabel,
                                             const char *directoryName) {
  if (context == NULL || geometry == NULL || directoryName == NULL ||
      directoryName[0] == '\0') {
    return FR_INVALID_PARAMETER;
  }

  AcsiFat16DirectoryEntry directoryEntry = {0};
  FRESULT fr = acsiFat16FindEntry(context, geometry, true, 0u, directoryName,
                                  &directoryEntry);
  if (fr != FR_OK) {
    acsiTestLog("  %s %s: lookup error (%d)\n",
                (partitionLabel != NULL) ? partitionLabel : "VOL",
                directoryName, (int)fr);
    return fr;
  }

  if ((directoryEntry.attributes & 0x10u) == 0u ||
      directoryEntry.firstCluster < 2u) {
    acsiTestLog("  %s %s: entry is not a valid directory\n",
                (partitionLabel != NULL) ? partitionLabel : "VOL",
                directoryName);
    return FR_INVALID_OBJECT;
  }

  char label[48];
  (void)snprintf(label, sizeof(label), "%s %s directory",
                 (partitionLabel != NULL) ? partitionLabel : "VOL",
                 directoryName);
  return acsiFat16ListDirectory(context, geometry, false,
                                directoryEntry.firstCluster, label);
}

static void acsiRunImageTests(void) {
  AcsiImageContext context = {0};
  AcsiPartitionEntry primaryPartitions[ACSI_PARTITION_COUNT] = {0};
  AcsiPartitionEntry usablePartitions[ACSI_MAX_PARTITIONS] = {0};
  uint8_t partitionCount = 0;
  uint8_t usablePartitionCount = 0;
  acsiTestLog("\nACSI image tests\n");
  acsiTestLog("----------------\n");
  acsiTestLog("Image: %s\n", acsiImagePath);

  FRESULT fr = acsi_image_open(&context, acsiImagePath, true);
  if (fr != FR_OK) {
    acsiTestLog("ERROR: cannot open image (%d)\n", (int)fr);
    return;
  }

  char imageSizeText[32];
  acsiFormatSize((uint64_t)context.imageSizeBytes, imageSizeText,
                 sizeof(imageSizeText));
  acsiTestLog("Image size: %llu bytes (%s)\n",
              (unsigned long long)context.imageSizeBytes, imageSizeText);
  acsiTestLog("Raw sectors: %lu\n", (unsigned long)context.totalSectors);

  if ((context.imageSizeBytes % ACSI_IMAGE_SECTOR_SIZE) != 0) {
    acsiTestLog("WARNING: image has trailing bytes outside 512-byte sectors\n");
  }

  fr = acsi_parse_mbr(&context, primaryPartitions, &partitionCount);
  if (fr != FR_OK) {
    if (!acsiRunNativeTosImageTests(&context)) {
      acsiTestLog("ERROR: invalid or unreadable MBR (%d)\n", (int)fr);
    }
    acsiTestLog("----------------\n");
    acsi_image_close(&context);
    return;
  }

  acsiTestLog("MBR: OK, partitions found: %u\n", (unsigned int)partitionCount);

  for (uint8_t index = 0; index < ACSI_PARTITION_COUNT; ++index) {
    const AcsiPartitionEntry *partition = &primaryPartitions[index];
    if (!partition->isPresent) {
      acsiTestLog("P%u: unused\n", (unsigned int)(index + 1u));
      continue;
    }

    uint64_t partitionBytes =
        (uint64_t)partition->sectorCount * ACSI_IMAGE_SECTOR_SIZE;
    char partitionSizeText[32];
    acsiFormatSize(partitionBytes, partitionSizeText,
                   sizeof(partitionSizeText));

    acsiTestLog(
        "P%u: type=0x%02X (%s) boot=0x%02X start=%lu sectors=%lu (%s)\n",
        (unsigned int)(index + 1u), (unsigned int)partition->partitionType,
        acsiPartitionTypeName(partition->partitionType),
        (unsigned int)partition->bootIndicator,
        (unsigned long)partition->firstLBA,
        (unsigned long)partition->sectorCount, partitionSizeText);

    if (partition->isExtended) {
      acsiValidateExtendedChain(&context, partition, index);
    }
  }

  fr = acsi_enumerate_partitions(&context, usablePartitions,
                                 &usablePartitionCount);
  if (fr != FR_OK) {
    acsiTestLog("ERROR: cannot enumerate usable partitions (%d)\n", (int)fr);
    acsi_image_close(&context);
    return;
  }

  acsiTestLog("Usable partitions: %u\n", (unsigned int)usablePartitionCount);

  if (usablePartitionCount == 0u) {
    acsiTestLog("  <none>\n");
  }

  for (uint8_t index = 0; index < usablePartitionCount; ++index) {
    if (index > 0u) {
      break;
    }

    const AcsiPartitionEntry *partition = &usablePartitions[index];
    uint16_t volumeDriveNumber =
        (uint16_t)((acsiVolumeDriveRangeValid ? acsiFirstVolumeDrive : 2u) +
                   (uint32_t)index);
    uint64_t partitionBytes =
        (uint64_t)partition->sectorCount * ACSI_IMAGE_SECTOR_SIZE;
    char partitionSizeText[32];
    char partitionLabel[8];

    acsiFormatSize(partitionBytes, partitionSizeText,
                   sizeof(partitionSizeText));
    (void)snprintf(partitionLabel, sizeof(partitionLabel), "V%u",
                   (unsigned int)(index + 1u));

    acsiTestLog("%s: type=0x%02X (%s) start=%lu sectors=%lu (%s)\n",
                partitionLabel, (unsigned int)partition->partitionType,
                acsiPartitionTypeName(partition->partitionType),
                (unsigned long)partition->firstLBA,
                (unsigned long)partition->sectorCount, partitionSizeText);

    if (!partition->isFat16) {
      acsiTestLog("  skipping: only FAT16 partitions are tested right now\n");
      continue;
    }

    AcsiFat16Geometry geometry = {0};
    fr = acsi_parse_fat16_geometry(&context, partition, &geometry);
    if (fr != FR_OK) {
      acsiTestLog("  ERROR: invalid FAT16 BPB/geometry (%d)\n", (int)fr);
      continue;
    }

    AcsiTosBootSectorInfo tosInfo = {0};
    AcsiTosDosStyle tosDosStyle =
        acsiDetectTosDosStyle(&context, partition, &geometry, &tosInfo);

    acsiTestLog(
        "  BPB: bps=%u spc=%u reserved=%u fats=%u spf=%u root=%u total=%lu\n",
        (unsigned int)geometry.bytesPerSector,
        (unsigned int)geometry.sectorsPerCluster,
        (unsigned int)geometry.reservedSectorCount,
        (unsigned int)geometry.fatCount, (unsigned int)geometry.sectorsPerFat,
        (unsigned int)geometry.rootEntryCount,
        (unsigned long)geometry.totalSectors);
    acsiTestLog("  GEO: hidden=%lu spt=%u heads=%u media=0x%02X clusters=%lu\n",
                (unsigned long)geometry.hiddenSectors,
                (unsigned int)geometry.sectorsPerTrack,
                (unsigned int)geometry.headCount,
                (unsigned int)geometry.mediaDescriptor,
                (unsigned long)geometry.clusterCount);
    acsiTestLog("  MAP: fat=%lu root=%lu data=%lu root_secs=%lu\n",
                (unsigned long)geometry.fatStartLBA,
                (unsigned long)geometry.rootDirStartLBA,
                (unsigned long)geometry.dataStartLBA,
                (unsigned long)geometry.rootDirSectorCount);
    acsiTestLog("  TOS&DOS: %s\n", acsiTosDosStyleName(tosDosStyle));
    if (tosDosStyle != ACSI_TOSDOS_STYLE_NONE) {
      AcsiBPBData dosBpb = {0};
      AcsiBPBData tosBpb = {0};

      acsiTestLog("  TBS: lba=%lu bps=%u spc=%u reserved=%u spf=%u total=%u\n",
                  (unsigned long)(partition->firstLBA + 1u),
                  (unsigned int)tosInfo.bytesPerSector,
                  (unsigned int)tosInfo.sectorsPerCluster,
                  (unsigned int)tosInfo.reservedSectorCount,
                  (unsigned int)tosInfo.sectorsPerFat,
                  (unsigned int)tosInfo.totalLogicalSectors);
      acsiTestLog("  TMAP: fat=%lu root=%lu data=%lu root_secs=%lu\n",
                  (unsigned long)tosInfo.fatStartLBA,
                  (unsigned long)tosInfo.rootDirStartLBA,
                  (unsigned long)tosInfo.dataStartLBA,
                  (unsigned long)tosInfo.rootDirSectorCount);
      if (acsiBuildBpbData(volumeDriveNumber, &geometry, &dosBpb)) {
        acsiTestLogBpbData("DOS BPB", &dosBpb);
      } else {
        acsiTestLog("  DOS BPB: <unavailable>\n");
      }
      if (acsiBuildTosBpbData(volumeDriveNumber, &tosInfo, &tosBpb)) {
        acsiTestLogBpbData("TOS BPB", &tosBpb);
      } else {
        acsiTestLog("  TOS BPB: <unavailable>\n");
      }
    }

  }

  acsiTestLog("----------------\n");
  acsi_image_close(&context);
}

void acsi_preInit(void) {
  bool enabled = false;
  uint8_t acsiId = 0;
  uint8_t firstDrive = 2u;  // 'C'

  DPRINTF("ACSI pre-init\n");
  acsiLoadConfiguredState(&enabled, &acsiId, &firstDrive);
  acsiResetVolumeDriveRange();
  acsiResetPunInfoCache();

  if (!enabled) {
    DPRINTF("ACSI tests skipped because ACSI is disabled.\n");
    return;
  }

  if (acsiImagePath[0] == '\0') {
    DPRINTF("ACSI tests skipped because no image is configured.\n");
    return;
  }

  FRESULT fr = f_mount(&acsiFilesys, "0:", 1);
  if (fr != FR_OK) {
    DPRINTF("ACSI tests cannot mount SD card (%d)\n", (int)fr);
    return;
  }

  acsiRefreshVolumeDriveRange(acsiId, firstDrive);
  DPRINTF("Running ACSI tests before driver initialization.\n");
  acsiRunImageTests();

  (void)f_mount(NULL, "0:", 1);
}

void acsi_printBootInfo(AcsiBootInfoPrint print) {
  if (print == NULL) {
    return;
  }

  if (!acsiIsEnabledSetting()) {
    print("ACSI emulation is disabled.\n");
    return;
  }

  if (acsiImagePath[0] == '\0') {
    print("ACSI: no image configured.\n");
    return;
  }

  char line[96];

  // Image path — trim to the last 30 chars so long paths still fit.
  const char *shortPath = acsiImagePath;
  size_t pathLen = strlen(acsiImagePath);
  const size_t maxPathChars = 30u;
  if (pathLen > maxPathChars) {
    shortPath = acsiImagePath + (pathLen - maxPathChars);
    snprintf(line, sizeof(line), "Image: ..%s\n", shortPath);
  } else {
    snprintf(line, sizeof(line), "Image: %s\n", shortPath);
  }
  print(line);

  uint8_t acsiId = acsiGetIdSetting();
  snprintf(line, sizeof(line), "ACSI ID: %u\n", (unsigned int)acsiId);
  print(line);

  if (!acsiVolumeDriveRangeValid) {
    if (acsiScanOversizedSectorSize > 0u) {
      snprintf(line, sizeof(line),
               "Unsupported logical sector size: %u bytes.\n",
               (unsigned int)acsiScanOversizedSectorSize);
      print(line);
      print("Max supported is 8192 (e.g. 256 MB Hatari images).\n");
      print("Rebuild the image at a smaller size to mount it.\n");
    } else {
      print("No usable FAT16 partitions found.\n");
    }
    return;
  }

  // Summarize the disk layout by looking at the first partition's style.
  AcsiTosDosStyle imageStyle =
      (AcsiTosDosStyle)acsiPartitionStyle[acsiFirstVolumeDrive];
  const char *imageTypeLabel = "Atari ST / FAT16";
  if (imageStyle == ACSI_TOSDOS_STYLE_PPDRIVER) {
    imageTypeLabel = "TOS&DOS (PPDRIVER)";
  } else if (imageStyle == ACSI_TOSDOS_STYLE_HDDRIVER) {
    imageTypeLabel = "TOS&DOS (HDDRIVER)";
  }
  snprintf(line, sizeof(line), "Disk type: %s\n", imageTypeLabel);
  print(line);

  uint32_t partitionCount =
      (acsiLastVolumeDrive - acsiFirstVolumeDrive) + 1u;
  snprintf(line, sizeof(line), "Partitions: %lu  (%c: to %c:)\n",
           (unsigned long)partitionCount,
           acsiDriveNumberToLetter(acsiFirstVolumeDrive),
           acsiDriveNumberToLetter(acsiLastVolumeDrive));
  print(line);

  for (uint32_t drive = acsiFirstVolumeDrive; drive <= acsiLastVolumeDrive;
       ++drive) {
    uint32_t sectors = acsiPartitionSectorCounts[drive];
    uint16_t sectorSize = acsiLogicalSectorSizes[drive];
    if ((sectors == 0u) || (sectorSize == 0u)) {
      continue;
    }
    uint64_t bytes = (uint64_t)sectors * (uint64_t)sectorSize;
    uint32_t megabytes = (uint32_t)(bytes / (1024u * 1024u));

    AcsiTosDosStyle style = (AcsiTosDosStyle)acsiPartitionStyle[drive];
    const char *typeLabel = "Atari/FAT16";
    if (style == ACSI_TOSDOS_STYLE_PPDRIVER) {
      typeLabel = "TOS&DOS PP";
    } else if (style == ACSI_TOSDOS_STYLE_HDDRIVER) {
      typeLabel = "TOS&DOS HD";
    }
    const char *viewLabel = acsiPartitionViewIsTos[drive] ? "TOS" : "DOS";

    snprintf(line, sizeof(line),
             "  %c: %4lu MB  %s [%s view]\n",
             acsiDriveNumberToLetter(drive), (unsigned long)megabytes,
             typeLabel, viewLabel);
    print(line);
  }
}

void __not_in_flash_func(acsi_init)() {
  DPRINTF("Initializing ACSI placeholder...\n");

  acsi_image_close(&acsiRuntimeImage);
  memorySharedAddress = (unsigned int)&__rom_in_ram_start__;
  memoryRandomTokenAddress = memorySharedAddress + ACSIEMUL_RANDOM_TOKEN_OFFSET;
  memoryRandomTokenSeedAddress =
      memorySharedAddress + ACSIEMUL_RANDOM_TOKEN_SEED_OFFSET;
  acsiResetHookTraceState();

  bool enabled = false;
  uint8_t acsiId = 0;
  uint8_t firstDrive = 2u;  // 'C' — unused here but acsiLoadConfiguredState expects it
  acsiLoadConfiguredState(&enabled, &acsiId, &firstDrive);

  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_ENABLED,
                         enabled ? 0xFFFFFFFFu : 0xDEAD0000u,
                         memorySharedAddress, ACSIEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_ACSI_ID, acsiId,
                         memorySharedAddress, ACSIEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_HOOKS_INSTALLED, 0u, memorySharedAddress,
                         ACSIEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_OLD_HDV_INIT, 0u, memorySharedAddress,
                         ACSIEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_OLD_HDV_BPB, 0u, memorySharedAddress,
                         ACSIEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_OLD_HDV_RW, 0u, memorySharedAddress,
                         ACSIEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_OLD_HDV_BOOT, 0u, memorySharedAddress,
                         ACSIEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_OLD_HDV_MEDIACH, 0u, memorySharedAddress,
                         ACSIEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_FIRST_VOLUME_DRIVE, acsiFirstVolumeDrive,
                         memorySharedAddress, ACSIEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_LAST_VOLUME_DRIVE, acsiLastVolumeDrive,
                         memorySharedAddress, ACSIEMUL_SHARED_VARIABLES_OFFSET);
  acsiSetRwStatus(0);
  uint32_t mediaChangedMask = acsiBuildInitialMediaChangedMask();
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_MEDIA_CHANGED_MASK, mediaChangedMask,
                         memorySharedAddress, ACSIEMUL_SHARED_VARIABLES_OFFSET);
  acsiWritePunInfoToSharedMemory();
  acsiWriteBpbDataToSharedMemory();
  SET_SHARED_PRIVATE_VAR(ACSIEMUL_SVAR_PUN_INFO_PTR,
                         acsiPunInfoValid ? ACSIEMUL_ST_PUN_INFO_PTR : 0u,
                         memorySharedAddress, ACSIEMUL_SHARED_VARIABLES_OFFSET);

  if (memoryRandomTokenAddress != 0) {
    uint32_t randomToken = 0;
    DPRINTF("Init random token: %08X\n", randomToken);
    TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenAddress, randomToken);

    uint32_t newRandomSeedToken = get_rand_32();
    DPRINTF("Set the new random token seed: %08X\n", newRandomSeedToken);
    TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenSeedAddress, newRandomSeedToken);
  }

  DPRINTF(
      "ACSI placeholder configured: enabled=%s acsi_id=%u "
      "start_drive=%c (%u) image=%s\n",
      enabled ? "true" : "false", (unsigned int)acsiId,
      acsiDriveNumberToLetter(firstDrive), (unsigned int)firstDrive,
      (acsiImagePath[0] != '\0') ? acsiImagePath : "<not set>");
  DPRINTF("ACSI shared drive range: first=%c (%lu) last=%c (%lu)\n",
          acsiDriveNumberToLetter(acsiFirstVolumeDrive),
          (unsigned long)acsiFirstVolumeDrive,
          acsiDriveNumberToLetter(acsiLastVolumeDrive),
          (unsigned long)acsiLastVolumeDrive);
  DPRINTF("ACSI mediach mask init: %08lX\n", (unsigned long)mediaChangedMask);
}

void __not_in_flash_func(acsi_tick)(void) {
  if (!acsiWriteDirty || !acsiRuntimeImage.isOpen ||
      acsiRuntimeImage.readOnly) {
    return;
  }
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if ((now - acsiWriteDirtyAtMs) < ACSI_FLUSH_INTERVAL_MS) {
    return;
  }
  FRESULT fr = f_sync(&acsiRuntimeImage.file);
  if (fr != FR_OK) {
    DPRINTF("ACSI tick f_sync failed (%d)\n", (int)fr);
    // Leave dirty so the next tick retries; pushing the timestamp forward
    // avoids tight-looping on a persistent error.
    acsiWriteDirtyAtMs = now;
    return;
  }
  acsiWriteDirty = false;
}

void __not_in_flash_func(acsi_loop)(TransmissionProtocol *lastProtocol,
                                    uint16_t *payloadPtr) {
  if (((lastProtocol->command_id >> 8) & 0xFF) != APP_ACSIEMUL) {
    return;
  }

  switch (lastProtocol->command_id) {
    case ACSIEMUL_READ_SECTOR: {
      uint16_t driveNumber = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      uint32_t logicalSector =
          (uint32_t)TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);

      if (!acsiDriveIsOwned(driveNumber) ||
          logicalSector >= acsiPartitionSectorCounts[driveNumber]) {
        DPRINTF(
            "ACSI READ_SECTOR out of range drive=%c (%u) recno=%lu limit=%lu\n",
            acsiDriveNumberToLetter(driveNumber), (unsigned int)driveNumber,
            (unsigned long)logicalSector,
            (unsigned long)((driveNumber < ACSI_PUN_INFO_MAXUNITS)
                                ? acsiPartitionSectorCounts[driveNumber]
                                : 0u));
        acsiSetRwStatus(-8);
        break;
      }

      FRESULT fr = acsiEnsureRuntimeImageOpen();
      if (fr != FR_OK) {
        DPRINTF("ACSI read open error drive=%c sector=%lu (%d)\n",
                acsiDriveNumberToLetter(driveNumber),
                (unsigned long)logicalSector, (int)fr);
        acsiSetRwStatus(-11);
        break;
      }

      uint32_t recsize = acsiLogicalSectorSizes[driveNumber];
      uint32_t physicalSectorCount = acsiLogicalToPhysicalRatios[driveNumber];
      if ((recsize < ACSI_IMAGE_SECTOR_SIZE) ||
          (recsize > ACSIEMUL_IMAGE_BUFFER_SIZE) ||
          (physicalSectorCount == 0u)) {
        DPRINTF("ACSI invalid mapping drive=%c recsize=%u ratio=%u\n",
                acsiDriveNumberToLetter(driveNumber), (unsigned int)recsize,
                (unsigned int)physicalSectorCount);
        acsiSetRwStatus(-11);
        break;
      }

      uint64_t physicalSector =
          (uint64_t)acsiPunInfoStartSectors[driveNumber] +
          ((uint64_t)logicalSector * (uint64_t)physicalSectorCount);
      DPRINTF(
          "ACSI READ_SECTOR physical drive=%c recno=%lu lba=%lu recsize=%u "
          "phys_count=%u\n",
          acsiDriveNumberToLetter(driveNumber), (unsigned long)logicalSector,
          (unsigned long)physicalSector, (unsigned int)recsize,
          (unsigned int)physicalSectorCount);
      if (physicalSector > 0xFFFFFFFFu) {
        acsiSetRwStatus(-11);
        break;
      }

      fr = acsi_image_read_sectors(
          &acsiRuntimeImage, (uint32_t)physicalSector,
          (uint16_t)physicalSectorCount,
          (void *)(uintptr_t)(memorySharedAddress +
                              ACSIEMUL_IMAGE_BUFFER_OFFSET),
          recsize);
      if (fr != FR_OK) {
        DPRINTF("ACSI read error drive=%c sector=%lu lba=%lu (%d)\n",
                acsiDriveNumberToLetter(driveNumber),
                (unsigned long)logicalSector, (unsigned long)physicalSector,
                (int)fr);
        acsiSetRwStatus(-11);
        break;
      }

      CHANGE_ENDIANESS_BLOCK16(
          memorySharedAddress + ACSIEMUL_IMAGE_BUFFER_OFFSET, recsize);
      DPRINTF("ACSI READ_SECTOR ok drive=%c recno=%lu lba=%lu recsize=%u\n",
              acsiDriveNumberToLetter(driveNumber),
              (unsigned long)logicalSector, (unsigned long)physicalSector,
              (unsigned int)recsize);
      acsiSetRwStatus(0);
    } break;
    case ACSIEMUL_READ_SECTOR_BATCH: {
      uint16_t driveNumber = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      uint32_t logicalSector =
          (uint32_t)TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);
      uint16_t sectorCount = TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);

      if (sectorCount == 0u || !acsiDriveIsOwned(driveNumber)) {
        acsiSetRwStatus(-8);
        break;
      }

      uint32_t recsize = acsiLogicalSectorSizes[driveNumber];
      uint32_t physicalSectorCount = acsiLogicalToPhysicalRatios[driveNumber];
      uint32_t totalBytes = (uint32_t)sectorCount * recsize;
      uint32_t totalPhysical = (uint32_t)sectorCount * physicalSectorCount;

      // recsize and physicalSectorCount are guaranteed non-zero (and recsize
      // is a valid power of two in [512, 8192]) by acsiDriveIsOwned +
      // the drive-table population gate, so only the batch-size ceiling
      // needs checking here (totalBytes = sectorCount × recsize, and
      // sectorCount comes from the wire).
      if (totalBytes > ACSIEMUL_IMAGE_BUFFER_SIZE) {
        DPRINTF(
            "ACSI BATCH invalid drive=%c recsize=%u count=%u total=%lu "
            "buf=%u\n",
            acsiDriveNumberToLetter(driveNumber), (unsigned int)recsize,
            (unsigned int)sectorCount, (unsigned long)totalBytes,
            (unsigned int)ACSIEMUL_IMAGE_BUFFER_SIZE);
        acsiSetRwStatus(-11);
        break;
      }

      if ((logicalSector + sectorCount) >
          acsiPartitionSectorCounts[driveNumber]) {
        acsiSetRwStatus(-8);
        break;
      }

      FRESULT fr = acsiEnsureRuntimeImageOpen();
      if (fr != FR_OK) {
        acsiSetRwStatus(-11);
        break;
      }

      uint64_t physicalSector =
          (uint64_t)acsiPunInfoStartSectors[driveNumber] +
          ((uint64_t)logicalSector * (uint64_t)physicalSectorCount);
      if ((physicalSector + totalPhysical) > 0xFFFFFFFFu) {
        acsiSetRwStatus(-11);
        break;
      }

      fr = acsi_image_read_sectors(
          &acsiRuntimeImage, (uint32_t)physicalSector, (uint16_t)totalPhysical,
          (void *)(uintptr_t)(memorySharedAddress +
                              ACSIEMUL_IMAGE_BUFFER_OFFSET),
          totalBytes);
      if (fr != FR_OK) {
        DPRINTF("ACSI BATCH read error drive=%c recno=%lu count=%u (%d)\n",
                acsiDriveNumberToLetter(driveNumber),
                (unsigned long)logicalSector, (unsigned int)sectorCount,
                (int)fr);
        acsiSetRwStatus(-11);
        break;
      }

      CHANGE_ENDIANESS_BLOCK16(
          memorySharedAddress + ACSIEMUL_IMAGE_BUFFER_OFFSET, totalBytes);
      DPRINTF("ACSI BATCH ok drive=%c recno=%lu count=%u total=%lu\n",
              acsiDriveNumberToLetter(driveNumber),
              (unsigned long)logicalSector, (unsigned int)sectorCount,
              (unsigned long)totalBytes);
      acsiSetRwStatus(0);
    } break;
    case ACSIEMUL_WRITE_SECTOR: {
      // Idempotent chunk protocol. Each chunk declares its own byte offset
      // inside the target sector via d4, so the RP never has to guess chunk
      // order and timeout-induced retries overwrite the same IMAGE_BUFFER
      // bytes harmlessly. When offset + chunkSize reaches recsize, the full
      // sector is byte-swapped and written to SD.
      uint16_t driveNumber = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      uint32_t logicalSector =
          (uint32_t)TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);
      uint32_t offsetInSector =
          TPROTO_GET_NEXT16_PAYLOAD_PARAM32(payloadPtr);  // d4
      // Advance past d4.high, d5.low, d5.high to the streamed buffer.
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);

      uint16_t *chunkData = payloadPtr;
      uint32_t chunkSize =
          (lastProtocol->payload_size >= 16u)
              ? (uint32_t)(lastProtocol->payload_size - 16u)
              : 0u;
      if (chunkSize > 1024u) {
        chunkSize = 1024u;
      }

      if (!acsiDriveIsOwned(driveNumber)) {
        DPRINTF("ACSI WRITE rejected: drive %u not owned\n",
                (unsigned int)driveNumber);
        acsiSetRwStatus(-8);
        break;
      }

      uint32_t recsize = acsiLogicalSectorSizes[driveNumber];
      // acsiDriveIsOwned guarantees recsize is a valid power of two in
      // [512, 8192]; only the chunk/offset bounds (from the wire) remain.
      if (chunkSize == 0u || offsetInSector + chunkSize > recsize) {
        DPRINTF(
            "ACSI WRITE invalid drive=%c recno=%lu offset=%lu chunk=%lu "
            "recsize=%lu\n",
            acsiDriveNumberToLetter(driveNumber),
            (unsigned long)logicalSector, (unsigned long)offsetInSector,
            (unsigned long)chunkSize, (unsigned long)recsize);
        acsiSetRwStatus(-11);
        break;
      }

      memcpy((void *)(uintptr_t)(memorySharedAddress +
                                  ACSIEMUL_IMAGE_BUFFER_OFFSET +
                                  offsetInSector),
             chunkData, chunkSize);

      DPRINTF(
          "ACSI WRITE chunk drive=%c recno=%lu offset=%lu chunk=%lu "
          "recsize=%lu\n",
          acsiDriveNumberToLetter(driveNumber),
          (unsigned long)logicalSector, (unsigned long)offsetInSector,
          (unsigned long)chunkSize, (unsigned long)recsize);

      if (offsetInSector + chunkSize < recsize) {
        // Intermediate chunk — the Atari side only reads SVAR_RW_STATUS
        // after all chunks of the sector have been acknowledged, so there
        // is no need to stamp it here.
        break;
      }

      // Last chunk: byte-swap the assembled sector and flush to SD.
      CHANGE_ENDIANESS_BLOCK16(
          memorySharedAddress + ACSIEMUL_IMAGE_BUFFER_OFFSET, recsize);

      FRESULT fr = acsiEnsureRuntimeImageOpen();
      if (fr != FR_OK) {
        DPRINTF("ACSI WRITE open error (%d)\n", (int)fr);
        acsiSetRwStatus(-11);
        break;
      }

      if (acsiRuntimeImage.readOnly) {
        DPRINTF("ACSI WRITE rejected: image is read-only\n");
        acsiSetRwStatus(-13);
        break;
      }

      uint32_t physicalSectorCount = acsiLogicalToPhysicalRatios[driveNumber];
      uint64_t physicalSector =
          (uint64_t)acsiPunInfoStartSectors[driveNumber] +
          ((uint64_t)logicalSector * (uint64_t)physicalSectorCount);

      fr = acsi_image_write_sectors(
          &acsiRuntimeImage, (uint32_t)physicalSector,
          (uint16_t)physicalSectorCount,
          (void *)(uintptr_t)(memorySharedAddress +
                               ACSIEMUL_IMAGE_BUFFER_OFFSET),
          recsize);
      if (fr != FR_OK) {
        DPRINTF("ACSI WRITE error drive=%c recno=%lu (%d)\n",
                acsiDriveNumberToLetter(driveNumber),
                (unsigned long)logicalSector, (int)fr);
        acsiSetRwStatus(-10);
        break;
      }

      DPRINTF("ACSI WRITE ok drive=%c recno=%lu recsize=%lu\n",
              acsiDriveNumberToLetter(driveNumber),
              (unsigned long)logicalSector, (unsigned long)recsize);
      acsiMarkWriteDirty();
      acsiSetRwStatus(0);
    } break;
    case ACSIEMUL_WRITE_SECTOR_BATCH: {
      // Batched, idempotent multi-sector write. Payload per chunk:
      //   d3 = startRecno:drive (constant across the batch)
      //   d4 = byte offset inside the batch (0, CHUNK, 2*CHUNK, ...)
      //   d5 = totalBytes for the batch (= count × recsize)
      // The RP copies each chunk directly to IMAGE_BUFFER[offset] and, when
      // offset + chunkSize == totalBytes, flushes the full batch with a
      // single acsi_image_write_sectors(). Retries land at the same offset
      // so they are harmless.
      uint16_t driveNumber = TPROTO_GET_PAYLOAD_PARAM16(payloadPtr);
      uint32_t startLogicalSector =
          (uint32_t)TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payloadPtr);
      uint32_t offsetInBatch =
          TPROTO_GET_NEXT16_PAYLOAD_PARAM32(payloadPtr);  // d4
      uint32_t totalBytes =
          TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);  // d5
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);              // past d5 to buffer

      uint16_t *chunkData = payloadPtr;
      uint32_t chunkSize =
          (lastProtocol->payload_size >= 16u)
              ? (uint32_t)(lastProtocol->payload_size - 16u)
              : 0u;
      if (chunkSize > 1024u) {
        chunkSize = 1024u;
      }

      if (!acsiDriveIsOwned(driveNumber)) {
        DPRINTF("ACSI WRITE_BATCH rejected: drive %u not owned\n",
                (unsigned int)driveNumber);
        acsiSetRwStatus(-8);
        break;
      }

      uint32_t recsize = acsiLogicalSectorSizes[driveNumber];
      // acsiDriveIsOwned guarantees recsize != 0; the remaining checks cover
      // wire-supplied values (totalBytes, chunkSize, offsetInBatch).
      if (totalBytes == 0u || (totalBytes % recsize) != 0u ||
          totalBytes > ACSIEMUL_IMAGE_BUFFER_SIZE || chunkSize == 0u ||
          offsetInBatch + chunkSize > totalBytes) {
        DPRINTF(
            "ACSI WRITE_BATCH invalid drive=%c recno=%lu offset=%lu chunk=%lu "
            "total=%lu recsize=%lu\n",
            acsiDriveNumberToLetter(driveNumber),
            (unsigned long)startLogicalSector,
            (unsigned long)offsetInBatch, (unsigned long)chunkSize,
            (unsigned long)totalBytes, (unsigned long)recsize);
        acsiSetRwStatus(-11);
        break;
      }

      memcpy((void *)(uintptr_t)(memorySharedAddress +
                                  ACSIEMUL_IMAGE_BUFFER_OFFSET +
                                  offsetInBatch),
             chunkData, chunkSize);

      DPRINTF(
          "ACSI WRITE_BATCH chunk drive=%c recno=%lu offset=%lu chunk=%lu "
          "total=%lu\n",
          acsiDriveNumberToLetter(driveNumber),
          (unsigned long)startLogicalSector,
          (unsigned long)offsetInBatch, (unsigned long)chunkSize,
          (unsigned long)totalBytes);

      if (offsetInBatch + chunkSize < totalBytes) {
        // Intermediate chunk — the Atari side only reads SVAR_RW_STATUS
        // after the last chunk of the batch is acknowledged.
        break;
      }

      // Last chunk — byte-swap the full batch and flush in one write.
      CHANGE_ENDIANESS_BLOCK16(
          memorySharedAddress + ACSIEMUL_IMAGE_BUFFER_OFFSET, totalBytes);

      FRESULT fr = acsiEnsureRuntimeImageOpen();
      if (fr != FR_OK) {
        DPRINTF("ACSI WRITE_BATCH open error (%d)\n", (int)fr);
        acsiSetRwStatus(-11);
        break;
      }

      if (acsiRuntimeImage.readOnly) {
        DPRINTF("ACSI WRITE_BATCH rejected: image is read-only\n");
        acsiSetRwStatus(-13);
        break;
      }

      uint32_t logicalSectorCount = totalBytes / recsize;
      uint32_t physicalSectorCount = acsiLogicalToPhysicalRatios[driveNumber];
      uint64_t physicalSector =
          (uint64_t)acsiPunInfoStartSectors[driveNumber] +
          ((uint64_t)startLogicalSector * (uint64_t)physicalSectorCount);
      uint64_t totalPhysical =
          (uint64_t)logicalSectorCount * (uint64_t)physicalSectorCount;

      if ((physicalSector + totalPhysical) > 0xFFFFFFFFu ||
          totalPhysical > 0xFFFFu) {
        DPRINTF(
            "ACSI WRITE_BATCH out of range drive=%c recno=%lu count=%lu\n",
            acsiDriveNumberToLetter(driveNumber),
            (unsigned long)startLogicalSector,
            (unsigned long)logicalSectorCount);
        acsiSetRwStatus(-11);
        break;
      }

      fr = acsi_image_write_sectors(
          &acsiRuntimeImage, (uint32_t)physicalSector,
          (uint16_t)totalPhysical,
          (void *)(uintptr_t)(memorySharedAddress +
                               ACSIEMUL_IMAGE_BUFFER_OFFSET),
          totalBytes);
      if (fr != FR_OK) {
        DPRINTF(
            "ACSI WRITE_BATCH error drive=%c recno=%lu count=%lu (%d)\n",
            acsiDriveNumberToLetter(driveNumber),
            (unsigned long)startLogicalSector,
            (unsigned long)logicalSectorCount, (int)fr);
        acsiSetRwStatus(-10);
        break;
      }

      DPRINTF(
          "ACSI WRITE_BATCH ok drive=%c recno=%lu count=%lu total=%lu\n",
          acsiDriveNumberToLetter(driveNumber),
          (unsigned long)startLogicalSector,
          (unsigned long)logicalSectorCount, (unsigned long)totalBytes);
      acsiMarkWriteDirty();
      acsiSetRwStatus(0);
    } break;
    case ACSIEMUL_DEBUG: {
      uint32_t hookId = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);
      if ((hookId == ACSIEMUL_DEBUG_HDV_MEDIACH_STATUS) &&
          (lastProtocol->payload_size >= 8u)) {
        uint32_t driveAndStatus = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        acsiTraceMediachStatus((uint16_t)(driveAndStatus >> 16),
                               (uint16_t)(driveAndStatus & 0xFFFFu));
        break;
      }
      if ((hookId == ACSIEMUL_DEBUG_HDV_BPB_DATA) &&
          (lastProtocol->payload_size >= 16u)) {
        uint32_t driveAndPhase = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        uint32_t value1 = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        uint32_t value2 = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        acsiTraceBpbData(driveAndPhase, value1, value2);
        break;
      }
      if ((hookId == ACSIEMUL_DEBUG_MEM_MAP_A) &&
          (lastProtocol->payload_size >= 12u)) {
        uint32_t phystop = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        uint32_t membot = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        DPRINTF("ACSI mem phystop=%08lX _membot=%08lX\n",
                (unsigned long)phystop, (unsigned long)membot);
        break;
      }
      if ((hookId == ACSIEMUL_DEBUG_MEM_MAP_B) &&
          (lastProtocol->payload_size >= 12u)) {
        uint32_t memtop = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        uint32_t vbasad = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        DPRINTF("ACSI mem _memtop=%08lX _v_bas_ad=%08lX\n",
                (unsigned long)memtop, (unsigned long)vbasad);
        break;
      }
      if ((hookId == ACSIEMUL_DEBUG_REBIND_DECISION) &&
          (lastProtocol->payload_size >= 16u)) {
        uint32_t decision = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        uint32_t need = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        uint32_t reserve = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
        DPRINTF("ACSI rebind decision=%s need=%lu reserve=%lu\n",
                acsiRebindDecisionName(decision), (unsigned long)need,
                (unsigned long)reserve);
        break;
      }
      uint32_t driveNumber = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
      if (hookId == ACSIEMUL_DEBUG_DRVBITS) {
        acsiTraceDrvbits(driveNumber);
        break;
      }
      if ((hookId == ACSIEMUL_DEBUG_FIRST_VOLUME_DRIVE) ||
          (hookId == ACSIEMUL_DEBUG_LAST_VOLUME_DRIVE) ||
          (hookId == ACSIEMUL_DEBUG_DRVBITS_MASK)) {
        acsiTraceDebugValue(hookId, driveNumber);
        break;
      }
      acsiTraceHookDrive(hookId, driveNumber);
    } break;
    case ACSIEMUL_SET_SHARED_VAR: {
      uint32_t sharedVarIdx = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);
      uint32_t sharedVarValue = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
      SET_SHARED_VAR(sharedVarIdx, sharedVarValue, memorySharedAddress,
                     ACSIEMUL_SHARED_VARIABLES_OFFSET);
      if (sharedVarIdx == ACSIEMUL_SVAR_HOOKS_INSTALLED) {
        acsiTraceHookInstallSummary();
      }
    } break;
    default:
      DPRINTF("ACSI placeholder ignoring command 0x%04X\n",
              lastProtocol->command_id);
      break;
  }
}
