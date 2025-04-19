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
#include "constants.h"
#include "debug.h"
#include "download.h"
#include "ff.h"
#include "httpc/httpc.h"
#include "memfunc.h"
#include "network.h"
#include "pico/stdlib.h"
#include "romemul.h"
#include "sdcard.h"
#include "select.h"
#include "term.h"

#define WIFI_SCAN_TIME_MS (5 * 1000)
#define DOWNLOAD_START_MS (3 * 1000)
#define DOWNLOAD_DAY_MS (86400 * 1000)
#define SLEEP_LOOP_MS 100

enum {
  APP_DIRECT = 0,       // Emulation
  APP_MODE_SETUP = 255  // Setup
};

#define APP_MODE_SETUP_STR "255"  // App mode setup string

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
