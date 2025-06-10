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

#define SDCARD_KILOBAUD 1000

#define NUM_BYTES_PER_SECTOR 512
#define SDCARD_MEGABYTE 1048576

// File/dir entry list browsable
#define MAX_ENTRIES_DIR 256
#define MAX_FILENAME_LENGTH \
  (SETTINGS_MAX_VALUE_LENGTH - 1)  // Max length of a filename

#define FS_ST_READONLY 0x1  // Read only
#define FS_ST_HIDDEN 0x2    // Hidden
#define FS_ST_SYSTEM 0x4    // System
#define FS_ST_LABEL 0x8     // Volume label
#define FS_ST_FOLDER 0x10   // Directory
#define FS_ST_ARCH 0x20     // Archive

typedef enum {
  SDCARD_INIT_OK = 0,
  SDCARD_INIT_ERROR = -1,
  SDCARD_MOUNT_ERROR = -2,
  SDCARD_CREATE_FOLDER_ERROR = -3
} sdcard_status_t;

typedef struct {
  char name[MAX_FILENAME_LENGTH + 1];
  bool is_dir;
} DirEntry;

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

// Prototype for filter callback: return true to include the entry, false to
// skip
typedef bool (*EntryFilterFn)(const char *name, BYTE attr);

/**
 * @brief Load entries from an SD card directory.
 *
 * This function reads the contents of the directory specified by @c path and
 * populates an array with the names of the entries found. It supports filtering
 * entries using an optional filter function and can be configured to load only
 * directories.
 *
 * @param path The path to the directory to be loaded.
 * @param entries_arr A 2D character array where the directory entry names will
 * be stored. Each entry should have a maximum length of @c MAX_FILENAME_LENGTH
 * characters, plus a null terminator.
 * @param entry_count Pointer to a uint16_t where the number of entries found
 * will be stored.
 * @param selected Pointer to a uint16_t representing the initially selected
 * entry's index.
 * @param page Pointer to a uint16_t representing the current page of entries
 * (for pagination purposes).
 * @param dirs_only If set to true, only directory entries will be loaded.
 * @param filter_fn Optional pointer to a function used to filter entries; only
 * entries for which this function returns true will be included.
 *
 * @return FRESULT Return code indicating the status of the operation.
 */
FRESULT __not_in_flash_func(sdcard_loadDirectory)(
    const char *path, char entries_arr[][MAX_FILENAME_LENGTH + 1],
    uint16_t *entry_count, uint16_t *selected, uint16_t *page, bool dirs_only,
    EntryFilterFn filter_fn, char top_dir[MAX_FILENAME_LENGTH + 1]);

/**
 * @brief Splits a full path into its drive letter, folder path, and file
 * pattern components.
 *
 * This function takes a full path that includes a drive letter, a path of
 * folders, and a file pattern, and splits it into three separate components.
 * The drive letter is identified by the colon following it, the folder path is
 * the part of the path before the file pattern, and the file pattern is the
 * last segment of the path. The function handles various cases, including paths
 * without a drive letter or folder path.
 *
 * @param fullPath A string representing the full path to split. It should
 * include the drive letter, folder path, and file pattern. Example:
 * "C:\\Users\\Public\\Documents\\*.txt".
 * @param drive A pointer to a character array where the extracted drive letter
 * will be stored.
 * @param folders A pointer to a character array where the extracted folder path
 * will be stored.
 * @param filePattern A pointer to a character array where the extracted file
 * pattern will be stored.
 *
 * Example usage:
 *     char drive[10], folders[256], filePattern[100];
 *     sdcard_splitFullpath("C:\\Users\\Public\\Documents\\*.txt", drive,
 * folders, filePattern);
 *     // `drive`, `folders`, and `filePattern` now contain the respective
 * components of the path.
 */
void sdcard_splitFullpath(const char *fullPath, char *drive, char *folders,
                          char *filePattern);

/**
 * @brief Converts all backslash characters to forward slashes in a given
 * string.
 *
 * This function iterates through the characters of the provided string and
 * replaces each backslash ('\\') character with a forward slash ('/'). This is
 * typically used to convert file paths from Windows-style to Unix-style. The
 * function operates in place, modifying the original string. It is safe to use
 * with strings that do not contain backslashes, as the function will simply
 * leave them unchanged.
 *
 * @param path A pointer to a character array (string) that will be modified in
 * place. The array should be null-terminated.
 *
 * Example usage:
 *     char path[] = "C:\\Users\\Public\\Documents\\file.txt";
 *     sdcard_back2ForwardSlash(path);
 *     // `path` is now "C:/Users/Public/Documents/file.txt"
 */
void sdcard_back2ForwardSlash(char *path);

/**
 * @brief Converts FATFS attributes to ST attributes.
 *
 * This function takes FATFS attributes as input and converts them to ST
 * attributes. The conversion is done by checking each bit of the FAT attributes
 * and setting the corresponding bit in the ST attributes if it is set in the
 * FATFS attributes.
 *
 * @param fat_attribs The FATFS attributes to be converted.
 * @return The converted ST attributes.
 */
uint8_t sdcard_attribsFAT2ST(uint8_t fat_attribs);

/**
 * @brief Converts ST attributes to FATFS attributes.
 *
 * This function takes ST attributes as input and converts them to FATFS
 * attributes. The conversion is done by checking each bit of the ST attributes
 * and setting the corresponding bit in the FATFS attributes if it is set in the
 * ST attributes.
 *
 * @param st_attribs The ST attributes to be converted.
 * @return The converted FATFS attributes.
 */
