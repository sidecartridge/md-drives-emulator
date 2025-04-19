#include "sdcard.h"

static sdcard_status_t sdcardInit() {
  DPRINTF("Initializing SD card...\n");
  // Initialize the SD card
  bool success = sd_init_driver();
  if (!success) {
    DPRINTF("ERROR: Could not initialize SD card\r\n");
    return SDCARD_INIT_ERROR;
  }
  DPRINTF("SD card initialized.\n");

  sdcard_setSpiSpeedSettings();
  return SDCARD_INIT_OK;
}

FRESULT sdcard_mountFilesystem(FATFS *fsys, const char *drive) {
  // Mount the drive
  FRESULT fres = f_mount(fsys, drive, 1);
  if (fres != FR_OK) {
    DPRINTF("ERROR: Could not mount the filesystem. Error code: %d\n", fres);
  } else {
    DPRINTF("Filesystem mounted.\n");
  }
  return fres;
}

bool sdcard_dirExist(const char *dir) {
  FILINFO fno;
  FRESULT res = f_stat(dir, &fno);

  // Check if the result is OK and if the attribute indicates it's a directory
  bool dirExist = (res == FR_OK && (fno.fattrib & AM_DIR));
  DPRINTF("Directory %s exists: %s\n", dir, dirExist ? "true" : "false");
  return dirExist;
}

sdcard_status_t sdcard_initFilesystem(FATFS *fsPtr, const char *folderName) {
  // Check the status of the sd card
  sdcard_status_t sdcardOk = sdcardInit();
  if (sdcardOk != SDCARD_INIT_OK) {
    DPRINTF("Error initializing the SD card.\n");
    return SDCARD_INIT_ERROR;
  }

  // Now try to mount the filesystem
  FRESULT fres;
  fres = sdcard_mountFilesystem(fsPtr, "0:");
  if (fres != FR_OK) {
    DPRINTF("Error mounting the filesystem.\n");
    return SDCARD_MOUNT_ERROR;
  }
  DPRINTF("Filesystem mounted.\n");

  // Now check if the folder exists in the SD card
  bool folderExists = sdcard_dirExist(folderName);
  DPRINTF("Folder exists: %s\n", folderExists ? "true" : "false");

  // If the folder does not exist, try to create it
  if (!folderExists) {
    // Create the folder
    fres = f_mkdir(folderName);
    if (fres != FR_OK) {
      DPRINTF("Error creating the folder.\n");
      return SDCARD_CREATE_FOLDER_ERROR;
    }
    DPRINTF("Folder created.\n");
  }
  return SDCARD_INIT_OK;
}

void sdcard_changeSpiSpeed(int baudRateKbits) {
  size_t sdNum = sd_get_num();
  if (sdNum > 0) {
    int baudRate = baudRateKbits;
    if (baudRate > 0) {
      DPRINTF("Changing SD card baud rate to %i\n", baudRate);
      sd_card_t *sdCard = sd_get_by_num(sdNum - 1);
      sdCard->spi_if_p->spi->baud_rate = baudRate * SDCARD_KILOBAUD;
    } else {
      DPRINTF("Invalid baud rate. Using default value\n");
    }
  } else {
    DPRINTF("SD card not found\n");
  }
}

void sdcard_setSpiSpeedSettings() {
  // Get the SPI speed from the configuration
  SettingsConfigEntry *spiSpeed =
      settings_find_entry(gconfig_getContext(), PARAM_SD_BAUD_RATE_KB);
  int baudRate = 0;
  if (spiSpeed != NULL) {
    baudRate = atoi(spiSpeed->value);
  }
  sdcard_changeSpiSpeed(baudRate);
}

void sdcard_getInfo(FATFS *fsPtr, uint32_t *totalSizeMb,
                    uint32_t *freeSpaceMb) {
  DWORD freClust;

  // Set initial values to zero as a precaution
  *totalSizeMb = 0;
  *freeSpaceMb = 0;

  // Get volume information and free clusters of drive
  FRESULT res = f_getfree("", &freClust, &fsPtr);
  if (res != FR_OK) {
    DPRINTF("Error getting free space information: %d\n", res);
    return;  // Error handling: Set values to zero if getfree fails
  }

  // Calculate total sectors in the SD card
  uint64_t totalSectors = (fsPtr->n_fatent - 2) * fsPtr->csize;

  // Convert total sectors to bytes and then to megabytes
  *totalSizeMb = (totalSectors * NUM_BYTES_PER_SECTOR) / SDCARD_MEGABYTE;

  // Convert free clusters to sectors and then to bytes
  uint64_t freeSpaceBytes =
      (uint64_t)freClust * fsPtr->csize * NUM_BYTES_PER_SECTOR;

  // Convert bytes to megabytes
  *freeSpaceMb = freeSpaceBytes / SDCARD_MEGABYTE;
}
