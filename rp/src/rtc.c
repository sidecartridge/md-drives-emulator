/**
 * File: rtcemul.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2023-March 2025
 * Copyright: 2023-2025- GOODDATA LABS SL
 * Description: Multi format RTC emulator
 */

#include "rtc.h"

// Communication with the remote computer
static TransmissionProtocol lastProtocol;
static bool lastProtocolValid = false;

// MEmory base
static uint32_t memorySharedAddress = 0;
static uint32_t memoryRandomTokenAddress = 0;
static uint32_t memoryRandomTokenSeedAddress = 0;

// RTC type to emulate
static RTC_TYPE rtcTypeVar = RTC_UNKNOWN;

// Dallas RTC variables
static DallasClock dallasClock = {0};

// NTP and RTC variables
static datetime_t rtcTime = {0};
static NTP_TIME netTime;
static long utcOffsetSeconds = 0;
static char ntpServerHost[SETTINGS_MAX_VALUE_LENGTH] = {0};
static int ntpServerPort = NTP_DEFAULT_PORT;

// Y2K patch
static bool y2kPatchEnabled = false;

static void setUtcOffsetSeconds(long offset) { utcOffsetSeconds = offset; }

static long getUtcOffsetSeconds() { return utcOffsetSeconds; }

static datetime_t *getRtcTime() { return &rtcTime; }

static NTP_TIME *getNetTime() { return &netTime; }

static void hostFoundCB(const char *name, const ip_addr_t *ipaddr, void *arg) {
  if (!name) {
    DPRINTF("NTP host name is NULL\n");
    return;
  }

  NTP_TIME *ntime = (NTP_TIME *)arg;
  if (!ntime) {
    DPRINTF("NTP_TIME argument is NULL\n");
    return;
  }

  if (ipaddr && !ntime->ntp_server_found) {
    ntime->ntp_server_found = true;
    ntime->ntp_ipaddr = *ipaddr;
    DPRINTF("NTP Host found: %s\n", name);
    DPRINTF("NTP Server IP: %s\n", ipaddr_ntoa(&ntime->ntp_ipaddr));
  } else if (!ipaddr) {
    DPRINTF("IP address for NTP Host '%s' not found.\n", name);
    ntime->ntp_error = true;
  }
}

static void ntpRecvCB(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port) {
  // Logging the entry into the callback
  DPRINTF("ntpRecvCB\n");

  // Validate the NTP response
  if (p == NULL || p->tot_len != NTP_MSG_LEN) {
    DPRINTF("Invalid NTP response size\n");
    if (p != NULL) {
      pbuf_free(p);
    }
    return;
  }

  // Ensure we are getting the response from the server we expect
  if (!ip_addr_cmp(&netTime.ntp_ipaddr, addr) || port != ntpServerPort) {
    DPRINTF("Received response from unexpected server or port\n");
    pbuf_free(p);
    return;
  }

  // Extract relevant fields from the NTP message
  uint8_t mode =
      pbuf_get_at(p, 0) & 0x07;         // mode should be 4 for server response
  uint8_t stratum = pbuf_get_at(p, 1);  // stratum should not be 0

  // Check if the message has the correct mode and stratum
  if (mode != 4 || stratum == 0) {
    DPRINTF("Invalid mode or stratum in NTP response\n");
    pbuf_free(p);
    return;
  }

  // Extract the Transmit Timestamp (field starting at byte 40)
  uint32_t transmit_timestamp_secs;
  pbuf_copy_partial(p, &transmit_timestamp_secs,
                    sizeof(transmit_timestamp_secs), 40);
  transmit_timestamp_secs =
      lwip_ntohl(transmit_timestamp_secs) - NTP_DELTA + utcOffsetSeconds;

  // Convert NTP time to a `struct tm`
  time_t utc_sec = transmit_timestamp_secs;
  struct tm *utc = gmtime(&utc_sec);
  if (utc == NULL) {
    DPRINTF("Error converting NTP time to struct tm\n");
    pbuf_free(p);
    return;
  }

  // Fill the rtcTime structure
  rtcTime.year = utc->tm_year + 1900;
  rtcTime.month = utc->tm_mon + 1;
  rtcTime.day = utc->tm_mday;
  rtcTime.hour = utc->tm_hour;
  rtcTime.min = utc->tm_min;
  rtcTime.sec = utc->tm_sec;
  rtcTime.dotw = utc->tm_wday;  // Day of the week, Sunday is day 0

  // Set the RTC with the received time
  if (!rtc_set_datetime(&rtcTime)) {
    DPRINTF("Cannot set internal RTC!\n");
  } else {
    DPRINTF("RP2040 RTC set to: %02d/%02d/%04d %02d:%02d:%02d UTC+0\n",
            rtcTime.day, rtcTime.month, rtcTime.year, rtcTime.hour, rtcTime.min,
            rtcTime.sec);
  }

  // Free the packet buffer
  pbuf_free(p);
  udp_recv(pcb, NULL, NULL);
  udp_remove(pcb);
  netTime.ntp_pcb = NULL;
}

