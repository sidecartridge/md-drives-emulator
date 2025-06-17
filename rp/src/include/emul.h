/**
 * File: emul.h
 * Author: Diego Parrilla Santamaría
 * Date: January 20205
 * Copyright: 2025 - GOODDATA LABS SL
 * Description: Header for the ROM emulator core and setup features
 */

#ifndef EMUL_H
#define EMUL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aconfig.h"
#include "chandler.h"
#include "constants.h"
#include "debug.h"
#include "ff.h"
#include "floppy.h"
#include "gemdrive.h"
#include "httpc/httpc.h"
#include "memfunc.h"
#include "network.h"
#include "pico/stdlib.h"
#include "romemul.h"
#include "rtc.h"
#include "sdcard.h"
#include "select.h"
#include "term.h"
#include "usb_mass.h"

#define WIFI_SCAN_TIME_MS (5 * 1000)
#define DOWNLOAD_START_MS (3 * 1000)
#define DOWNLOAD_DAY_MS (86400 * 1000)
#define SLEEP_LOOP_MS 100

enum {
  APP_EMULATION_RUNTIME = 0,  // Emulation during runtime
  APP_EMULATION_INIT = 1,     // Emulation init
  APP_MODE_NTP_INIT = 2,      // NTP initialization
  APP_MODE_NTP_DONE = 3,      // NTP done
  APP_MODE_SETUP = 255        // Setup
};

typedef enum {
  FLOPPY_FORMAT_SIZE_STATE = 0,        // Format size state
  FLOPPY_FORMAT_NAME_STATE = 1,        // Format name
  FLOPPY_FORMAT_LABEL_STATE = 2,       // Format label
  FLOPPY_FORMAT_CONFIRM_STATE = 3,     // Format confirm
  FLOPPY_FORMAT_DONE_STATE = 4,        // Format done
  FLOPPY_FORMAT_FORMATTING_STATE = 5,  // Formatting
} FloppyFormatState;

// Define the structure to hold floppy image parameters
typedef struct {
  uint16_t template;
  uint16_t num_tracks;
  uint16_t num_sectors;
  uint16_t num_sides;
  uint16_t overwrite;
  char volume_name[14];  // Round to 14 to avoid problems with odd addresses
  char floppy_name[256];
} FloppyImageHeader;

#define APP_MODE_SETUP_STR "255"  // App mode setup string

#define MAX_DOMAIN_LENGTH 255
#define MAX_LABEL_LENGTH 63

/**
 * @brief
 *
 * Launches the ROM emulator application. Initializes terminal interfaces,
 * configures network and storage systems, and loads the ROM data from SD or
 * network sources. Manages the main loop which includes firmware bypass,
 * user interaction and potential system resets.
 */
void emul_start();

#endif  // EMUL_H
