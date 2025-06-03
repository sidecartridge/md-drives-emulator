/**
 * File: emul.c
 * Author: Diego Parrilla Santamaría
 * Date: February 2025
 * Copyright: 2025 - GOODDATA LABS
 * Description: Template code for the core emulation
 */

#include "emul.h"

// inclusw in the C file to avoid multiple definitions
#include "target_firmware.h"  // Include the target firmware binary

// Command handlers
static void cmdMenu(const char *arg);
static void cmdClear(const char *arg);
static void cmdExit(const char *arg);
static void cmdHelp(const char *arg);
static void cmdBooster(const char *arg);
static void cmdHiddenSettings(const char *arg);
static void cmdGemdriveEnabled(const char *arg);
static void cmdGemdriveFolder(const char *arg);
static void cmdGemdriveDrive(const char *arg);
static void cmdReadOnly(const char *arg);
static void cmdFloppyEnabled(const char *arg);
static void cmdFloppiesFolder(const char *arg);
static void cmdFloppyDriveA(const char *arg);
static void cmdFloppyDriveAEject(const char *arg);
static void cmdFloppyDriveB(const char *arg);
static void cmdFloppyDriveBEject(const char *arg);
static void cmdFormatFloppy(const char *arg);
static void cmdMSA2ST(const char *arg);
static void cmdBootEnabled(const char *arg);
static void cmdXbiosEnabled(const char *arg);
static void cmdRTCEnabled(const char *arg);
static void cmdY2KPatch(const char *arg);
static void cmdUTCOffset(const char *arg);
static void cmdHost(const char *arg);
static void cmdPort(const char *arg);

// Command table
static const Command commands[] = {
    {" ", cmdMenu},
    {"m", cmdMenu},
    {"e", cmdExit},
    {"x", cmdBooster},
    {"g", cmdGemdriveEnabled},
    {"o", cmdGemdriveFolder},
    {"d", cmdGemdriveDrive},
    {"n", cmdReadOnly},
    {"f", cmdFloppyEnabled},
    {"l", cmdFloppiesFolder},
    {"a", cmdFloppyDriveA},
    {"A", cmdFloppyDriveAEject},
    {"b", cmdFloppyDriveB},
    {"B", cmdFloppyDriveBEject},
    {"i", cmdFormatFloppy},
    {"c", cmdMSA2ST},
    {"t", cmdBootEnabled},
    {"s", cmdXbiosEnabled},
    {"r", cmdRTCEnabled},
    {"y", cmdY2KPatch},
    {"u", cmdUTCOffset},
    {"h", cmdHost},
    {"p", cmdPort},
    {"?", cmdHiddenSettings},
    {"print", term_cmdPrint},
    {"save", term_cmdSave},
    {"erase", term_cmdErase},
    {"get", term_cmdGet},
    {"put_int", term_cmdPutInt},
    {"put_bool", term_cmdPutBool},
    {"put_str", term_cmdPutString},
};

// Number of commands in the table
static const size_t numCommands = sizeof(commands) / sizeof(commands[0]);

// FatFS object

static FATFS fsys = {0};

// Boot countdown
static int countdown = 0;

// Halt the contdown
static bool haltCountdown = false;

// Keep active loop or exit
static bool keepActive = true;

// Jump to the booster app
static bool jumpBooster = false;

// GEM launched
static bool gemLaunched = false;

// Do we have network or not?
static bool hasNetwork = false;

// app status
static int appStatus = APP_MODE_SETUP;

// USB Mass Storage ready
static bool usbMassStorageReady = false;
static bool usbMassStorageReadyPrevious = false;

// Folder search
#define NAV_LINES_PER_PAGE 16
#define NAV_LINES_PER_PAGE_OFFSET 4
enum navStatus {
  NAV_DIR_ERROR = -1,
  NAV_DIR_FIRST_TIME_OK = 0,
  NAV_DIR_NEXT_TIME_OK = 1,
  NAV_DIR_SELECTED = 2,
  NAV_DIR_CANCEL = 3,
};

typedef struct {
  uint16_t count;
  uint16_t selected;
  uint16_t page;
  char entries[MAX_ENTRIES_DIR][MAX_FILENAME_LENGTH + 1];
  char folderPath[MAX_FILENAME_LENGTH + 1];
  char topDir[MAX_FILENAME_LENGTH + 1];
} DirNavigation;

static DirNavigation *navState = NULL;

static FloppyFormatState floppyFormatState = FLOPPY_FORMAT_SIZE_STATE;
static FloppyImageHeader floppyImageHeader = {0};

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

char *right(const char *str, int n) {
  if (str == NULL || n < 0) return NULL;

  int len = strlen(str);
  if (n == 0) return strdup("");

  // If n is greater than or equal to the length, return full string
  if (n >= len) return strdup(str);

  // If n is smaller, return the last n characters prefixed with ".."
  const char *suffix = str + len - n;

  char *result = malloc(n + 3);  // ".." + n chars + null terminator
  if (!result) return NULL;

  strcpy(result, "..");
  strncpy(result + 2, suffix, n);
  result[n + 2] = '\0';
  return result;
}

// Function to verify if a domain name is valid
static bool isValidDomain(const char *domain) {
  if (domain == NULL) return false;
  size_t len = strlen(domain);
  if (len == 0 || len > MAX_DOMAIN_LENGTH) {
    return false;
  }

  int label_length = 0;
  for (size_t i = 0; i < len; i++) {
    char c = domain[i];

    if (c == '.') {
      // Dot found: end of a label
      if (label_length ==
          0) {  // Empty label (e.g., consecutive dots, leading dot)
        return false;
      }
      label_length = 0;  // Reset for the next label
    } else {
      // Check for valid characters: letters, digits, or hyphen.
      if (!(isalnum((unsigned char)c) || c == '-')) {
        return false;
      }
      // The first character of a label cannot be a hyphen.
      if (label_length == 0 && c == '-') {
        return false;
      }
      label_length++;
      if (label_length > MAX_LABEL_LENGTH) {
        return false;  // Label too long.
      }
    }
  }

  // After looping, ensure the last label is not empty and does not end with a
  // hyphen.
  if (label_length == 0 || domain[len - 1] == '-') {
    return false;
  }
  return true;
}

// Check if the input buffer is a valid drive letter
static bool isValidDrive(const char *drive) {
  if (drive == NULL || drive[0] == '\0') {
    return false;
  }
  char c = toupper((unsigned char)drive[0]);
  return (c >= 'C' && c <= 'Z');
}

// Remove last path component (".." navigation)
static void pathUp() {
  char temp[MAX_FILENAME_LENGTH + 1];
  char *segments[MAX_ENTRIES_DIR];
  int sp = 0;

  // Copy and tokenize
  strncpy(temp, navState->folderPath, sizeof(temp));
  temp[sizeof(temp) - 1] = '\0';

  char *token = strtok(temp, "/");
  while (token) {
    if (strcmp(token, "..") == 0) {
      // Pop one segment if available
      if (sp > 0) sp--;
    } else if (strcmp(token, "") != 0) {
      // Regular segment
      segments[sp++] = token;
    }
    token = strtok(NULL, "/");
  }

  // Rebuild navState->folderPath
  if (sp == 0) {
    // Root
    strcpy(navState->folderPath, "/");
  } else {
    char newPath[MAX_FILENAME_LENGTH + 1] = "";
    for (int i = 0; i < sp; ++i) {
      strlcat(newPath, "/", sizeof(newPath));
      strlcat(newPath, segments[i], sizeof(newPath));
    }
    // Ensure leading slash
    if (newPath[0] != '/') {
      char tmp[MAX_FILENAME_LENGTH + 1];
      snprintf(tmp, sizeof(tmp), "/%s", newPath);
      strncpy(newPath, tmp, sizeof(newPath));
    }
    strncpy(navState->folderPath, newPath, sizeof(navState->folderPath));
    navState->folderPath[sizeof(navState->folderPath) - 1] = '\0';
  }
}

// Filter: include only files with extensions .st or .st.rw
//(case-insensitive), and omit hidden files (those starting with a dot).
static bool floppiesFilter(const char *name, BYTE attr) {
  if (name[0] == '.') {
    return false;  // skip dotfiles
  }
  if (attr & AM_DIR) {
    return true;  // directories always
  }
  size_t len = strlen(name);
  // Check for .st.rw (6 chars)
  if (len > 6 && strcasecmp(name + len - 6, ".st.rw") == 0) {
    return true;
  }
  // Check for .st (3 chars)
  if (len > 3 && strcasecmp(name + len - 3, ".st") == 0) {
    return true;
  }
  return false;
}

