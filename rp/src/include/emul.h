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
#include "poolfix.h"
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

// App commands for the debug-only devhooks mailbox (`swd.py app NAME`).
// Debug builds only; see rp/src/include/devhooks.h.
// Stops the setup-menu boot countdown, as if a key had been pressed.
#define DEVHOOKS_APP_COUNTDOWN_STOP 1
// Makes the next N GEMDRIVE write chunks stall after the data is committed but
// before the ST is answered — the exact shape of a lost write answer: the
// write happened, the answer was lost, and the ST re-sends the chunk. Used to
// validate the Fwrite chunk dedup on hardware. Payload word 0 = how many
// chunks to stall, word 1 = stall length in 100 ms units (default 20 = 2 s;
// use ≥ 100 to outlast the ST's write timeout and force a retry).
#define DEVHOOKS_APP_GEMDRIVE_STALL 2
// Makes the next N GEMDRIVE write chunks fail as an SD error, through the
// real error path. Payload word 0 = how many chunks (default 1).
#define DEVHOOKS_APP_GEMDRIVE_FAIL_WRITE 3
// Holds the given number of KB of heap (payload word 0), on top of what is
// already held, to test what the firmware does when memory runs out; 0 KB
// releases everything held. Result 0 when the allocation is refused.
#define DEVHOOKS_APP_HEAP_HOLD 4
// Makes the next floppy read of the given logical sector (payload word 0), on
// either drive, fail as an SD error, through the real error path.
#define DEVHOOKS_APP_FLOPPY_FAIL_READ 5

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
