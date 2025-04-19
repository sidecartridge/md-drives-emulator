/**
 * File: sdcard.h
 * Author: Diego Parrilla Santamaría
 * Date: December 2024
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: Header for sdcard.c which manages the SD card
 */

#ifndef SDCARD_H
#define SDCARD_H

#include "constants.h"
#include "debug.h"
#include "gconfig.h"
#include "sd_card.h"
#include "sdcard.h"

typedef enum {
  SDCARD_INIT_OK = 0,
  SDCARD_INIT_ERROR = -1,
  SDCARD_MOUNT_ERROR = -2,
  SDCARD_CREATE_FOLDER_ERROR = -3
} sdcard_status_t;

#define SDCARD_KILOBAUD 1000

#define NUM_BYTES_PER_SECTOR 512
#define SDCARD_MEGABYTE 1048576

/**
 * @brief Mount filesystem using FatFS library.
 *
 * Attempts to mount the provided filesystem on the given drive. Incorporates
 * error checking based on FatFS return codes.
 *
 * @param fsys Pointer to a FATFS structure to be associated with the drive.
 * @param drive Drive identifier string.
 * @return FRESULT Returns the FatFS function result code.
 */
FRESULT sdcard_mountFilesystem(FATFS *fsys, const char *drive);

/**
 * @brief Verify the existence of a directory.
 *
 * Leverages FatFS f_stat to determine if the specified directory exists and is
 * accessible.
 *
 * @param dir Null-terminated string containing the directory path.
 * @return bool Returns true if the directory exists; false otherwise.
 */
bool sdcard_dirExist(const char *dir);

/**
 * @brief Initialize filesystem on SD card.
 *
 * Sets up the SD card filesystem by mounting it and preparing the designated
 * folder. Returns specific status codes reflecting success or the nature of any
 * initialization failure. The folder is created if it does not already exist.
 *
 * @param fsPtr Pointer to a FATFS structure used for filesystem mounting.
 * @param folderName Name of the folder to be created or used on the SD card.
 * @return sdcard_status_t Status code indicating the initialization result.
 */
sdcard_status_t sdcard_initFilesystem(FATFS *fsPtr, const char *folderName);

/**
 * @brief Adjust the SPI communication speed.
 *
 * Alters the SPI baud rate using a configuration entry. Verifies the provided
 * rate and defaults to a preset value if the given baud rate is invalid.
 *
 * @param baudRateKbits Desired SPI speed in kilobits per second.
 */
void sdcard_changeSpiSpeed(int baudRateKbits);

/**
 * @brief Establish SPI speed configuration.
 *
 * Applies the SPI speed settings to align communication parameters for SD card
 * operations.
 */
void sdcard_setSpiSpeedSettings();

/**
 * @brief Retrieve SD card storage information.
 *
 * Obtains details regarding the SD card by determining both total capacity and
 * available free space in megabytes.
 *
 * @param fsPtr Pointer to the FATFS object associated with the SD card.
 * @param totalSizeMb Pointer to a variable where the total storage (in MB) is
 * stored.
 * @param freeSpaceMb Pointer to a variable where the free space (in MB) is
 * stored.
 */
void sdcard_getInfo(FATFS *fsPtr, uint32_t *totalSizeMb, uint32_t *freeSpaceMb);

// Hardware Configuration of SPI "objects"

// NOLINTBEGIN(readability-identifier-naming)
size_t sd_get_num();
sd_card_t *sd_get_by_num(size_t num);
size_t spi_get_num();
spi_t *spi_get_by_num(size_t num);
// NOLINTEND(readability-identifier-naming)

#endif  // SDCARD_H
