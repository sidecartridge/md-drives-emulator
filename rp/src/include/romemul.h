/**
 * File: romemul.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023-2025
 * Copyright: 2023-2025 - GOODDATA LABS SL
 * Description: Header file for the ROM emulator C program.
 */

#ifndef ROMEMUL_H
#define ROMEMUL_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "../../build/romemul.pio.h"
#include "constants.h"
#include "debug.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/vreg.h"
#include "memfunc.h"
#include "pico/stdlib.h"

#define ROMEMUL_BUS_BITS 17

typedef void (*IRQInterceptionCallback)();

// extern int read_addr_rom_dma_channel;
// extern int lookup_data_rom_dma_channel;

// Function Prototypes
int init_romemul(IRQInterceptionCallback requestCallback,
                 IRQInterceptionCallback responseCallback, bool copyFlashToRAM);

void dma_irqHandlerLookup(void);
void dma_irqHandlerAddress(void);
void dma_setResponseCB(IRQInterceptionCallback responseCallback);

#endif  // ROMEMUL_H