// Filter: include only files with extensions .msa.
//(case-insensitive), and omit hidden files (those starting with a dot).
static bool floppiesMSAFilter(const char *name, BYTE attr) {
  if (name[0] == '.') {
    return false;  // skip dotfiles
  }
  if (attr & AM_DIR) {
    return true;  // directories always
  }
  size_t len = strlen(name);
  // Check for .msa (4 chars)
  if (len > 4 && strcasecmp(name + len - 4, ".msa") == 0) {
    return true;
  }
  return false;
}

static inline void vt52Cursor(uint8_t row, uint8_t col) {
  char vt52Seq[5];
  vt52Seq[0] = '\x1B';
  vt52Seq[1] = 'Y';
  vt52Seq[2] = (char)(32 + row);
  vt52Seq[3] = (char)(32 + col);
  vt52Seq[4] = '\0';
  term_printString(vt52Seq);
}

static void showTitle() {
  term_printString(
      "\x1B"
      "E"
      "\x1Bp"
      "Drives Emulator - " RELEASE_VERSION "\n\x1Bq");
}

static void menu(void) {
  floppyFormatState = FLOPPY_FORMAT_SIZE_STATE;
  term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);

  showTitle();

  // Global options, read only
  vt52Cursor(2, 0);

  // Display network status
  term_printString("\x1BpNetwork: ");
  ip_addr_t currentIp = network_getCurrentIp();

  hasNetwork = currentIp.addr != 0;
  if (hasNetwork) {
    term_printString("CONNECTED");
    char ipStr[32];

    // Convert to dotted-decimal in a reentrant way
    ipaddr_ntoa_r(&currentIp, ipStr, sizeof(ipStr));

    term_printString(" | IP: ");
    term_printString(ipStr);
  } else {
    term_printString("NOT CONNECTED");
  }
  term_printString("\n\x1Bq");

  // Display USB Mass Storage status
  term_printString("\x1BpUSB Mass Storage: ");
  if (usbMassStorageReady) {
    term_printString("READY");
  } else {
    term_printString("NOT READY");
  }
  term_printString("\n\x1Bq");

  // Configurable options
  vt52Cursor(5, 0);
  // Display first GEMDRIVE options
  term_printString("[G]EMDRIVE Enabled? ");
  // Is it enabled?
  SettingsConfigEntry *gemDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_ENABLED);
  DPRINTF("GEMDRIVE: %s\n", gemDrive->value);
  if (isTrue(gemDrive->value)) {
    term_printString("Yes\n");
    // Display the GEMDRIVE folder
    SettingsConfigEntry *gemDriveFolder = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_FOLDER);
    DPRINTF("Folder: %s\n", gemDriveFolder->value);
    term_printString("  F[o]lder: ");
    term_printString(gemDriveFolder->value);

    // Display the GEMDRIVE drive
    SettingsConfigEntry *gemDriveDrive = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_DRIVE);
    DPRINTF("Drive: %s\n", gemDriveDrive->value);
    term_printString("\n  [D]rive: ");
    term_printString(gemDriveDrive->value);
    // Display the GEMDRIVE readonly
    SettingsConfigEntry *gemDriveReadonly = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_READONLY);
    DPRINTF("Readonly: %s\n", gemDriveReadonly->value);
    // term_printString("\n  Read O[n]ly? ");
    // term_printString(isTrue(gemDriveReadonly->value) ? "Yes" : "No");
    term_printString("\n\n");

    term_printString("\n\n");
  } else {
    term_printString("No\n\n\n\n\n");
  }

  // Display the Floppy options
  term_printString("[F]LOPPY Enabled? ");
  // Is it enabled?
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  DPRINTF("FLOPPY: %s\n", floppyDrive->value);
  if (isTrue(floppyDrive->value)) {
    term_printString("Yes\n");
    // Display the FLOPPY folder
    SettingsConfigEntry *floppyDriveFolder = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_FOLDER);
    DPRINTF("Folder: %s\n", floppyDriveFolder->value);
    term_printString("  Fo[l]der: ");
    term_printString(floppyDriveFolder->value);
    // Display the FLOPPY drive A
    SettingsConfigEntry *floppyDriveADrive = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A);
    char *driveAValue = right(floppyDriveADrive->value, 16);
    DPRINTF("Drive A: %s\n", driveAValue);
    term_printString("\n  [(SHFT+)A] Drive: ");
    term_printString(driveAValue);
    if (driveAValue != NULL) {
      free(driveAValue);  // Free the allocated memory for driveAValue
    }
    // Display the FLOPPY drive B
    SettingsConfigEntry *floppyDriveBDrive = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_B);
    char *driveBValue = right(floppyDriveBDrive->value, 16);
    DPRINTF("Drive B: %s\n", driveBValue);
    term_printString("\n  [(SHFT+)B] Drive: ");
    term_printString(driveBValue);
    if (driveBValue != NULL) {
      free(driveBValue);  // Free the allocated memory for driveBValue
    }
    // Display the FLOPPY boot
    SettingsConfigEntry *floppyBootEnabled = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_BOOT_ENABLED);
    DPRINTF("Boot: %s\n", floppyBootEnabled->value);
    term_printString("\n  Boo[t] enabled?");
    term_printString(isTrue(floppyBootEnabled->value) ? "Yes" : "No");
    // Display the FLOPPY xbios
    SettingsConfigEntry *floppyXbiosEnabled = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_XBIOS_ENABLED);
    DPRINTF("XBIOS: %s\n", floppyXbiosEnabled->value);
    term_printString("  XBIO[S] trap?");
    term_printString(isTrue(floppyXbiosEnabled->value) ? "Yes" : "No");
    // Format floppy
    term_printString("\n  Format [I]mage | [C]onvert MSA to ST\n\n");
  } else {
    term_printString("No\n\n\n\n\n\n");
  }

  // TODO: Merge code with the RTC options
  // Display the RTC options
  term_printString("[R]TC Enabled? ");
  // Is it enabled?
  SettingsConfigEntry *rtcEnabled = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_ENABLED);
  DPRINTF("RTC: %s\n", rtcEnabled->value);
  if (false && isTrue(rtcEnabled->value)) {
    term_printString("Yes\n");
    term_printString("  [H]ost NTP: ");
    // Print the NTP server host
    SettingsConfigEntry *ntpHost = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_NTP_HOST);
    if (ntpHost != NULL) {
      term_printString(ntpHost->value);
    } else {
      term_printString("Not set");
    }
    term_printString("\n  [P]ort NTP: ");
    // Print the NTP server port
    SettingsConfigEntry *ntpPort = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_NTP_PORT);
    if (ntpPort != NULL) {
      term_printString(ntpPort->value);
    } else {
      term_printString("Not set");
    }
    term_printString("\n  [U]TC Offset: ");
    // Print the UTC offset
    SettingsConfigEntry *utcOffset = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_UTC_OFFSET);
    if (utcOffset != NULL) {
      term_printString(utcOffset->value);
    } else {
      term_printString("Not set");
    }
    term_printString("  [Y]2K Patch?");
    // Print the Y2K patch
    SettingsConfigEntry *y2kPatch = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_Y2K_PATCH);
    if (y2kPatch != NULL) {
      term_printString(isTrue(y2kPatch->value) ? "Yes" : "No");
    } else {
      term_printString("Not set");
    }
  } else {
    term_printString("No\n");
  }
  vt52Cursor(TERM_SCREEN_SIZE_Y - 3, 0);
  term_printString("[E]xit desktop    [X] Return to Booster\n");

  term_printString("\nSelect an option: ");
}

static void showCounter(int cdown) {
  // Clear the bar
  char msg[64];
  if (cdown > 0) {
    sprintf(msg, "Boot will continue in %d seconds...", cdown);
  } else {
    showTitle();
    sprintf(msg, "Booting... Please wait...               ");
  }
  u8g2_SetDrawColor(display_getU8g2Ref(), 1);
  u8g2_DrawBox(display_getU8g2Ref(), 0,
               DISPLAY_HEIGHT - DISPLAY_TERM_CHAR_HEIGHT, DISPLAY_WIDTH,
               DISPLAY_TERM_CHAR_HEIGHT);
  u8g2_SetFont(display_getU8g2Ref(), u8g2_font_squeezed_b7_tr);
  u8g2_SetDrawColor(display_getU8g2Ref(), 0);
  u8g2_DrawStr(display_getU8g2Ref(), 0, DISPLAY_HEIGHT - 1, msg);
  u8g2_SetDrawColor(display_getU8g2Ref(), 1);
  u8g2_SetFont(display_getU8g2Ref(), u8g2_font_amstrad_cpc_extended_8f);
}