static bool isTrue(const char *value) {
  if ((value == NULL) || (value[0] == '\0')) {
    return false;
  }
  if ((value[0] == 't') || (value[0] == 'T') || (value[0] == 'y') ||
      (value[0] == 'Y') || (value[0] == '1')) {
    return true;
  }
  return false;
}

static void ntp_init() {
  // Attempt to allocate a new UDP control block.
  netTime.ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
  if (netTime.ntp_pcb == NULL) {
    DPRINTF("Failed to allocate a new UDP control block.\n");
    return;
  }

  // Set up the callback function that will be called when an NTP response is
  // received.
  udp_recv(netTime.ntp_pcb, ntpRecvCB, &netTime);

  // Initialization success, set flag.
  netTime.ntp_server_found = false;
  netTime.ntp_error = false;
  DPRINTF("NTP UDP control block initialized and callback set.\n");
}

static void set_internal_rtc() {
  // Begin LwIP operation
  cyw43_arch_lwip_begin();

  if (!netTime.ntp_pcb) {
    DPRINTF("NTP PCB not initialized\n");
    cyw43_arch_lwip_end();
    return;
  }

  // Allocate a pbuf for the NTP request.
  struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
  if (!pb) {
    DPRINTF("Failed to allocate pbuf for NTP request.\n");
    cyw43_arch_lwip_end();
    return;  // Early exit if pbuf allocation fails
  }

  // Prepare the NTP request.
  uint8_t *req = (uint8_t *)pb->payload;
  memset(req, 0, NTP_MSG_LEN);
  req[0] = 0x1b;  // NTP request header for a client request

  // Send the NTP request.
  err_t err =
      udp_sendto(netTime.ntp_pcb, pb, &netTime.ntp_ipaddr, ntpServerPort);
  if (err != ERR_OK) {
    DPRINTF("Failed to send NTP request: %s\n", lwip_strerr(err));
    pbuf_free(pb);  // Clean up the pbuf
    cyw43_arch_lwip_end();
    return;  // Early exit if sending fails
  }

  // Free the pbuf after sending.
  pbuf_free(pb);

  // End LwIP operation.
  cyw43_arch_lwip_end();

  DPRINTF("NTP request sent successfully.\n");
}

// Function to populate the magic_sequence_dallas_rtc
static void populateMagicSequence(uint8_t *sequence, uint64_t hex_value) {
  // Loop through each bit of the 64-bit hex value. Leave the first two bits
  // untouched
  for (int i = 2; i < 66; i++) {
    // Check if the bit is 0 or 1 by shifting hex_value right i positions
    // and checking the least significant bit
    if ((hex_value >> (i - 2)) & 1) {
      sequence[i] = dallasClock.write_address_bit_one;  // If the bit is 1
    } else {
      sequence[i] = dallasClock.write_address_bit_zero;  // If the bit is 0
    }
  }
}

