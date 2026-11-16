/* usb_mass.h: the firmware's USB mass storage (the microSD card as a USB
 * drive), left out of the plugin with TinyUSB. Same include guard as the
 * original, which emul.h includes in quotes, so emu/prefix.h includes this
 * one first. No USB host is ever connected: emu/standins.c answers so. */
#ifndef USB_MASS_H
#define USB_MASS_H

#include <stdbool.h>

#define USBDRIVE_READ_ONLY false
#define USBDRIVE_MASS_STORE true

bool usb_mass_init(void);
bool usb_mass_start(void);
bool usb_mass_get_mounted(void);
void usb_mass_poll(void);

/* TinyUSB's device task and disconnect, which emul.c calls. */
void tud_task(void);
bool tud_disconnect(void);

#endif