// Command handlers
void cmdMenu(const char *arg) {
  haltCountdown = true;
  menu();
}

void cmdHelp(const char *arg) {
  // term_printString("\x1B" "E" "Available commands:\n");
  term_printString("Available commands:\n");
  term_printString(" General:\n");
  term_printString("  clear   - Clear the terminal screen\n");
  term_printString("  exit    - Exit the terminal\n");
  term_printString("  help    - Show available commands\n");
  haltCountdown = true;
}

void cmdClear(const char *arg) {
  haltCountdown = true;
  term_clearScreen();
}

void cmdExit(const char *arg) {
  showTitle();
  term_printString("\n\n");
  term_printString("Exiting terminal...\n");
  // Send continue to desktop command
  haltCountdown = true;
  appStatus = APP_EMULATION_INIT;
}

void cmdBooster(const char *arg) {
  showTitle();
  term_printString("\n\n");
  term_printString("Launching Booster app...\n");
  term_printString("The computer will boot shortly...\n\n");
  term_printString("If it doesn't boot, power it on and off.\n");
  jumpBooster = true;
  keepActive = false;  // Exit the active loop
  haltCountdown = true;
}

void cmdHiddenSettings(const char *arg) {
  showTitle();
  term_printString(
      "\x1B"
      "E"
      "Available settings commands:\n");
  term_printString("  print   - Show settings\n");
  term_printString("  save    - Save settings\n");
  term_printString("  erase   - Erase settings\n");
  term_printString("  get     - Get setting (requires key)\n");
  term_printString("  put_int - Set integer (key and value)\n");
  term_printString("  put_bool- Set boolean (key and value)\n");
  term_printString("  put_str - Set string (key and value)\n");
  term_printString("\n");
  term_setCommandLevel(TERM_COMMAND_LEVEL_COMMAND_INPUT);
  haltCountdown = true;
  term_printString("Enter command. ESC to return > ");
}

//
// GEMDRIVE commands
//
void cmdGemdriveEnabled(const char *arg) {
  // Option to enable the GEMDRIVE
  SettingsConfigEntry *gemDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_ENABLED);
  settings_put_bool(aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_ENABLED,
                    !isTrue(gemDrive->value));
  settings_save(aconfig_getContext(), true);
  haltCountdown = true;
  menu();
  display_refresh();
}

static void drawPage(uint16_t top_offset) {
  uint16_t start = navState->page * NAV_LINES_PER_PAGE;
  uint16_t end = start + NAV_LINES_PER_PAGE;
  if (end > navState->count) {
    end = navState->count;
  }

  for (uint16_t i = start; i < end; i++) {
    uint8_t row = i - start;
    vt52Cursor(row + top_offset, 0);
    term_printString(i == navState->selected ? ">" : " ");
    char buffer[TERM_SCREEN_SIZE_X + 1];
    snprintf(buffer, sizeof(buffer), " %-*s", TERM_SCREEN_SIZE_X - 2,
             navState->entries[i]);
    term_printString(buffer);
  }

  // Draw info area
  vt52Cursor(TERM_SCREEN_SIZE_Y - 3,
             0);  // Bottom three lines of the screen for status
  char infoBuffer[128];
  int totalPages =
      (navState->count + NAV_LINES_PER_PAGE - 1) / NAV_LINES_PER_PAGE;
  sprintf(infoBuffer, "Page %d/%d\n", navState->page + 1, totalPages);
  term_printString(infoBuffer);
  term_printString("Use cursor keys and RETURN to navigate.\n");
  term_printString("SPACE to confirm selection. ESC to exit");
}

static enum navStatus navigate_directory(
    bool first_time, bool dirs_only, char key, EntryFilterFn filter_fn,
    char top_folder[MAX_FILENAME_LENGTH + 1]) {
  enum navStatus status = NAV_DIR_ERROR;
  if (first_time) {
    DPRINTF("First time loading directory.\n");
    navState->count = 0;
    navState->selected = 0;
    navState->page = 0;
    // Set the top folder
    if (top_folder != NULL) {
      strncpy(navState->topDir, top_folder, sizeof(navState->topDir));
      navState->topDir[sizeof(navState->topDir) - 1] = '\0';
    } else {
      strncpy(navState->topDir, "/", sizeof(navState->topDir));
      navState->topDir[sizeof(navState->topDir) - 1] = '\0';
    }
    memset(navState->entries, 0, sizeof(navState->entries));
    FRESULT result = sdcard_loadDirectory(
        (strlen(navState->folderPath) == 0) ? "/" : navState->folderPath,
        navState->entries, &navState->count, &navState->selected,
        &navState->page, dirs_only, filter_fn, navState->topDir);
    if (result != FR_OK) {
      term_printString("Error loading directory.\n");
    } else {
      status = NAV_DIR_FIRST_TIME_OK;
    }
  } else {
    DPRINTF("Next times loading directory.\n");
    status = NAV_DIR_NEXT_TIME_OK;
    int totalPages =
        (navState->count + NAV_LINES_PER_PAGE - 1) / NAV_LINES_PER_PAGE;
    switch (key) {
      case TERM_KEYBOARD_KEY_UP:
        if (navState->selected > 0) {
          navState->selected--;
        }
        break;
      case TERM_KEYBOARD_KEY_DOWN:
        if ((navState->selected < navState->count - 1) &&
            (navState->selected % NAV_LINES_PER_PAGE <
             NAV_LINES_PER_PAGE - 1)) {
          navState->selected++;
        }
        break;
      case TERM_KEYBOARD_KEY_LEFT:
        if (navState->page > 0) {
          navState->page--;
          navState->selected = navState->page * NAV_LINES_PER_PAGE;
        }
        break;
      case TERM_KEYBOARD_KEY_RIGHT:
        if (navState->page < (totalPages - 1)) {
          navState->page++;
          navState->selected = navState->page * NAV_LINES_PER_PAGE;
          DPRINTF("Page: %d\n", navState->page);
        }
        break;
      case '\r':
      case '\n': {
        // If the selected entry is a directory or two dots, navigate into it
        if ((navState->entries[navState->selected]
                              [strlen(navState->entries[navState->selected]) -
                               1] == '/') ||
            (strcmp(navState->entries[navState->selected], "..") == 0)) {
          // Select the entry
          navState->count = 0;
          navState->page = 0;
          char newFolderPath[MAX_FILENAME_LENGTH + 1];
          DPRINTF("Old folder path: %s\n", navState->folderPath);
          size_t len = strlen(navState->folderPath);
          if (len > 0 && navState->folderPath[len - 1] != '/') {
            snprintf(newFolderPath, sizeof(newFolderPath), "%s/%s",
                     navState->folderPath,
                     navState->entries[navState->selected]);
          } else {
            snprintf(newFolderPath, sizeof(newFolderPath), "%s%s",
                     navState->folderPath,
                     navState->entries[navState->selected]);
          }
          strncpy(navState->folderPath, newFolderPath, sizeof(newFolderPath));
          DPRINTF("Selected entry raw: %s\n", navState->folderPath);
          // Path up
          pathUp();
          DPRINTF("Selected entry with path up: %s\n", navState->folderPath);
          memset(navState->entries, 0, sizeof(navState->entries));
          navState->selected = 0;
          FRESULT result = sdcard_loadDirectory(
              navState->folderPath, navState->entries, &navState->count,
              &navState->selected, &navState->page, dirs_only, filter_fn,
              navState->topDir);
          if (result != FR_OK) {
            term_printString("Error loading directory.\n");
            status = NAV_DIR_ERROR;
          }
        }
        break;
      }
      case ' ': {
        // Confirm selection
        status = NAV_DIR_SELECTED;
        break;
      }
      default:
        // Return the ASCII uppercase value of the key
        // This is used to select the entry
        if (key >= 'a' && key <= 'z') {
          key = key - 'a' + 'A';
        }
        // Check if the key is a valid entry
        if (key >= 'A' && key <= 'Z') {
          status = key;
          DPRINTF("Navigation menu Key: %c\n", key);
        }
        break;
    }
  }
  return status;
}

