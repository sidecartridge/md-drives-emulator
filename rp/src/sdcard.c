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

static void sanitizeDOSName(char *name) {
  if (!name) return;
  static const char invalid[] = "<>:\"/\\|?*";
  // Replace control or invalid chars and uppercase the rest
  for (char *p = name; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (iscntrl(c) || strchr(invalid, c)) {
      *p = '_';
    } else {
      *p = (char)toupper(c);
    }
  }
  // Trim trailing spaces and dots
  size_t len = strlen(name);
  while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '.')) {
    name[--len] = '\0';
  }
}

// Comparison function for qsort
static int dirFirstCmp(const void *a, const void *b) {
  const DirEntry *e1 = (const DirEntry *)a;
  const DirEntry *e2 = (const DirEntry *)b;

  // Directories first
  if (e1->is_dir && !e2->is_dir) return -1;
  if (!e1->is_dir && e2->is_dir) return 1;

  // Both same type: lexicographic compare
  return strcasecmp(e1->name, e2->name);
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
    // If the folder is empty or '/' then ignore it
    if (strcmp(folderName, "") == 0 || strcmp(folderName, "/") == 0) {
      DPRINTF("Empty folder name. Ignoring.\n");
      return SDCARD_INIT_OK;
    }
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

FRESULT sdcard_loadDirectory(const char *path,
                             char entries_arr[][MAX_FILENAME_LENGTH + 1],
                             uint16_t *entry_count, uint16_t *selected,
                             uint16_t *page, bool dirs_only,
                             EntryFilterFn filter_fn,
                             char top_dir[MAX_FILENAME_LENGTH + 1]) {
  FILINFO fno;
  DIR dir;
  FRESULT res;
  *entry_count = 0;

  DirEntry temp_entries[MAX_ENTRIES_DIR];
  uint16_t temp_count = 0;
  bool has_parent = false;

  // Add ".." if not at root or top_dir
  if ((strcmp(path, "/") != 0) &&
      (strlen(top_dir) > 0 && strcmp(path, top_dir) != 0)) {
    snprintf(entries_arr[*entry_count], MAX_FILENAME_LENGTH + 1, "..");
    (*entry_count)++;
    has_parent = true;
  }

  res = f_opendir(&dir, path);
  if (res != FR_OK) {
    DPRINTF("Error opening directory: %d\n", res);
    return res;
  }

  while ((res = f_readdir(&dir, &fno)) == FR_OK && fno.fname[0]) {
    if (dirs_only && !(fno.fattrib & AM_DIR)) continue;
    if (filter_fn && !filter_fn(fno.fname, fno.fattrib)) continue;
    if (temp_count >= MAX_ENTRIES_DIR) break;

    snprintf(temp_entries[temp_count].name, MAX_FILENAME_LENGTH + 1, "%s%s",
             fno.fname, (fno.fattrib & AM_DIR) ? "/" : "");
    temp_entries[temp_count].is_dir = (fno.fattrib & AM_DIR) != 0;
    temp_count++;
  }

  f_closedir(&dir);

  // Sort temporary list: directories first, then files, both alphabetically
  qsort(temp_entries, temp_count, sizeof(DirEntry), dirFirstCmp);

  // Copy sorted names into output array
  for (uint16_t i = 0; i < temp_count; i++) {
    strncpy(entries_arr[*entry_count], temp_entries[i].name,
            MAX_FILENAME_LENGTH);
    entries_arr[*entry_count][MAX_FILENAME_LENGTH] =
        '\0';  // ensure null-terminated
    (*entry_count)++;
  }

  *page = 0;
  *selected = 0;

  DPRINTF("Loaded %d entries\n", *entry_count);
  return FR_OK;
}
void sdcard_splitFullpath(const char *fullPath, char *drive, char *folders,
                          char *filePattern) {
  const char *driveEnd;
  const char *pathEnd;

  // Initialize the output strings
  drive[0] = '\0';
  folders[0] = '\0';
  filePattern[0] = '\0';

  // Define max sizes matching caller buffers
  const size_t maxDrive = 10;
  const size_t maxFolders = 128;
  const size_t maxPattern = 100;

  // Find the position of the first ':' to identify the drive
  driveEnd = strchr(fullPath, ':');
  if (driveEnd != NULL) {
    size_t dlen = driveEnd - fullPath + 1;
    if (dlen >= maxDrive) dlen = maxDrive - 1;
    strncpy(drive, fullPath, dlen);
    drive[dlen] = '\0';
    fullPath = driveEnd + 1;  // Adjust fullPath to point after the drive letter
  }

  // Find the last '\\' or '/' to separate folders and file pattern
  char slash = fullPath[strcspn(fullPath, "\\/")] == '\\' ? '\\' : '/';
  pathEnd = strrchr(fullPath, slash);
  if (pathEnd != NULL) {
    size_t flen = pathEnd - fullPath + 1;
    if (flen >= maxFolders) flen = maxFolders - 1;
    strncpy(folders, fullPath, flen);
    folders[flen] = '\0';
    size_t plen = strlen(pathEnd + 1);
    if (plen >= maxPattern) plen = maxPattern - 1;
    strncpy(filePattern, pathEnd + 1, plen);
    filePattern[plen] = '\0';
  } else {
    // No separator: full path is the pattern
    size_t plen = strlen(fullPath);
    if (plen >= maxPattern) plen = maxPattern - 1;
    strncpy(filePattern, fullPath, plen);
    filePattern[plen] = '\0';
  }
}

void sdcard_back2ForwardSlash(char *path) {
  if (path == NULL) return;

  for (char *p = path; p && *p; ++p)
    if (*p == '\\') *p = '/';
}

uint8_t sdcard_attribsFAT2ST(uint8_t fat_attribs) {
  // FATFS attribute bits match FS_ST_* bit positions (except volume-label)
  const uint8_t validMask = AM_RDO | AM_HID | AM_SYS | AM_DIR | AM_ARC;
  // Return only the valid bits; callers treat these as ST attributes
  return fat_attribs & validMask;
}

uint8_t sdcard_attribsST2FAT(uint8_t st_attribs) {
  uint8_t fat_attribs = 0;
  if (st_attribs & FS_ST_READONLY) fat_attribs |= AM_RDO;
  if (st_attribs & FS_ST_HIDDEN) fat_attribs |= AM_HID;
  if (st_attribs & FS_ST_SYSTEM) fat_attribs |= AM_SYS;
  if (st_attribs & FS_ST_FOLDER) fat_attribs |= AM_DIR;
  if (st_attribs & FS_ST_ARCH) fat_attribs |= AM_ARC;
  return fat_attribs;
}

void sdcard_getAttribsSTStr(char *attribs_str, uint8_t st_attribs) {
  if (!attribs_str) return;
  // Fill default dashes and null terminator
  static const char defaults[] = "------";
  memcpy(attribs_str, defaults, sizeof(defaults));
  // Replace positions based on ST attribute flags
  if (st_attribs & FS_ST_READONLY) attribs_str[0] = 'R';
  if (st_attribs & FS_ST_HIDDEN) attribs_str[1] = 'H';
  if (st_attribs & FS_ST_SYSTEM) attribs_str[2] = 'S';
  if (st_attribs & FS_ST_LABEL) attribs_str[3] = 'L';
  if (st_attribs & FS_ST_FOLDER) attribs_str[4] = 'D';
  if (st_attribs & FS_ST_ARCH) attribs_str[5] = 'A';
}

void sdcard_removeDupSlashes(char *str) {
  if (!str) return;
  char *read = str;
  char *write = str;
  bool prev_slash = false;
  while (*read) {
    if (*read == '/') {
      if (!prev_slash) {
        *write++ = '/';
        prev_slash = true;
      }
      // else skip duplicate slash
    } else {
      *write++ = *read;
      prev_slash = false;
    }
    read++;
  }
  *write = '\0';
}

void sdcard_forward2Backslash(char *path) {
  // Convert all '/' to '\' in-place
  if (!path) return;
  for (char *p = path; *p; ++p) {
    if (*p == '/') {
      *p = '\\';
    }
  }
}

void sdcard_removeTrailingSlashes(char *path) {
  if (!path) return;
  size_t len = strlen(path);
  while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
    path[--len] = '\0';
  }
}

