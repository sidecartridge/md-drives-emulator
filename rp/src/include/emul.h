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

#include "acsi.h"
#include "aconfig.h"
#include "chandler.h"
#include "constants.h"
#include "debug.h"
#include "ff.h"
#include "floppy.h"
#include "gemdrive.h"
#include "memfunc.h"
#include "network.h"
#include "pico/stdlib.h"
#include "romemul.h"
#include "rtc.h"
#include "sdcard.h"
#include "select.h"
#include "term.h"
#include "usb_mass.h"

#define SLEEP_LOOP_MS 100

enum {
  APP_EMULATION_RUNTIME = 0,  // Emulation during runtime
  APP_EMULATION_INIT = 1,     // Emulation init
  APP_MODE_NTP_INIT = 2,      // NTP initialization
  APP_MODE_NTP_DONE = 3,      // NTP done
  APP_MODE_SETUP = 255        // Setup
};

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
