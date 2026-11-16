/* f_util.h: the SD card library's FatFs helpers (fatfs-sdk). Only
 * FRESULT_str() is used, which emumd's FatFs has; and f_utime(), a FatFs call
 * emumd's FatFs lacks, which only GEMDRIVE uses (emu/standins.c). */
#ifndef EMU_F_UTIL_H
#define EMU_F_UTIL_H

#include "ff.h"

const char *FRESULT_str(FRESULT i);
FRESULT f_utime(const TCHAR *path, const FILINFO *fno);

#endif
