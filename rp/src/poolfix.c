/**
 * File: poolfix.c
 * Author: Diego Parrilla Santamaría
 * Date: September 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: RP side of the GEMDOS pool fix for TOS 1.04 and 1.06.
 */

#include "poolfix.h"

static bool poolfixAddressValid(uint32_t address) {
  return address >= POOLFIX_WINDOW_START &&
         address + 4u <= POOLFIX_WINDOW_END && (address & 0x3u) == 0u;
}

void poolfix_init(void) {
  SettingsConfigEntry *enabled =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_POOLFIX_ENABLED);
  // Unset reads as enabled, the setting's default.
  bool on = (enabled == NULL) || (enabled->value[0] == 't') ||
            (enabled->value[0] == 'T') || (enabled->value[0] == 'y') ||
            (enabled->value[0] == 'Y') || (enabled->value[0] == '1');
  WRITE_AND_SWAP_LONGWORD((uint32_t)&__rom_in_ram_start__,
                          POOLFIX_ENABLED_ADDR & 0xFFFFu,
                          on ? 0xFFFFFFFFu : 0u);
  DPRINTF("Pool fix: %s\n", on ? "enabled" : "disabled");
}

void __not_in_flash_func(poolfix_loop)(TransmissionProtocol *lastProtocol,
                                       uint16_t *payloadPtr) {
  if (((lastProtocol->command_id >> 8) & 0xFF) != APP_POOLFIX) return;

  uint32_t memory = (uint32_t)&__rom_in_ram_start__;
  switch (lastProtocol->command_id) {
    case POOLFIX_SET_LONG: {
      uint32_t address = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);
      uint32_t value = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
      if (!poolfixAddressValid(address)) {
        DPRINTF("Pool fix: refused write to %08lX\n", (unsigned long)address);
        break;
      }
      WRITE_AND_SWAP_LONGWORD(memory, address & 0xFFFFu, value);
      if (value > 1) {
        DPRINTF("Pool fix: installed, GEMDOS entry %08lX\n",
                (unsigned long)value);
      }
      break;
    }
    case POOLFIX_COMPACTED: {
      uint32_t flagAddress = TPROTO_GET_PAYLOAD_PARAM32(payloadPtr);
      uint32_t released = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
      uint32_t freeLeft = TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payloadPtr);
      if (poolfixAddressValid(flagAddress)) {
        WRITE_AND_SWAP_LONGWORD(memory, flagAddress & 0xFFFFu, 0);
      }
      DPRINTF("Pool fix: compacted, %lu blocks released, %lu slots free\n",
              (unsigned long)released, (unsigned long)freeLeft);
      break;
    }
    default:
      break;
  }
}
