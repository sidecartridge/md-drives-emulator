/**
 * File: poolfix.h
 * Author: Diego Parrilla Santamaría
 * Date: September 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: RP side of the GEMDOS pool fix for TOS 1.04 and 1.06.
 */

#ifndef POOLFIX_H
#define POOLFIX_H

#include "aconfig.h"
#include "chandler.h"
#include "constants.h"
#include "debug.h"
#include "memfunc.h"
#include "tprotocol.h"

// The pool fix code on the ST (target/atarist/src/poolfix.s) runs from this
// window of the cartridge ROM. Its two variables live there too, and only the
// RP can write them.
#define POOLFIX_WINDOW_START 0xFA4C00u
#define POOLFIX_WINDOW_END 0xFA5400u
// pf_enabled in poolfix.s: not 0 lets the ST install the fix
#define POOLFIX_ENABLED_ADDR 0xFA4C04u

#define APP_POOLFIX 0x06  // The GEMDOS pool fix app

// d3 = address inside the pool fix window, d4 = long to store there
#define POOLFIX_SET_LONG (APP_POOLFIX << 8 | 0)
// The pool was compacted: clear the flag. d3 = the flag's address, d4 = blocks
// released, d5 = free descriptor slots left
#define POOLFIX_COMPACTED (APP_POOLFIX << 8 | 1)

// Tell the ST side whether to install the fix, from the POOLFIX_ENABLED
// setting. Call before the ST starts the cartridge drivers.
void poolfix_init(void);

void __not_in_flash_func(poolfix_loop)(TransmissionProtocol *lastProtocol,
                                       uint16_t *payloadPtr);

#endif  // POOLFIX_H