void cmdGemdriveFolder(const char *arg) {
  // Check if the GEMDRIVE is enabled
  SettingsConfigEntry *gemDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_ENABLED);
  if (isTrue(gemDrive->value)) {
    haltCountdown = true;
    enum navStatus status = NAV_DIR_ERROR;
    // Check if this is the first time we enter the command
    switch (term_getCommandLevel()) {
      case TERM_COMMAND_LEVEL_SINGLE_KEY: {
        DPRINTF("Gemdrive folder will start listen for browse keys.\n");
        SettingsConfigEntry *gemDriveFolder = settings_find_entry(
            aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_FOLDER);
        strncpy(navState->folderPath, gemDriveFolder->value,
                sizeof(navState->folderPath));
        term_setCommandLevel(TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY);
        status = navigate_directory(true, true, '\0', floppiesFilter, NULL);
        break;
      }
      case TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY: {
        // If we are here is because we have already entered the command
        DPRINTF("GEMDRIVE key: %d\n", arg[0]);
        char key = arg[0];
        // Check if the key is a valid navigation key
        status = navigate_directory(false, true, key, floppiesFilter, NULL);
        break;
      }
      default:
        break;
    }
    switch (status) {
      case NAV_DIR_FIRST_TIME_OK:
      case NAV_DIR_NEXT_TIME_OK: {
        // Print the navState->selected entry
        DPRINTF("Entries: %d\n", navState->count);
        DPRINTF("Selected entry: %d = %s\n", navState->selected,
                navState->entries[navState->selected]);
        // Redraw the navState->page
        showTitle();
        DPRINTF("Folder: %s\n", navState->folderPath);
        term_printString("\nGEMDRIVE folder: ");
        term_printString(navState->folderPath);
        drawPage(NAV_LINES_PER_PAGE_OFFSET);
        break;
      }
      case NAV_DIR_SELECTED: {
        // Confirm selection
        // Save the navState->selected entry
        settings_put_string(aconfig_getContext(),
                            ACONFIG_PARAM_DRIVES_GEMDRIVE_FOLDER,
                            navState->folderPath);
        settings_save(aconfig_getContext(), true);
        DPRINTF("Folder: %s. SAVED!\n", navState->entries[navState->selected]);
        menu();
        term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
        break;
      }
    }
  }
}

void cmdReadOnly(const char *arg) {
  // Readonly option to avoid writing to the SD card
  // Check first if the GEMDRIVE is enabled
  SettingsConfigEntry *gemDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_ENABLED);
  if (isTrue(gemDrive->value)) {
    term_printString("GEMDRIVE is not enabled.\n");
    SettingsConfigEntry *readOnly = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_READONLY);
    settings_put_bool(aconfig_getContext(),
                      ACONFIG_PARAM_DRIVES_GEMDRIVE_READONLY,
                      !isTrue(readOnly->value));
    settings_save(aconfig_getContext(), true);
    haltCountdown = true;
    menu();
    display_refresh();
  }
}

void cmdGemdriveDrive(const char *arg) {
  // Check if the GEMDRIVE is enabled
  SettingsConfigEntry *gemDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_ENABLED);
  if (isTrue(gemDrive->value)) {
    if (term_getCommandLevel() == TERM_COMMAND_LEVEL_SINGLE_KEY) {
      showTitle();
      term_printString("\n\n");
      term_printString("Enter the GEMDRIVE drive (C to Z):\n");
      term_setCommandLevel(TERM_COMMAND_LEVEL_DATA_INPUT);
      haltCountdown = true;
    } else {
      DPRINTF("Gemdrive drive in single key mode.\n");
      term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
      // Check if the input buffer is empty
      if (strlen(term_getInputBuffer()) == 0) {
        term_printString("Invalid drive.\n");
        term_printString("Press SPACE to continue...\n");
      }
      // Check if the input buffer is a valid domain name
      else if (!isValidDrive(term_getInputBuffer())) {
        term_printString("Invalid drive.\n");
        term_printString("Press SPACE to continue...\n");
      }
      // Store the GEMDRIVE drive
      else {
        char driveBuffer[64];
        const char *input = term_getInputBuffer();
        if (!input) {
          driveBuffer[0] = '\0';
        } else {
          // Only scan up to buffer‑1, and shorten to actual input length
          size_t max = sizeof(driveBuffer) - 1;
          size_t len = strnlen(input, max);
          for (size_t i = 0; i < len; ++i) {
            driveBuffer[i] = (char)toupper((unsigned char)input[i]);
          }
          driveBuffer[len] = '\0';
        }
        settings_put_string(aconfig_getContext(),
                            ACONFIG_PARAM_DRIVES_GEMDRIVE_DRIVE, driveBuffer);
        settings_save(aconfig_getContext(), true);
        menu();
      }
    }
  }
}

//
// FLOPPY commands
//
void cmdFloppyEnabled(const char *arg) {
  // Option to enable the FLOPPY
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  settings_put_bool(aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED,
                    !isTrue(floppyDrive->value));
  settings_save(aconfig_getContext(), true);
  haltCountdown = true;
  menu();
  display_refresh();
}

void cmdFloppiesFolder(const char *arg) {
  // Check if the Floppy is enabled
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (isTrue(floppyDrive->value)) {
    haltCountdown = true;
    enum navStatus status = NAV_DIR_ERROR;
    // Check if this is the first time we enter the command
    switch (term_getCommandLevel()) {
      case TERM_COMMAND_LEVEL_SINGLE_KEY: {
        DPRINTF("Floppy folder will start listen for browse keys.\n");
        SettingsConfigEntry *floppyDriveFolder = settings_find_entry(
            aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_FOLDER);
        strncpy(navState->folderPath, floppyDriveFolder->value,
                sizeof(navState->folderPath));
        term_setCommandLevel(TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY);
        status = navigate_directory(true, true, '\0', floppiesFilter, NULL);
        break;
      }
      case TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY: {
        // If we are here is because we have already entered the command
        DPRINTF("Floppy folder key: %d\n", arg[0]);
        char key = arg[0];
        // Check if the key is a valid navigation key
        status = navigate_directory(false, true, key, floppiesFilter, NULL);
        break;
      }
      default:
        break;
    }
    switch (status) {
      case NAV_DIR_FIRST_TIME_OK:
      case NAV_DIR_NEXT_TIME_OK: {
        // Print the navState->selected entry
        DPRINTF("Entries: %d\n", navState->count);
        DPRINTF("Selected entry: %d = %s\n", navState->selected,
                navState->entries[navState->selected]);
        // Redraw the navState->page
        showTitle();
        DPRINTF("Folder: %s\n", navState->folderPath);
        term_printString("\nFloppies folder: ");
        term_printString(navState->folderPath);
        drawPage(NAV_LINES_PER_PAGE_OFFSET);
        break;
      }
      case NAV_DIR_SELECTED: {
        // Confirm selection
        // Save the navState->selected entry
        settings_put_string(aconfig_getContext(),
                            ACONFIG_PARAM_DRIVES_FLOPPY_FOLDER,
                            navState->folderPath);
        settings_save(aconfig_getContext(), true);
        DPRINTF("Folder: %s. SAVED!\n", navState->entries[navState->selected]);
        menu();
        term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
        break;
      }
    }
  }
}

static void selectFloppyDrive(const char *arg, bool driveA) {
  // Check if the Floppy is enabled
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (isTrue(floppyDrive->value)) {
    haltCountdown = true;
    enum navStatus status = NAV_DIR_ERROR;
    // Check if this is the first time we enter the command
    switch (term_getCommandLevel()) {
      case TERM_COMMAND_LEVEL_SINGLE_KEY: {
        DPRINTF("Floppy Drive %c will start listen for browse keys.\n",
                driveA ? 'A' : 'B');
        SettingsConfigEntry *floppyDriveFolder = settings_find_entry(
            aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_FOLDER);
        strncpy(navState->folderPath, floppyDriveFolder->value,
                sizeof(navState->folderPath));
        term_setCommandLevel(TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY);
        status = navigate_directory(true, false, '\0', floppiesFilter,
                                    navState->folderPath);
        break;
      }
      case TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY: {
        // If we are here is because we have already entered the command
        DPRINTF("Floppy Drive %c key: %d\n", arg[0], driveA ? 'A' : 'B');
        char key = arg[0];
        // Check if the key is a valid navigation key
        status = navigate_directory(false, false, key, floppiesFilter,
                                    navState->folderPath);
        break;
      }
      default:
        break;
    }
    switch (status) {
      case NAV_DIR_FIRST_TIME_OK:
      case NAV_DIR_NEXT_TIME_OK: {
        // Print the navState->selected entry
        DPRINTF("Entries: %d\n", navState->count);
        DPRINTF("Selected entry: %d = %s\n", navState->selected,
                navState->entries[navState->selected]);
        // Redraw the navState->page
        showTitle();
        DPRINTF("Folder: %s\n", navState->folderPath);
        term_printString("\nDrive ");
        term_printString(driveA ? "A" : "B");
        term_printString(" file: ");
        term_printString(navState->folderPath);
        drawPage(NAV_LINES_PER_PAGE_OFFSET);
        break;
      }
      case NAV_DIR_SELECTED: {
        // Confirm selection
        // Save the navState->selected entry
        char bufTmp[MAX_FILENAME_LENGTH + 1];
        snprintf(bufTmp, sizeof(bufTmp), "%s/%s", navState->folderPath,
                 navState->entries[navState->selected]);
        settings_put_string(aconfig_getContext(),
                            driveA ? ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A
                                   : ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_B,
                            bufTmp);
        settings_save(aconfig_getContext(), true);
        DPRINTF("Drive %c file: %s. SAVED!\n", driveA ? 'A' : 'B', bufTmp);
        menu();
        term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
        break;
      }
      default:
        break;
    }
  }
}