int rtc_queryNTPTime() {
  // We have network connection. Otherwise, we would not be here.
  // Start the internal RTC
  rtc_init();

  SettingsConfigEntry *ntpHost = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_NTP_HOST);
  if (ntpHost != NULL && ntpHost->value != NULL && ntpHost->value[0] != '\0') {
    snprintf(ntpServerHost, SETTINGS_MAX_VALUE_LENGTH, "%s", ntpHost->value);
  } else {
    snprintf(ntpServerHost, SETTINGS_MAX_VALUE_LENGTH, "%s", NTP_DEFAULT_HOST);
  }
  SettingsConfigEntry *ntpPort = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_NTP_PORT);
  if (ntpPort != NULL && ntpPort->value != NULL && ntpPort->value[0] != '\0') {
    int port = atoi(ntpPort->value);
    if (port > 0 && port <= 65535) {
      ntpServerPort = port;
    } else {
      ntpServerPort = NTP_DEFAULT_PORT;
    }
  } else {
    ntpServerPort = NTP_DEFAULT_PORT;
  }
  DPRINTF("NTP server host: %s\n", ntpServerHost);
  DPRINTF("NTP server port: %d\n", ntpServerPort);

  SettingsConfigEntry *utcOffset = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_UTC_OFFSET);
  if (utcOffset != NULL && utcOffset->value != NULL &&
      utcOffset->value[0] != '\0') {
    char *endptr = NULL;
    double offsetHours = strtod(utcOffset->value, &endptr);

    // Check if the entire string was parsed and it's within valid range
    if (endptr != utcOffset->value && *endptr == '\0' && offsetHours >= -12.0 &&
        offsetHours <= 14.0) {
      long offsetSeconds = (long)(offsetHours * 3600);
      setUtcOffsetSeconds(offsetSeconds);
    } else {
      // Optionally: fall back to default or log invalid input
      // setUtcOffsetSeconds(DEFAULT_OFFSET);
    }
  }

  DPRINTF("UTC offset: %ld\n", getUtcOffsetSeconds());

  // Start the NTP client
  ntp_init();
  getNetTime()->ntp_server_found = false;

  bool dns_query_done = false;
  absolute_time_t rtcTimeoutSec =
      make_timeout_time_ms(5 * 1000);  // 3 seconds minimum for network scanning
  uint32_t rtcPoll_ms = 200;           // 200ms

  while ((absolute_time_diff_us(get_absolute_time(), rtcTimeoutSec) > 0) &&
         (getRtcTime()->year == 0)) {
#if PICO_CYW43_ARCH_POLL
    network_safePoll();
#else
    sleep_ms(rtcPoll_ms);
#endif

    if ((getNetTime()->ntp_server_found) && dns_query_done) {
      DPRINTF("NTP server found. Connecting to NTP server...\n");
      getNetTime()->ntp_server_found = false;
      set_internal_rtc();
    }
    // Get the IP address from the DNS server if the wifi is connected and
    // no IP address is found yet
    if (!(dns_query_done)) {
      // Let's connect to ntp server
      DPRINTF("Querying the DNS...\n");
      err_t dns_ret = dns_gethostbyname(
          ntpServerHost, &getNetTime()->ntp_ipaddr, hostFoundCB, getNetTime());
#if PICO_CYW43_ARCH_POLL
      network_safePoll();
#endif
      if (dns_ret == ERR_ARG) {
        DPRINTF("Invalid DNS argument\n");
      }
      DPRINTF("DNS query done\n");
      dns_query_done = true;
    }
    if (getNetTime()->ntp_error) {
      DPRINTF("Error getting the NTP server IP address\n");
      dns_query_done = false;
      getNetTime()->ntp_error = false;
      getNetTime()->ntp_server_found = false;
    }
  }
  if (getRtcTime()->year != 0) {
    DPRINTF("RTC set by NTP server\n");
    rtc_get_datetime(&rtcTime);

    DPRINTF("RP2040 RTC set to: %02d/%02d/%04d %02d:%02d:%02d UTC+0\n",
            rtcTime.day, rtcTime.month, rtcTime.year, rtcTime.hour, rtcTime.min,
            rtcTime.sec);
    if (netTime.ntp_pcb) {
      udp_recv(netTime.ntp_pcb, NULL, NULL);
      udp_remove(netTime.ntp_pcb);
      netTime.ntp_pcb = NULL;
    }
    return 0;  // Success

  } else {
    DPRINTF("Timeout waiting for NTP server\n");
  }
  if (netTime.ntp_pcb) {
    udp_recv(netTime.ntp_pcb, NULL, NULL);
    udp_remove(netTime.ntp_pcb);
    netTime.ntp_pcb = NULL;
  }
  return -1;  // Failure
}

