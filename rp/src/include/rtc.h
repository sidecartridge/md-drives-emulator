/**
 * File: rtc.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023-2025
 * Copyright: 2023-2025 - GOODDATA LABS SL
 * Description: Header file for the RTC emulator C program.
 */

#ifndef RTC_H
#define RTC_H

#include "aconfig.h"
#include "chandler.h"
#include "constants.h"
#include "debug.h"
#include "hardware/rtc.h"
#include "lwip/dns.h"
#include "lwip/udp.h"
#include "memfunc.h"
#include "network.h"
#include "pico/cyw43_arch.h"
#include "time.h"
#include "tprotocol.h"

#define RTCEMUL_RANDOM_TOKEN_OFFSET (CHANDLER_RANDOM_TOKEN_OFFSET)
#define RTCEMUL_RANDOM_TOKEN_SEED_OFFSET (CHANDLER_RANDOM_TOKEN_SEED_OFFSET)

#define RTCEMUL_SHARED_VARIABLES_OFFSET (CHANDLER_SHARED_VARIABLES_OFFSET)

// Index for the common shared variables
#define RTCEMUL_HARDWARE_TYPE (CHANDLER_HARDWARE_TYPE)
#define RTCEMUL_SVERSION (CHANDLER_SVERSION)

// 0xA208 ├────────────────────────────────────────────┤
//        │ RTCEMUL_SVAR_XBIOS_TRAP_ENABLED            │
//        │   size 4 bytes                             │
// 0xA20C ├────────────────────────────────────────────┤
//        │ RTCEMUL_SVAR_BOOT_ENABLED                  │
//        │   size 4 bytes                             │
// 0xA210 ├────────────────────────────────────────────┤
//        │ RTCEMUL_SVAR_EMULATION_MODE                │
//        │   size 4 bytes                             │
// 0xA214 ├────────────────────────────────────────────┤
//        │ Empty space...                             │
//        ...
// 0xA228 ├────────────────────────────────────────────┤
//        │ RTCEMUL_VARIABLES_OFFSET.                  │
//        ├────────────────────────────────────────────┤

// We need a gap of 6KB after the random token and seed
#define RTCEMUL_GAP_SIZE 0x2000  // 8KB gap
#define RTCEMUL_SHARED_VARIABLE_SIZE \
  (RTCEMUL_GAP_SIZE / 4)  // 6KB gap divided by 4 bytes per longword
#define RTCEMUL_SHARED_VARIABLES_COUNT \
  (32)  // Size of the shared variables of the shared functions

// Now the index for the shared variables of the program
#define RTCEMUL_SVAR_ENABLED (RTCEMUL_SHARED_VARIABLE_SIZE + 0)  // enabled flag
#define RTCEMUL_SVAR_GET_TIME_ADDR \
  (RTCEMUL_SHARED_VARIABLE_SIZE + 1)  //  Get time address

// We will need 32 bytes extra for the variables of the floppy emulator
#define RTCEMUL_VARIABLES_OFFSET                    \
  (RTCEMUL_RANDOM_TOKEN_OFFSET + RTCEMUL_GAP_SIZE + \
   RTCEMUL_SHARED_VARIABLES_COUNT)  // random_token + gap + shared
                                    // variables
                                    // size

#define RTCEMUL_DATETIME_BCD (RTCEMUL_VARIABLES_OFFSET)
#define RTCEMUL_DATETIME_MSDOS \
  (RTCEMUL_DATETIME_BCD + 8)  // datetime_bcd + 8 bytes
#define RTCEMUL_OLD_XBIOS_TRAP \
  (RTCEMUL_DATETIME_MSDOS + 8)  // datetime_msdos + 8 bytes // old_bios trap + 4
                                // bytes
#define RTCEMUL_Y2K_PATCH (RTCEMUL_OLD_XBIOS_TRAP + 4)  // reentry_trap + 4 byte

#define NTP_DEFAULT_HOST "pool.ntp.org"
#define NTP_DEFAULT_PORT 123
#define NTP_DELTA 2208988800  // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_MSG_LEN 48        // ignore Authenticator (optional)

// The commands code is the combinatino of two bytes:
// - The most significant byte is the application code. All the commands of an
// app should have the same code
// - The least significant byte is the command code. Each command of an app
// should have a different code
#define APP_RTCEMUL 0x03  // The RTC emulator app

// APP_RTCEMUL commands
#define RTCEMUL_READ_TIME \
  (APP_RTCEMUL << 8 | 1)  // Read the time from the internal RTC
#define RTCEMUL_SAVE_VECTORS \
  (APP_RTCEMUL << 8 | 2)  // Save the vectors of the RTC emulator
#define RTCEMUL_SET_SHARED_VAR (APP_RTCEMUL << 8 | 3)  // Set a shared variable

#define RTCEMUL_PARAMETERS_MAX_SIZE 20  // Maximum size of the parameters

typedef enum {
  RTC_SIDECART,
  RTC_DALLAS,
  RTC_AREAL,
  RTC_FMCII,
  RTC_UNKNOWN
} RTC_TYPE;

typedef struct NTP_TIME_T {
  ip_addr_t ntp_ipaddr;
  struct udp_pcb *ntp_pcb;
  bool ntp_server_found;
  bool ntp_error;
} NTP_TIME;

// DAllas RTC. Info here:
// https://pdf1.alldatasheet.es/datasheet-pdf/view/58439/DALLAS/DS1216.html
typedef struct {
  uint64_t last_magic_found;
  uint16_t retries;
  uint64_t magic_sequence_hex;
  uint8_t clock_sequence[64];
  uint8_t read_address_bit;
  uint8_t write_address_bit_zero;
  uint8_t write_address_bit_one;
  uint8_t magic_sequence[66];
  uint16_t size_magic_sequence;
  uint16_t size_clock_sequence;
  uint32_t rom_address;
} DallasClock;

int rtc_queryNTPTime();
void rtc_initf();  // rtc_init already defined in the SDK
void __not_in_flash_func(rtc_loop)(TransmissionProtocol *lastProtocol,
                                   uint16_t *payloadPtr);

#endif  // RTC_H