void cmdFloppyDriveA(const char *arg) {
  // Mount the floppy drive A
  selectFloppyDrive(arg, true);
}

void cmdFloppyDriveB(const char *arg) {
  // Mount the floppy drive B
  selectFloppyDrive(arg, false);
}

static void ejectFloppyDrive(bool driveA) {
  // Eject the floppy drive
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (isTrue(floppyDrive->value)) {
    SettingsConfigEntry *floppyDriveEject = settings_find_entry(
        aconfig_getContext(), driveA ? ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_A
                                     : ACONFIG_PARAM_DRIVES_FLOPPY_DRIVE_B);
    settings_put_string(aconfig_getContext(), floppyDriveEject->key, "");
    settings_save(aconfig_getContext(), true);
    haltCountdown = true;
    menu();
    display_refresh();
  }
}

void cmdFloppyDriveAEject(const char *arg) {
  // Eject the floppy drive A
  ejectFloppyDrive(true);
}
void cmdFloppyDriveBEject(const char *arg) {
  // Eject the floppy drive B
  ejectFloppyDrive(false);
}

void cmdFormatFloppy(const char *arg) {
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (isTrue(floppyDrive->value)) {
    DPRINTF("Floppy format state: %d\n", floppyFormatState);
    switch (floppyFormatState) {
      case FLOPPY_FORMAT_SIZE_STATE:
        // Set the floppy format size
        {
          showTitle();
          term_printString("\n\n");
          term_printString("Format a new floppy image file\n\n");
          term_printString("  [1] 360K (DS)\n");
          term_printString("  [2] 720K (DD)\n");
          term_printString("  [3] 1.44M (HD)\n");
          term_printString("  [4] 2.88M (ED)\n");
          term_printString("Enter the disk image size: ");
          term_setCommandLevel(TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY);
          haltCountdown = true;
          floppyFormatState = FLOPPY_FORMAT_NAME_STATE;
          break;
        }
      case FLOPPY_FORMAT_NAME_STATE:
        // Set the floppy format name
        {
          switch (arg[0]) {
            case '1':
              term_printString("360K (DS)\n");
              floppyImageHeader.num_tracks = 80;
              floppyImageHeader.num_sides = 1;
              floppyImageHeader.num_sectors = 9;
              break;
            case '2':
              term_printString("720K (DD)\n");
              floppyImageHeader.num_tracks = 80;
              floppyImageHeader.num_sides = 2;
              floppyImageHeader.num_sectors = 9;
              break;
            case '3':
              term_printString("1.44M (HD)\n");
              floppyImageHeader.num_tracks = 80;
              floppyImageHeader.num_sides = 2;
              floppyImageHeader.num_sectors = 18;
              break;
            case '4':
              term_printString("2.88M (ED)\n");
              floppyImageHeader.num_tracks = 80;
              floppyImageHeader.num_sides = 2;
              floppyImageHeader.num_sectors = 36;
              break;
            default:
              return;
          }
          term_printString("\nEnter the name of the file:\n");
          term_setCommandLevel(TERM_COMMAND_LEVEL_DATA_INPUT);
          floppyFormatState = FLOPPY_FORMAT_LABEL_STATE;
          break;
        }
      case FLOPPY_FORMAT_LABEL_STATE:
        // Set the floppy format label
        {
          // Create a file name concatenating:
          // 1. The name of the file
          // 2. The extension .st.rw
          char fileName[MAX_FILENAME_LENGTH + 1];
          snprintf(fileName, sizeof(fileName), "%s.st.rw",
                   term_getInputBuffer());
          // Check if the file name is valid
          // if (!isValidFileName(fileName)) {
          //   term_printString("Invalid file name.\n");
          //   term_printString("Press SPACE to continue...\n");
          //   return;
          // }
          // Copy the file name to the floppy image header
          strncpy(floppyImageHeader.floppy_name, fileName,
                  sizeof(floppyImageHeader.floppy_name));
          floppyImageHeader
              .floppy_name[sizeof(floppyImageHeader.floppy_name) - 1] = '\0';
          term_clearInputBuffer();
          term_printString("\n");
          term_printString("Enter the label of the file:\n");
          term_setCommandLevel(TERM_COMMAND_LEVEL_DATA_INPUT);
          floppyFormatState = FLOPPY_FORMAT_CONFIRM_STATE;
          break;
        }
      case FLOPPY_FORMAT_CONFIRM_STATE:
        // Confirm the floppy format
        {
          // Check if the label is valid
          // if (!isValidLabel(term_getInputBuffer())) {
          //   term_printString("Invalid label.\n");
          //   term_printString("Press SPACE to continue...\n");
          //   return;
          // }
          // Copy the label to the floppy image header
          strncpy(floppyImageHeader.volume_name, term_getInputBuffer(),
                  sizeof(floppyImageHeader.volume_name));
          floppyImageHeader
              .volume_name[sizeof(floppyImageHeader.volume_name) - 1] = '\0';
          term_clearInputBuffer();
          term_printString("\n");
          term_printString("Floppy image:\n");
          term_printString("  File name: ");
          term_printString(floppyImageHeader.floppy_name);
          term_printString("\n  Label: ");
          term_printString(floppyImageHeader.volume_name);
          term_printString("\n  tracks: ");
          term_printString((floppyImageHeader.num_tracks == 80) ? "80" : "40");
          term_printString("  sectors: ");
          // Convert to string
          char numSectors[4];
          snprintf(numSectors, sizeof(numSectors), "%d",
                   floppyImageHeader.num_sectors);
          term_printString(numSectors);
          term_printString("  sides: ");
          term_printString((floppyImageHeader.num_sides == 2) ? "2" : "1");
          term_printString("\n\n");
          term_printString("Start formatting? (Y/N)");
          term_setCommandLevel(TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY);
          floppyFormatState = FLOPPY_FORMAT_FORMATTING_STATE;
          break;
        }
      case FLOPPY_FORMAT_FORMATTING_STATE: {
        if (arg[0] != 'Y' && arg[0] != 'y') {
          term_printString("Format cancelled.\n");
          term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
          menu();
          return;
        }
        term_printString("\n\nFormatting floppy disk... Please wait\n");
        // Perform the actual formatting here
        floppyFormatState = FLOPPY_FORMAT_DONE_STATE;
        term_setCommandLevel(TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY);
        term_setLastSingleKeyCommand('i');
        term_forceInputChar('i', false);  // Force the input to 'i' to continue
        break;
      }
      case FLOPPY_FORMAT_DONE_STATE: {
        // Check if the key is a valid navigation key
        SettingsConfigEntry *floppyDriveFolder = settings_find_entry(
            aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_FOLDER);

        FRESULT err = floppy_createSTImage(
            floppyDriveFolder->value, floppyImageHeader.floppy_name,
            floppyImageHeader.num_tracks, floppyImageHeader.num_sectors,

            floppyImageHeader.num_sides, floppyImageHeader.volume_name, false);
        term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
        menu();
        break;
      }
    }
  }
}