// Function to convert a binary number to BCD format
static uint8_t to_bcd(uint8_t val) { return ((val / 10) << 4) | (val % 10); }

// Function to add two BCD values
static uint8_t add_bcd(uint8_t bcd1, uint8_t bcd2) {
  uint8_t low_nibble = (bcd1 & 0x0F) + (bcd2 & 0x0F);
  uint8_t high_nibble = (bcd1 & 0xF0) + (bcd2 & 0xF0);

  if (low_nibble > 9) {
    low_nibble += 6;
  }

  high_nibble += (low_nibble & 0xF0);  // Add carry to high nibble
  low_nibble &= 0x0F;                  // Keep only the low nibble

  if ((high_nibble & 0x1F0) > 0x90) {
    high_nibble += 0x60;
  }

  return (high_nibble & 0xF0) | (low_nibble & 0x0F);
}
static void set_ikb_datetime_msg(uint32_t mem_shared_addr,
                                 uint16_t rtcemul_datetime_bcd_idx,
                                 uint16_t rtcemul_y2k_patch_idx,
                                 uint16_t rtcemul_datetime_msdos_idx,
                                 uint16_t gemdos_version, bool y2k_patch) {
  uint8_t *rtc_time_ptr =
      (uint8_t *)(mem_shared_addr + rtcemul_datetime_bcd_idx);
  DPRINTF("GEMDOS version: %x\n", gemdos_version);
  rtc_get_datetime(&rtcTime);

  DPRINTF("RP2040 RTC set to: %02d/%02d/%04d %02d:%02d:%02d UTC+0\n",
          rtcTime.day, rtcTime.month, rtcTime.year, rtcTime.hour, rtcTime.min,
          rtcTime.sec);

  // Now set the MSDOS time format after the BCD format
  uint32_t msdos_datetime = 0;

  // Convert the RTC time to MSDOS datetime format
  uint16_t msdos_date =
      ((rtcTime.year - 1980) << 9) | (rtcTime.month << 5) | (rtcTime.day);
  uint16_t msdos_time =
      (rtcTime.hour << 11) | (rtcTime.min << 5) | (rtcTime.sec / 2);

  // Change order for the endianess
  rtc_time_ptr[1] = 0x1b;

  // If negative number, it is EmuTOS
  if ((gemdos_version >= 0) && (y2k_patch)) {
    DPRINTF("Applying Y2K fix in the date\n");
    rtc_time_ptr[0] =
        add_bcd(to_bcd((rtcTime.year % 100)),
                to_bcd((2000 - 1980) + (80 - 30)));  // Fix Y2K issue
  } else {
    DPRINTF("Not applying Y2K fix in the date\n");
    rtc_time_ptr[0] =
        to_bcd(rtcTime.year % 100);  // EmuTOS already handles the Y2K issue
    // If the TOS is EmuTOS, then we disable the Y2K fix
    WRITE_LONGWORD_RAW(mem_shared_addr, rtcemul_y2k_patch_idx, 0);
  }
  rtc_time_ptr[3] = to_bcd(rtcTime.month);
  rtc_time_ptr[2] = to_bcd(rtcTime.day);
  rtc_time_ptr[5] = to_bcd(rtcTime.hour);
  rtc_time_ptr[4] = to_bcd(rtcTime.min);
  rtc_time_ptr[7] = to_bcd(rtcTime.sec);
  rtc_time_ptr[6] = 0x0;

  // Store MSDOS datetime into shared memory
  msdos_datetime = (msdos_date << 16) | msdos_time;
  WRITE_LONGWORD_RAW(mem_shared_addr, rtcemul_datetime_msdos_idx,
                     msdos_datetime);
  DPRINTF("MSDOS datetime: 0x%08x\n", msdos_datetime);
}

