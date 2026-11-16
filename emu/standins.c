/* File: standins.c
 * Description: what the drives-emulator firmware needs from the parts the
 * plugin leaves out (see mdfw.ini) or from hardware emumd has no stand-in
 * for. Nothing here takes part in the floppy emulation.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "f_util.h"
#include "floppy.h"
#include "hardware/rtc.h"
#include "select.h"
#include "usb_mass.h"

/* The SELECT button. The firmware's main loop polls it; emumd keeps it
 * unpressed and drops the callbacks the firmware registers. The bench presses
 * it by creating the file EMU_SELECT_FILE names; a press during emulation does
 * what the firmware's short press does then (emul.c, handleSelectShortPress):
 * drive A goes to its next slot, when it has more than one. The bench only
 * presses it during emulation. */
void select_poll(void) {
  static const char *path;
  static struct timespec last;
  if (!path && !(path = getenv("EMU_SELECT_FILE"))) path = "";
  if (!*path) return;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  if ((now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000 < 20) return;
  last = now;
  if (access(path, F_OK) != 0) return;
  unlink(path);
  if (!floppy_canCycleDriveA()) {
    fprintf(stderr, "emu: SELECT pressed: drive A has one slot, nothing to cycle\n");
    return;
  }
  uint8_t slot = 0;
  FRESULT res = floppy_cycleDriveA(&slot);
  fprintf(stderr, "emu: SELECT pressed: drive A to slot %u (FRESULT %d)\n", slot, (int)res);
}

/* USB mass storage: no USB host is ever connected. */
bool usb_mass_init(void) { return false; }
bool usb_mass_start(void) { return false; }
bool usb_mass_get_mounted(void) { return false; }
void usb_mass_poll(void) {}
void tud_task(void) {}
bool tud_disconnect(void) { return true; }

/* A file's date and time, set by GEMDRIVE only: left as the host has it. */
FRESULT f_utime(const TCHAR *path, const FILINFO *fno) {
  (void)path;
  (void)fno;
  return FR_OK;
}

/* The RP2040's real-time clock: the time the firmware sets, running on with
 * the host's clock. */
static time_t rtc_base;   /* host time when the firmware set the clock */
static struct tm rtc_set; /* what it set */
static bool rtc_on;

void rtc_init(void) { rtc_on = true; }

bool rtc_set_datetime(const datetime_t *t) {
  rtc_set = (struct tm){.tm_year = t->year - 1900, .tm_mon = t->month - 1,
                        .tm_mday = t->day, .tm_hour = t->hour,
                        .tm_min = t->min, .tm_sec = t->sec};
  rtc_base = time(NULL);
  rtc_on = true;
  return true;
}

bool rtc_get_datetime(datetime_t *t) {
  struct tm now = rtc_set;
  now.tm_sec += (int)(time(NULL) - rtc_base);
  timegm(&now); /* normalises the fields */
  *t = (datetime_t){.year = (int16_t)(now.tm_year + 1900),
                    .month = (int8_t)(now.tm_mon + 1), .day = (int8_t)now.tm_mday,
                    .dotw = (int8_t)now.tm_wday, .hour = (int8_t)now.tm_hour,
                    .min = (int8_t)now.tm_min, .sec = (int8_t)now.tm_sec};
  return true;
}

bool rtc_running(void) { return rtc_on; }

/* A test hook, not a stand-in: with EMU_ROM4_DUMP set to a file name, the
 * ROM4 window ($FA0000-$FAFFFF) is written there when Hatari exits, each word
 * high byte first, as the ST reads it. Hatari's debugger cannot show it: it
 * reads Hatari's own cartridge memory, which the fork bypasses. */
extern uint16_t mdfw_rom_in_ram[];

__attribute__((destructor)) static void rom4_dump(void) {
  const char *path = getenv("EMU_ROM4_DUMP");
  if (!path || !*path) return;
  FILE *f = fopen(path, "wb");
  if (!f) return;
  for (size_t i = 0; i < 0x8000; i++) {
    const uint8_t be[2] = {(uint8_t)(mdfw_rom_in_ram[i] >> 8), (uint8_t)mdfw_rom_in_ram[i]};
    fwrite(be, 1, 2, f);
  }
  fclose(f);
}