static void selectMSA(const char *arg) {
  // Check if the Floppy is enabled
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (isTrue(floppyDrive->value)) {
    haltCountdown = true;
    enum navStatus status = NAV_DIR_ERROR;
    // Check if this is the first time we enter the command
    switch (term_getCommandLevel()) {
      case TERM_COMMAND_LEVEL_SINGLE_KEY: {
        SettingsConfigEntry *floppyDriveFolder = settings_find_entry(
            aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_FOLDER);
        strncpy(navState->folderPath, floppyDriveFolder->value,
                sizeof(navState->folderPath));
        term_setCommandLevel(TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY);
        status = navigate_directory(true, false, '\0', floppiesMSAFilter,
                                    navState->folderPath);
        break;
      }
      case TERM_COMMAND_LEVEL_COMMAND_SINGLE_KEY_REENTRY: {
        // If we are here is because we have already entered the command
        char key = arg[0];
        // Check if the key is a valid navigation key
        status = navigate_directory(false, false, key, floppiesMSAFilter,
                                    navState->folderPath);
        break;
      }
      default:
        break;
    }
    switch (status) {
      case NAV_DIR_FIRST_TIME_OK:
      case NAV_DIR_NEXT_TIME_OK: {
        // Print the navState->selected entry
        DPRINTF("Entries: %d\n", navState->count);
        DPRINTF("Selected entry: %d = %s\n", navState->selected,
                navState->entries[navState->selected]);
        // Redraw the navState->page
        showTitle();
        DPRINTF("Folder: %s\n", navState->folderPath);
        term_printString(" file: ");
        term_printString(navState->folderPath);
        drawPage(NAV_LINES_PER_PAGE_OFFSET);
        break;
      }
      case NAV_DIR_SELECTED: {
        // Confirm selection
        // Save the navState->selected entry
        // Source file
        char bufSrcTmp[MAX_FILENAME_LENGTH + 1];
        snprintf(bufSrcTmp, sizeof(bufSrcTmp), "%s",
                 navState->entries[navState->selected]);
        DPRINTF("File to convert to ST: %s\n", bufSrcTmp);

        // Already processing, do nothing
        char bufTmp[MAX_FILENAME_LENGTH + 1];
        snprintf(bufTmp, sizeof(bufTmp), "%s",
                 navState->entries[navState->selected]);
        // Remove the .msa or .MSA extension
        floppy_removeMSAExtension(bufTmp);
        // And add a .st.rw extension
        char bufTmp2[MAX_FILENAME_LENGTH + 1];
        snprintf(bufTmp2, sizeof(bufTmp2), "%s.st.rw", bufTmp);
        FRESULT ferr =
            floppy_MSA2ST(navState->folderPath, bufSrcTmp, bufTmp2, false);
        if (ferr != FR_OK) {
          DPRINTF("Error converting MSA to ST: %d\n", ferr);
          term_printString("Error converting MSA to ST.\n");
        } else {
          DPRINTF("MSA converted to ST: %s\n", bufTmp2);
          term_printString("MSA converted to ST.\n");
        }
        menu();
        term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
      }
      default:
        break;
    }
  }
}

void cmdMSA2ST(const char *arg) {
  // Convert MSA to ST
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (isTrue(floppyDrive->value)) {
    selectMSA(arg);
  }
}

void cmdBootEnabled(const char *arg) {
  // Boot option to booting from the floppy
  // Check first if the FLOPPY is enabled
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (isTrue(floppyDrive->value)) {
    SettingsConfigEntry *bootEnabled = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_BOOT_ENABLED);
    settings_put_bool(aconfig_getContext(),
                      ACONFIG_PARAM_DRIVES_FLOPPY_BOOT_ENABLED,
                      !isTrue(bootEnabled->value));
    settings_save(aconfig_getContext(), true);
    haltCountdown = true;
    menu();
    display_refresh();
  }
}

void cmdXbiosEnabled(const char *arg) {
  // Option to enable XBIOS traps for the floppy drive
  // Check first if the FLOPPY is enabled
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (isTrue(floppyDrive->value)) {
    SettingsConfigEntry *xbiosEnabled = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_XBIOS_ENABLED);
    settings_put_bool(aconfig_getContext(),
                      ACONFIG_PARAM_DRIVES_FLOPPY_XBIOS_ENABLED,
                      !isTrue(xbiosEnabled->value));
    settings_save(aconfig_getContext(), true);
    haltCountdown = true;
    menu();
    display_refresh();
  }
}

//
// RTC commands
//
void cmdRTCEnabled(const char *arg) {
  // Option to enable the RTC
  SettingsConfigEntry *rtcEnabled = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_ENABLED);
  settings_put_bool(aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_ENABLED,
                    !isTrue(rtcEnabled->value));
  settings_save(aconfig_getContext(), true);
  haltCountdown = true;
  menu();
  display_refresh();
}

void cmdY2KPatch(const char *arg) {
  SettingsConfigEntry *rtc = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_ENABLED);
  if (isTrue(rtc->value)) {
    // Y2K patch command
    SettingsConfigEntry *y2kPatch = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_Y2K_PATCH);
    settings_put_bool(aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_Y2K_PATCH,
                      !isTrue(y2kPatch->value));
    settings_save(aconfig_getContext(), true);
    haltCountdown = true;
    menu();
    display_refresh();
  }
}

void cmdUTCOffset(const char *arg) {
  SettingsConfigEntry *rtc = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_ENABLED);
  if (isTrue(rtc->value)) {
    if (term_getCommandLevel() == TERM_COMMAND_LEVEL_SINGLE_KEY) {
      showTitle();
      term_printString("\n\n");
      term_printString("Enter the UTC offset:\n");
      term_setCommandLevel(TERM_COMMAND_LEVEL_DATA_INPUT);
      haltCountdown = true;
    } else {
      DPRINTF("UTC Offset command not in single key mode.\n");
      term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
      // Verify that the UTC offset in the input buffer is valid
      // Check if the input buffer is empty
      if (strlen(term_getInputBuffer()) == 0) {
        term_printString("Invalid UTC offset.\n");
        term_printString("Press SPACE to continue...\n");
      }
      // Convert the input buffer to an integer
      const char *input = term_getInputBuffer();
      char *endptr;
      long utcOffset = strtol(input, &endptr, 10);

      // Check if the conversion was successful and within valid range
      if (input == endptr || *endptr != '\0' || utcOffset < -12 ||
          utcOffset > 14) {
        term_printString("Invalid UTC offset.\n");
        term_printString("Press SPACE to continue...\n");
      }
      // Store the NTP server host
      else {
        settings_put_string(aconfig_getContext(),
                            ACONFIG_PARAM_DRIVES_RTC_UTC_OFFSET,
                            term_getInputBuffer());
        settings_save(aconfig_getContext(), true);
        menu();
      }
    }
  }
}

void cmdHost(const char *arg) {
  SettingsConfigEntry *rtc = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_ENABLED);
  if (isTrue(rtc->value)) {
    if (term_getCommandLevel() == TERM_COMMAND_LEVEL_SINGLE_KEY) {
      showTitle();
      term_printString("\n\n");
      term_printString("Enter the NTP server host:\n");
      term_setCommandLevel(TERM_COMMAND_LEVEL_DATA_INPUT);
      haltCountdown = true;
    } else {
      DPRINTF("Host command not in single key mode.\n");
      term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
      // Verify that the NTP server in the input buffer is valid
      // Check if the input buffer is empty
      if (strlen(term_getInputBuffer()) == 0) {
        term_printString("Invalid NTP server host.\n");
        term_printString("Press SPACE to continue...\n");
      }
      // Check if the input buffer is a valid domain name
      else if (!isValidDomain(term_getInputBuffer())) {
        term_printString("Invalid NTP server host.\n");
        term_printString("Press SPACE to continue...\n");
      }
      // Store the NTP server host
      else {
        settings_put_string(aconfig_getContext(),
                            ACONFIG_PARAM_DRIVES_RTC_NTP_HOST,
                            term_getInputBuffer());
        settings_save(aconfig_getContext(), true);
        menu();
      }
    }
  }
}

