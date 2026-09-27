/**
 * File: usb_mass.h
 * Author: Diego Parrilla Santamaría
 * Date: June 2024
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: Header for usb_mass.c which manages the USB Mass storage device
 * of the SD card
 */

#ifndef USB_MASS_H
#define USB_MASS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blink.h"
#include "constants.h"
#include "debug.h"
#include "diskio.h" /* Declarations of disk functions */
#include "f_util.h"
#include "ff.h"
#include "sd_card.h"
#include "tusb.h"

// For resetting the USB controller
#include "hardware/resets.h"

#define USBDRIVE_READ_ONLY false
#define USBDRIVE_MASS_STORE true

// Init USB Mass storage device
bool usb_mass_init(void);
bool usb_mass_start(void);
// True while a host has the card mounted; false again once the host ejects it,
// even if the cable stays connected.
bool usb_mass_get_mounted(void);
// Call right after every tud_task(): writes the chunk the host just sent and
// reads ahead the next one, while USB transfers in the background.
void usb_mass_poll(void);

#endif  // USB_MASS_H