void rtc_initf() {
  DPRINTF("RTC init\n");
  memorySharedAddress = (unsigned int)&__rom_in_ram_start__;
  memoryRandomTokenAddress = memorySharedAddress + RTCEMUL_RANDOM_TOKEN_OFFSET;
  memoryRandomTokenSeedAddress =
      memorySharedAddress + RTCEMUL_RANDOM_TOKEN_SEED_OFFSET;

  SET_SHARED_PRIVATE_VAR(RTCEMUL_SVAR_ENABLED, 0, memorySharedAddress,
                         RTCEMUL_SHARED_VARIABLES_OFFSET);
  SET_SHARED_PRIVATE_VAR(RTCEMUL_SVAR_GET_TIME_ADDR, 0, memorySharedAddress,
                         RTCEMUL_SHARED_VARIABLES_OFFSET);

  // RTC type
  SettingsConfigEntry *rtcType =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_TYPE);

  if (rtcType != NULL && rtcType->value != NULL) {
    DPRINTF("RTC type value: %s\n", rtcType->value);

    if (strcmp(rtcType->value, "DALLAS") == 0) {
      DPRINTF("RTC type: DALLAS\n");
      rtcTypeVar = RTC_DALLAS;

      // Initialize Dallas RTC structure
      dallasClock.last_magic_found = 0;
      dallasClock.retries = 0;
      dallasClock.magic_sequence_hex = 0x5ca33ac55ca33ac5;
      dallasClock.read_address_bit = 0x9;
      dallasClock.write_address_bit_zero = 0x1;
      dallasClock.write_address_bit_one = 0x3;
      dallasClock.size_magic_sequence = sizeof(dallasClock.magic_sequence);
      dallasClock.size_clock_sequence = sizeof(dallasClock.clock_sequence);
      dallasClock.rom_address = memorySharedAddress;

      populateMagicSequence(dallasClock.magic_sequence,
                            dallasClock.magic_sequence_hex);

    } else if (strcmp(rtcType->value, "SIDECART") == 0) {
      DPRINTF("RTC type: SIDECART\n");
      rtcTypeVar = RTC_SIDECART;

    } else {
      DPRINTF("RTC type: UNKNOWN\n");
      rtcTypeVar = RTC_UNKNOWN;
    }

  } else {
    DPRINTF("RTC type not found in the settings.\n");
    rtcTypeVar = RTC_UNKNOWN;
  }

  DPRINTF("RTC type: %d\n", rtcTypeVar);
  // Set the RTC type in the shared memory

  // Y2K patch command
  SettingsConfigEntry *y2kPatch = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_Y2K_PATCH);

  if (y2kPatch != NULL && y2kPatch->value != NULL &&
      y2kPatch->value[0] != '\0') {
    DPRINTF("Y2K patch value: %s\n", y2kPatch->value);
    char firstChar = y2kPatch->value[0];
    y2kPatchEnabled =
        (firstChar == 't' || firstChar == 'T' || firstChar == 'y' ||
         firstChar == 'Y' || firstChar == '1');

    WRITE_LONGWORD_RAW(memorySharedAddress, RTCEMUL_Y2K_PATCH,
                       y2kPatchEnabled ? 0xFFFFFFFF : 0);
  } else {
    DPRINTF("Y2K patch not found in the settings or is empty.\n");
    WRITE_LONGWORD_RAW(memorySharedAddress, RTCEMUL_Y2K_PATCH, 0);
  }

  // Set the RTC time for the Atari ST to read
  uint32_t gemdos_version = 0;
  GET_SHARED_VAR(RTCEMUL_SVERSION, &gemdos_version, memorySharedAddress,
                 RTCEMUL_SHARED_VARIABLES_OFFSET);
  DPRINTF("Shared variable SVERSION: %x\n", gemdos_version);
  set_ikb_datetime_msg(memorySharedAddress, RTCEMUL_DATETIME_BCD,
                       RTCEMUL_Y2K_PATCH, RTCEMUL_DATETIME_MSDOS,
                       (int16_t)gemdos_version, y2kPatchEnabled);

  if (memoryRandomTokenAddress != 0) {
    uint32_t randomToken = rand();  // Generate a random 32-bit value
    DPRINTF("Init random token: %08X\n", memoryRandomTokenAddress);
    // Set the random token in the shared memory
    TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenAddress, randomToken);
    // Init the random token seed in the shared memory for the next command
    uint32_t newRandomSeedToken = rand();  // Generate a new random 32-bit value
    DPRINTF("Set the new random token seed: %08X\n", newRandomSeedToken);
    TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenSeedAddress, newRandomSeedToken);
  }

  bool rtcBoolEnabled = false;
  SettingsConfigEntry *rtcBoolEnabledParam = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_DRIVES_RTC_ENABLED);
  if (rtcBoolEnabledParam != NULL) {
    rtcBoolEnabled = isTrue(rtcBoolEnabledParam->value);
  } else {
    rtcBoolEnabled = false;
  }
  DPRINTF("RTC enabled: %s\n", rtcBoolEnabled ? "Yes" : "No");

  SET_SHARED_PRIVATE_VAR(RTCEMUL_SVAR_ENABLED, rtcBoolEnabled ? 0xFFFFFFFF : 0,
                         memorySharedAddress, RTCEMUL_SHARED_VARIABLES_OFFSET);
  DPRINTF("RTC postinit done\n");
}