uint8_t sdcard_attribsST2FAT(uint8_t st_attribs);

/**
 * @brief Converts ST attributes to a string representation.
 *
 * This function takes ST attributes as input and converts them to a string
 * representation. The conversion is done by checking each bit of the ST
 * attributes and setting the corresponding character in the string if it is set
 * in the ST attributes.
 *
 * @param attribs_str The string to store the representation. It should be at
 * least 6 characters long.
 * @param st_attribs The ST attributes to be converted.
 */
void sdcard_getAttribsSTStr(char *attribs_str, uint8_t st_attribs);

/**
 * @brief Removes consecutive duplicate slashes from a string.
 *
 * This function iterates through the provided string, `str`, and when it
 * finds two consecutive forward slashes, it removes one of them. This
 * process is done in-place, modifying the original string.
 *
 * @param str The string from which duplicate slashes are to be removed.
 *            This parameter is modified in place.
 *
 * @note The function modifies the string in place. Ensure the provided
 *       string is modifiable and not a string literal.
 *
 * Example Usage:
 *     char path[] = "path//to//your//directory/";
 *     sdcard_removeDupSlashes(path);
 *     // path is now "path/to/your/directory/"
 */
void sdcard_removeDupSlashes(char *str);

/**
 * @brief Removes trailing slashes from the specified file path.
 *
 * This function processes the provided path string in place, eliminating
 * any trailing '/' characters. This normalization helps ensure consistent
 * file path representations by removing redundant delimiters at the end of the
 * path.
 *
 * @param[in,out] path A pointer to a null-terminated string representing the
 * file path.
 */
void sdcard_removeTrailingSlashes(char *path);

/**
 * @brief Converts all forward slash characters to backslashes in a given
 * string.
 *
 * This function iterates through the characters of the provided string and
 * replaces each forward slash ('/') character with a backslash ('\\'). This
 * is typically used to convert file paths from Unix-style to Windows-style.
 * The function operates in place, modifying the original string. It is safe
 * to use with strings that do not contain forward slashes, as the function
 * will simply leave them unchanged.
 *
 * @param path A pointer to a character array (string) that will be modified
 * in place. The array should be null-terminated.
 *
 * Example usage:
 *     char path[] = "C:/Users/Public/Documents/file.txt";
 *     sdcard_forward2Backslash(path);
 *     // `path` is now "C:\\Users\\Public\\Documents\\file.txt"
 */
void sdcard_forward2Backslash(char *path);

/**
 * @brief Filters a filename.
 *
 * This function takes a filename as input and filters it by removing
 * non-alphanumeric characters. The filtered filename is stored in a separate
 * string. The original filename remains unchanged. The function ensures that
 * the filtered filename is null-terminated.
 *
 * @param originalName The original filename to be filtered. It should be a
 * null-terminated string.
 * @param filteredName The string to store the filtered filename. It should be
 * at least 14 characters long.
 */
void sdcard_filterFname(const char *originalName, char filteredName[14]);

/**
 * @brief Converts a filename to uppercase.
 *
 * This function takes a filename as input and converts all its characters to
 * uppercase. The converted filename is stored in a separate string. The
 * original filename remains unchanged. The function ensures that the
 * converted filename is null-terminated.
 *
 * @param originalName The original filename to be converted. It should be a
 * null-terminated string.
 * @param upperName The string to store the converted filename. It should be
 * at least 14 characters long.
 */
void sdcard_upperFname(const char *originalName, char upperName[14]);

/**
 * @brief Shortens a long file name to a DOS 8.3 filename format in uppercase
 * and stores it in a provided array.
 *
 * This function takes a long file name, shortens it to comply with the
 * DOS 8.3 format, and stores it in uppercase in the provided array. For
 * filenames with multiple dots, it uses the last dot as the extension
 * separator, replaces other dots with underscores in the name part, and
 * formats the name accordingly.
 *
 * @param originalName A pointer to a constant character array containing the
 * original long file name. The name should include the extension and must be
 * null-terminated.
 * @param shortenedName A pointer to a character array of size 13 where the
 * shortened file name will be stored. This array will be modified by the
 * function to contain the new file name.
 *
 * Example usage:
 *     char shortenedFileName[13];
 *     sdcard_shortenFname("file.with.many.dots.ext", shortenedFileName);
 *     printf("Shortened File Name: %s\n", shortenedFileName); // Output:
 * FILE_W~1.EXT
 */
void sdcard_shortenFname(const char *originalName, char shortenedName[13]);

/**
 * @brief Normalizes the provided file path.
 *
 * This function modifies the given path string in-place to resolve redundant
 * or relative components in the file path. It ensures that the path adheres
 * to a consistent format, which may include tasks like removing extra
 * directory separators or handling "." and ".." components.
 *
 * @param path Pointer to the null-terminated string containing the file path.
 *             The normalized path will be written back to this string.
 */
void sdcard_normalizePath(char *path);

// Hardware Configuration of SPI "objects"
// NOLINTBEGIN(readability-identifier-naming)
size_t sd_get_num();
sd_card_t *sd_get_by_num(size_t num);
size_t spi_get_num();
spi_t *spi_get_by_num(size_t num);
// NOLINTEND(readability-identifier-naming)

#endif  // SDCARD_H