void cmdPort(const char *arg) {
  SettingsConfigEntry *rtc = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_ENABLED);
  if (isTrue(rtc->value)) {
    if (term_getCommandLevel() == TERM_COMMAND_LEVEL_SINGLE_KEY) {
      showTitle();
      term_printString("\n\n");
      term_printString("Enter the NTP server port:\n");
      term_setCommandLevel(TERM_COMMAND_LEVEL_DATA_INPUT);
      haltCountdown = true;
    } else {
      DPRINTF("Port command not in single key mode.\n");
      term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
      // Verify that the NTP server in the input buffer is valid
      // Check if the input buffer is empty
      if (strlen(term_getInputBuffer()) == 0) {
        term_printString("Invalid NTP server port.\n");
        term_printString("Press SPACE to continue...\n");
      }
      // Convert the input buffer to an integer
      const char *input = term_getInputBuffer();
      char *endptr;
      long port = strtol(input, &endptr, 10);

      // Check if the conversion was successful and within valid port range
      if (input == endptr || *endptr != '\0' || port < 1 || port > 65535) {
        term_printString("Invalid NTP server port.\n");
        term_printString("Press SPACE to continue...\n");
      }
      // Store the NTP server host
      else {
        settings_put_string(aconfig_getContext(),
                            ACONFIG_PARAM_DRIVES_RTC_NTP_PORT,
                            term_getInputBuffer());
        settings_save(aconfig_getContext(), true);
        menu();
      }
    }
  }
}

// This section contains the functions that are called from the main loop

static bool getKeepActive() { return keepActive; }

static bool getJumpBooster() { return jumpBooster; }

static void preinit() {
  // Initialize the terminal
  term_init();

  // Clear the screen
  term_clearScreen();

  // Show the title
  showTitle();
  term_printString("\n\n");
  term_printString("Configuring network... please wait...\n");
  term_printString("or press SHIFT to boot to desktop.\n");

  display_refresh();

  // Allocate memory for navState
  navState = (DirNavigation *)malloc(sizeof(DirNavigation));
  if (navState == NULL) {
    term_printString("Error allocating memory for navState.\n");
  }
  // Optional: zero out memory
  memset(navState, 0, sizeof(DirNavigation));
}

static void deinit() {
  // Free the memory allocated for navState
  if (navState != NULL) {
    free(navState);
    navState = NULL;
  }
}

void failure(const char *message) {
  // Initialize the terminal
  term_init();

  // Clear the screen
  term_clearScreen();

  // Show the title
  showTitle();
  term_printString("\n\n");
  term_printString(message);

  display_refresh();
}

static void init(const char *path) {
  // Set the command table
  term_setCommands(commands, numCommands);

  // Clear the screen
  term_clearScreen();

  // Init contdown
  countdown = 20;

  // Set command level
  term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);  // Single key command

  // Display the menu
  menu();

  // Example 1: Move the cursor up one line.
  // VT52 sequence: ESC A (moves cursor up)
  // The escape sequence "\x1BA" will move the cursor up one line.
  // term_printString("\x1B" "A");
  // After moving up, print text that overwrites part of the previous line.
  // term_printString("Line 2 (modified by ESC A)\n");

  // Example 2: Move the cursor right one character.
  // VT52 sequence: ESC C (moves cursor right)
  // term_printString("\x1B" "C");
  // term_printString(" <-- Moved right with ESC C\n");

  // Example 3: Direct cursor addressing.
  // VT52 direct addressing uses ESC Y <row> <col>, where:
  //   row_char = row + 0x20, col_char = col + 0x20.
  // For instance, to move the cursor to row 0, column 10:
  //   row: 0 -> 0x20 (' ')
  //   col: 10 -> 0x20 + 10 = 0x2A ('*')
  // term_printString("\x1B" "Y" "\x20" "\x2A");
  // term_printString("Text at row 0, column 10 via ESC Y\n");

  // term_printString("\x1B" "Y" "\x2A" "\x20");

  display_refresh();
}

void __not_in_flash_func(emul_start)() {
  // The anatomy of an app or microfirmware is as follows:
  // - The driver code running in the remote device (the computer)
  // - the driver code running in the host device (the rp2040/rp2350)
  //
  // The driver code running in the remote device is responsible for:
  // 1. Perform the emulation of the device (ex: a ROM cartridge)
  // 2. Handle the communication with the host device
  // 3. Handle the configuration of the driver (ex: the ROM file to load)
  // 4. Handle the communication with the user (ex: the terminal)
  //
  // The driver code running in the host device is responsible for:
  // 1. Handle the communication with the remote device
  // 2. Handle the configuration of the driver (ex: the ROM file to load)
  // 3. Handle the communication with the user (ex: the terminal)
  //
  // Hence, we effectively have two drivers running in two different devices
  // with different architectures and capabilities.
  //
  // Please read the documentation to learn to use the communication
  // protocol between the two devices in the tprotocol.h file.
  //

  // 1. Check if the host device must be initialized to perform the
  // emulation
  //    of the device, or start in setup/configuration mode
  SettingsConfigEntry *appMode =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_MODE);
  int appModeValue = APP_MODE_SETUP;  // Setup menu
  if (appMode == NULL) {
    DPRINTF(
        "APP_MODE_SETUP not found in the configuration. Using default "
        "value\n");
  } else {
    appModeValue = atoi(appMode->value);
    DPRINTF("Start emulation in mode: %i\n", appModeValue);
  }

  // 2. Initialiaze the normal operation of the app, unless the
  // configuration option says to start the config app Or a SELECT button is
  // (or was) pressed to start the configuration section of the app

  // In this example, the flow will always start the configuration app first
  // The ROM Emulator app for example will check here if the start directly
  // in emulation mode is needed or not

  // 3. If we are here, it means the app is not in emulation mode, but in
  // setup/configuration mode

  // As a rule of thumb, the remote device (the computer) driver code must
  // be copied to the RAM of the host device where the emulation will take
  // place.
  // The code is stored as an array in the target_firmware.h file
  //
  // Copy the terminal firmware to RAM
  COPY_FIRMWARE_TO_RAM((uint16_t *)target_firmware, target_firmware_length);

  // Initialize the terminal emulator PIO programs
  // The communication between the remote (target) computer and the RP2040
  // is done using a command protocol over the cartridge bus
  // term_dma_irq_handler_lookup is the implementation of the terminal
  // emulator using the command protocol. Hence, if you want to implement
  // your own app or microfirmware, you should implement your own command
  // handler using this protocol.
  init_romemul(NULL, term_dma_irq_handler_lookup, false);

  // After this point, the remote computer can execute the code

  // 4. During the setup/configuration mode, the driver code must interact
  // with the user to configure the device. To simplify the process, the
  // terminal emulator is used to interact with the user.
  // The terminal emulator is a simple text-based interface that allows the
  // user to configure the device using text commands.
  // If you want to use a custom app in the remote computer, you can do it.
  // But it's easier to debug and code in the rp2040

  // Initialize the display
  display_setupU8g2();

  // 5. Configure the SELECT button
  // Short press: reset the device and restart the app
  // Long press: reset the device and erase the flash.
  select_configure();
  select_coreWaitPush(reset_device,
                      reset_deviceAndEraseFlash);  // Wait for the SELECT
                                                   // button to be pushed

  // 6. Init the sd card
  // Most of the apps or microfirmwares will need to read and write files
  // to the SD card. The SD card is used to store the ROM, floppies, even
  // full hard disk files, configuration files, and other data.
  // The SD card is initialized here. If the SD card is not present, the
  // app will show an error message and wait for the user to insert the SD
  // card. The app will not start until the SD card is inserted correctly.
  // Each app or microfirmware must have a folder in the SD card where the
  // files are stored. The folder name is defined in the configuration.
  // If there is no folder in the micro SD card, the app will create it.

  // Let's start with the GEMDRIVE folder
  // First, check if the GEMDRIVE is enabled
  SettingsConfigEntry *gemDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_ENABLED);
  if (gemDrive == NULL) {
    DPRINTF("GEMDRIVE not found in the configuration.\n");
    DPRINTF("Error initializing the SD card: GEMDRIVE params not found\n");
    failure(
        "SD card error.\nCheck the card is inserted correctly.\nInsert "
        "card "
        "and restart the computer.");
    while (1) {
      // Wait forever
      term_loop();
#ifdef BLINK_H
      blink_toogle();
#endif
    }
  } else {
    DPRINTF("GEMDRIVE: %s\n", gemDrive->value);
    if (isTrue(gemDrive->value)) {
      SettingsConfigEntry *folder = settings_find_entry(
          aconfig_getContext(), ACONFIG_PARAM_DRIVES_GEMDRIVE_FOLDER);
      char *folderName = "/hd";
      if (folder == NULL) {
        DPRINTF("FOLDER not found in the configuration. Using default value\n");
      } else {
        DPRINTF("FOLDER: %s\n", folder->value);
        folderName = folder->value;
      }
      int sdcardErr = sdcard_initFilesystem(&fsys, folderName);
      if (sdcardErr != SDCARD_INIT_OK) {
        DPRINTF("Error initializing the SD card: %i\n", sdcardErr);
        failure(
            "SD card error.\nCheck the card is inserted correctly.\nInsert "
            "card "
            "and restart the computer.");
        while (1) {
          // Wait forever
          term_loop();
#ifdef BLINK_H
          blink_toogle();
#endif
        }
      } else {
        DPRINTF("SD card found & initialized for GEMDRIVE\n");
      }
    }
  }

  // Now let's check the FLOPPY folder
  // First, check if the FLOPPY is enabled
  SettingsConfigEntry *floppyDrive = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_ENABLED);
  if (floppyDrive == NULL) {
    DPRINTF("FLOPPY not found in the configuration.\n");
    DPRINTF("Error initializing the SD card: FLOPPY params not found\n");
    failure(
        "SD card error.\nCheck the card is inserted correctly.\nInsert "
        "card "
        "and restart the computer.");
    while (1) {
      // Wait forever
      term_loop();
#ifdef BLINK_H
      blink_toogle();
#endif
    }
  } else
    DPRINTF("FLOPPY: %s\n", floppyDrive->value);
  if (isTrue(floppyDrive->value)) {
    SettingsConfigEntry *folder = settings_find_entry(
        aconfig_getContext(), ACONFIG_PARAM_DRIVES_FLOPPY_FOLDER);
    char *folderName = "/floppies";
    if (folder == NULL) {
      DPRINTF("FOLDER not found in the configuration. Using default value\n");
    } else {
      DPRINTF("FOLDER: %s\n", folder->value);
      folderName = folder->value;
    }
    int sdcardErr = sdcard_initFilesystem(&fsys, folderName);
    if (sdcardErr != SDCARD_INIT_OK) {
      DPRINTF("Error initializing the SD card: %i\n", sdcardErr);
      failure(
          "SD card error.\nCheck the card is inserted correctly.\nInsert "
          "card "
          "and restart the computer.");
      while (1) {
        // Wait forever
        term_loop();
#ifdef BLINK_H
        blink_toogle();
#endif
      }
    } else {
      DPRINTF("SD card found & initialized for GEMDRIVE\n");
    }
  }

  // Initialize the display again (in case the terminal emulator changed it)
  display_setupU8g2();

  // Pre-init the stuff
  // In this example it only prints the please wait message, but can be used
  // as a place to put other code that needs to be run before the network is
  // initialized
  preinit();

  // 7 Init the network, if needed
  // It's always a good idea to wait for the network to be ready
  // Get the WiFi mode from the settings
  // If you are developing code that does not use the network, you can
  // comment this section
  // It's important to note that the network parameters are taken from the
  // global configuration of the Booster app. The network parameters are
  // ready only for the microfirmware apps.
  SettingsConfigEntry *wifiMode =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_MODE);
  wifi_mode_t wifiModeValue = WIFI_MODE_STA;
  if (wifiMode == NULL) {
    DPRINTF("No WiFi mode found in the settings. No initializing.\n");
  } else {
    wifiModeValue = (wifi_mode_t)atoi(wifiMode->value);
    if (wifiModeValue != WIFI_MODE_AP) {
      DPRINTF("WiFi mode is STA\n");
      wifiModeValue = WIFI_MODE_STA;
      int err = network_wifiInit(wifiModeValue);
      if (err != 0) {
        DPRINTF("Error initializing the network: %i. No initializing.\n", err);
      } else {
        // Set the term_loop as a callback during the polling period
        network_setPollingCallback(term_loop);
        // Connect to the WiFi network
        int maxAttempts = 3;  // or any other number defined elsewhere
        int attempt = 0;
        err = NETWORK_WIFI_STA_CONN_ERR_TIMEOUT;

        while ((attempt < maxAttempts) &&
               (err == NETWORK_WIFI_STA_CONN_ERR_TIMEOUT)) {
          err = network_wifiStaConnect();
          attempt++;

          if ((err > 0) && (err < NETWORK_WIFI_STA_CONN_ERR_TIMEOUT)) {
            DPRINTF("Error connecting to the WiFi network: %i\n", err);
          }
        }

        if (err == NETWORK_WIFI_STA_CONN_ERR_TIMEOUT) {
          DPRINTF("Timeout connecting to the WiFi network after %d attempts\n",
                  maxAttempts);
          // Optionally, return an error code here.
        }
        network_setPollingCallback(NULL);
      }
    } else {
      DPRINTF("WiFi mode is AP. No initializing.\n");
    }
  }

  // 8. Now complete the terminal emulator initialization
  // The terminal emulator is used to interact with the user to configure
  // the device.
  init(NULL);

  // Init the usb device
  usb_mass_init();