// Invoke this function to process the commands from the active loop in the
// main function
void __not_in_flash_func(rtc_loop)(TransmissionProtocol *lastProtocol,
                                   uint16_t *payloadPtr) {
  // Only check the RTCEMUL commands
  if (((lastProtocol->command_id >> 8) & 0xFF) != APP_RTCEMUL) return;

  // Handle the command
  switch (lastProtocol->command_id) {
    case RTCEMUL_READ_TIME: {
      // Set the RTC time for the Atari ST to read
      uint32_t gemdos_version = 0;
      GET_SHARED_VAR(RTCEMUL_SVERSION, &gemdos_version, memorySharedAddress,
                     RTCEMUL_SHARED_VARIABLES_OFFSET);
      DPRINTF("Shared variable SVERSION: %x\n", gemdos_version);
      set_ikb_datetime_msg(memorySharedAddress, RTCEMUL_DATETIME_BCD,
                           RTCEMUL_Y2K_PATCH, RTCEMUL_DATETIME_MSDOS,
                           (int16_t)gemdos_version, y2kPatchEnabled);
      DPRINTF("RTCEMUL_READ_TIME received. Setting the time\n");
      break;
    }
    case RTCEMUL_SAVE_VECTORS: {
      uint16_t *payload = ((uint16_t *)(lastProtocol)->payload);
      // Jump the random token
      TPROTO_NEXT32_PAYLOAD_PTR(payload);
      // Extract the 32 bit payload
      uint32_t payload32 = TPROTO_GET_PAYLOAD_PARAM32(payload);
      WRITE_AND_SWAP_LONGWORD(
          memorySharedAddress, RTCEMUL_OLD_XBIOS_TRAP,
          payload32);  // Save the reentry trap address in the shared memory
      DPRINTF("RTCEMUL_SAVE_VECTORS received. Saving the vectors\n");
      break;
    }
    case RTCEMUL_SET_SHARED_VAR: {
      uint16_t *payload = ((uint16_t *)(lastProtocol)->payload);
      // Jump the random token
      TPROTO_NEXT32_PAYLOAD_PTR(payload);
      // Extract the 32 bit payload with the variable index
      uint32_t sharedVarIdx = TPROTO_GET_PAYLOAD_PARAM32(payload);
      TPROTO_NEXT32_PAYLOAD_PTR(payload);
      // Extract the 32 bit payload with the variable value
      uint32_t sharedVarValue = TPROTO_GET_PAYLOAD_PARAM32(payload);
      // Set the shared variable in the shared memory
      SET_SHARED_VAR(sharedVarIdx, sharedVarValue, memorySharedAddress,
                     RTCEMUL_SHARED_VARIABLES_OFFSET);
      DPRINTF("RTCEMUL_SET_SHARED_VAR received. Setting %d to %x\n",
              sharedVarIdx, sharedVarValue);
      break;
    }
    default:
      // Unknown command
      DPRINTF("Unknown command: %x\n", lastProtocol->command_id);
      break;
  }
}