void sdcard_filterFname(const char *originalName, char filteredName[14]) {
  if (!originalName || !filteredName) return;
  // Allow alphanumeric and select punctuation in filenames
  static const char *allowed = "_!@#$%^&()+=-~`;'<,>.|[]{}";
  const size_t maxLen = 13;
  size_t j = 0;
  for (size_t i = 0; originalName[i] && j < maxLen; ++i) {
    unsigned char c = (unsigned char)originalName[i];
    if (isalnum(c) || strchr(allowed, c)) {
      filteredName[j++] = c;
    }
  }
  filteredName[j] = '\0';
}

void sdcard_upperFname(const char *originalName, char upperName[14]) {
  if (!originalName || !upperName) return;
  const size_t maxLen = 13;
  size_t i = 0;
  for (; i < maxLen && originalName[i]; ++i) {
    upperName[i] = (char)toupper((unsigned char)originalName[i]);
  }
  upperName[i] = '\0';  // Null-terminate safely
}

void sdcard_shortenFname(const char *originalName, char shortenedName[13]) {
  if (!originalName || !shortenedName) return;
  // Use enum for compile-time constants to allow static array initialization
  enum { NAME_LEN = 8, EXT_LEN = 3 };
  char namePart[NAME_LEN + 1] = {0};
  char extPart[EXT_LEN + 1] = {0};

  // Split base and extension
  const char *dot = strrchr(originalName, '.');
  size_t baseLen = dot && dot != originalName ? (size_t)(dot - originalName)
                                              : strlen(originalName);
  if (dot && dot != originalName) {
    size_t e = strlen(dot + 1);
    if (e > EXT_LEN) e = EXT_LEN;
    strncpy(extPart, dot + 1, e);
    extPart[e] = '\0';
  }

  // Copy and, if needed, suffix tilde
  size_t copyLen = baseLen > NAME_LEN ? NAME_LEN : baseLen;
  strncpy(namePart, originalName, copyLen);
  namePart[copyLen] = '\0';
  if (baseLen > NAME_LEN) {
    // Omit last two chars, append ~1
    namePart[NAME_LEN - 2] = '~';
    namePart[NAME_LEN - 1] = '1';
    namePart[NAME_LEN] = '\0';
  }

  // Sanitize and uppercase parts
  sanitizeDOSName(namePart);
  sanitizeDOSName(extPart);

  // Build final string
  if (extPart[0]) {
    snprintf(shortenedName, NAME_LEN + EXT_LEN + 2, "%s.%s", namePart, extPart);
  } else {
    strncpy(shortenedName, namePart, NAME_LEN + 1);
  }
}

void sdcard_normalizePath(char *path) {
  char *segments[32];
  int seg_count = 0;

  // Work on a temporary buffer to tokenize safely
  char temp[MAX_FILENAME_LENGTH];
  strncpy(temp, path, sizeof(temp) - 1);
  temp[sizeof(temp) - 1] = '\0';

  char *token = strtok(temp, "/\\");
  while (token) {
    if (strcmp(token, "..") == 0) {
      if (seg_count > 0) seg_count--;
    } else if (strcmp(token, ".") != 0 && token[0] != '\0') {
      segments[seg_count++] = token;
    }
    token = strtok(NULL, "/\\");
  }

  // Rebuild normalized path
  char result[MAX_FILENAME_LENGTH] = "";
  for (int i = 0; i < seg_count; ++i) {
    strncat(result, "/", sizeof(result) - strlen(result) - 1);
    strncat(result, segments[i], sizeof(result) - strlen(result) - 1);
  }

  if (seg_count == 0) {
    strncpy(path, "/", MAX_FILENAME_LENGTH);
  } else {
    strncpy(path, result, MAX_FILENAME_LENGTH);
  }
  path[MAX_FILENAME_LENGTH - 1] = '\0';
}