// Blink on
#ifdef BLINK_H
  blink_on();
#endif

  // 9. Start the main loop
  // The main loop is the core of the app. It is responsible for running the
  // app, handling the user input, and performing the tasks of the app.
  // The main loop runs until the user decides to exit.
  // For testing purposes, this app only shows commands to manage the
  // settings
  DPRINTF("Start the app loop here\n");
  absolute_time_t wifiScanTime = make_timeout_time_ms(
      WIFI_SCAN_TIME_MS);  // 3 seconds minimum for network scanning

  // Initialize the timer for decrementing the countdown
  absolute_time_t lastDecrement = get_absolute_time();

  while (getKeepActive()) {
    // #if PICO_CYW43_ARCH_POLL
    //     network_safePoll();
    //     cyw43_arch_wait_for_work_until(wifiScanTime);
    // #else
    //     sleep_ms(SLEEP_LOOP_MS);
    //     DPRINTF("Polling...\n");
    // #endif
    // if the USB is connected and the VBUS is high
    // if (usb_mass_get_mounted()) {
    if (usbMassStorageReady != usb_mass_get_mounted()) {
      usbMassStorageReadyPrevious = usbMassStorageReady;
      usbMassStorageReady = usb_mass_get_mounted();
      menu();
    }
    if (usbMassStorageReady) {
      haltCountdown = true;
    }
    tud_task();  // tinyusb device task
    usb_cdc_task();
    switch (appStatus) {
      case APP_EMULATION_RUNTIME: {
        // The app is running in emulation mode

        // Call all the "drives" loops
        chandler_loop();
        if (!gemLaunched) {
          DPRINTF("Jumping to desktop...\n");
          SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_START);
          // SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_NOP);
          gemLaunched = true;
        }
        break;
      }
      case APP_EMULATION_INIT: {
        // The app is running in initialization mode
        // Let's deinit the terminal emulator
        deinit();

        // Initialize Command Handler init
        DPRINTF("Initializing the command handler...\n");
        DPRINTF("Changing the command handler\n");
        dma_setResponseCB(
            chandler_dma_irq_handler_lookup);  // Set the chanlder handler
        chandler_init();                       // Initialize the command handler
        // Initializing the GEMDRIVE
        DPRINTF("Initializing the GEMDRIVE...\n");
        gemdrive_init();
        // Initializing Floppy drives
        DPRINTF("Initializing the floppy drives...\n");
        floppy_init();  // Initialize the floppy drives

        chandler_addCB(gemdrive_loop);  // Add the GEMDRIVE loop
        chandler_addCB(floppy_loop);    // Add the floppy drives loop

        // Check remote commands
        appStatus = APP_EMULATION_RUNTIME;
        DPRINTF("GEMDRIVE initialized\n");
        break;
      }
      case APP_MODE_SETUP:
      default: {
        // Check remote commands
        term_loop();

        if (!haltCountdown) {
          // Check if at least one second (1,000,000 µs) has passed since
          // the last decrement
          absolute_time_t now = get_absolute_time();
          if (absolute_time_diff_us(lastDecrement, now) >= 1000000) {
            // Update the lastDecrement time for the next second
            lastDecrement = now;
            countdown--;
            showCounter(countdown);
            display_refresh();
            if (countdown <= 0) {
              haltCountdown = true;
              appStatus = APP_EMULATION_INIT;
            }
          }
        }
      }
    }
  }

  DPRINTF("Exiting the app loop...\n");

  if (jumpBooster) {
    select_coreWaitPushDisable();  // Disable the SELECT button
    sleep_ms(SLEEP_LOOP_MS);
    // We must reset the computer
    SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_RESET);
    sleep_ms(SLEEP_LOOP_MS);

    // Jump to the booster app
    DPRINTF("Jumping to the booster app...\n");
    reset_jump_to_booster();
  } else {
    // 9. Send CONTINUE computer command to continue booting
    SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_CONTINUE);
  }

  while (1) {
    // Wait for the computer to start
    sleep_ms(SLEEP_LOOP_MS);
  }
}