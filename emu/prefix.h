/* Included ahead of every source (cflags -include): stand-ins for firmware
 * headers that a neighbour includes in quotes, so they would be found in the
 * firmware's own folder first. Each stand-in uses the original's include
 * guard, so the original is then skipped. */
#include "reset.h"    /* emumd's template stand-in: no ARM code */
#include "usb_mass.h" /* ours: no TinyUSB */

/* newlib's sys/types.h gives the RP2040 these; unsigned long is 32 bits there. */
#include <stdint.h>
#include <sys/types.h>
typedef uint32_t ulong;
