/**
 * File: chandler.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2023-April 2025
 * Copyright: 2023 2025 - GOODDATA LABS SL
 * Description: Command Handler for the SidecarTridge protocol
 */

#include "chandler.h"

#include "blink.h"
#include "commemul.h"

static TransmissionProtocol pendingProtocol;
static bool protocolPending = false;

static uint32_t incrementalCmdCount = 0;

static uint32_t memorySharedAddress = 0;
static uint32_t memoryRandomTokenAddress = 0;
static uint32_t memoryRandomTokenSeedAddress = 0;

// Head of the callback list
static CommandCallbackNode *callbackListHead = NULL;

static inline void __not_in_flash_func(chandler_clear_pending_protocol)(void) {
  pendingProtocol.command_id = 0;
  pendingProtocol.payload_size = 0;
  pendingProtocol.bytes_read = 0;
  pendingProtocol.final_checksum = 0;
  protocolPending = false;
}

static inline bool __not_in_flash_func(chandler_protocol_matches_pending)(
    const TransmissionProtocol *protocol, uint16_t size) {
  if (!protocolPending) {
    return false;
  }

  if ((pendingProtocol.command_id != protocol->command_id) ||
      (pendingProtocol.payload_size != protocol->payload_size) ||
      (pendingProtocol.final_checksum != protocol->final_checksum)) {
    return false;
  }

  return memcmp(pendingProtocol.payload, protocol->payload, size) == 0;
}

void __not_in_flash_func(chandler_init)() {
  DPRINTF("Initializing Command Handler...\n");  // Print alwayse

  memorySharedAddress = (unsigned int)&__rom_in_ram_start__;
  memoryRandomTokenAddress = memorySharedAddress + CHANDLER_RANDOM_TOKEN_OFFSET;
  memoryRandomTokenSeedAddress =
      memorySharedAddress + CHANDLER_RANDOM_TOKEN_SEED_OFFSET;
  chandler_clear_pending_protocol();
}

/**
 * @brief Register a callback
 *
 * Adds the callback to the linked list of callbacks.
 *
 * @param cb The callback function to register.
 */
void __not_in_flash_func(chandler_addCB)(CommandCallback cb) {
  if (!cb) return;
  CommandCallbackNode *node = malloc(sizeof(*node));
  if (!node) return;
  node->cb = cb;
  node->next = NULL;
  if (!callbackListHead) {
    callbackListHead = node;
  } else {
    CommandCallbackNode *cur = callbackListHead;
    while (cur->next) cur = cur->next;
    cur->next = node;
  }
}

/**
 * @brief CommandCallback that handles the protocol command received.
 *
 * This callback copy the content of the protocol to the last_protocol
 * structure. The last_protocol_valid flag is set to true to indicate that the
 * last_protocol structure contains a valid protocol. We return to the
 * dma_irq_handler_lookup function to continue asap with the next
 *
 * @param protocol The TransmissionProtocol structure containing the protocol
 * information.
 */
static inline void __not_in_flash_func(handle_protocol_command)(
    const TransmissionProtocol *protocol) {
  uint16_t size = tprotocol_clamp_payload_size(protocol->payload_size);

  if (protocolPending) {
    if (!chandler_protocol_matches_pending(protocol, size)) {
      DPRINTF("Ignoring protocol %04x (%u bytes) while %04x is pending\n",
              protocol->command_id, protocol->payload_size,
              pendingProtocol.command_id);
    }
    return;
  }

  tprotocol_copy_safely(&pendingProtocol, protocol);
  protocolPending = true;
}

static inline void __not_in_flash_func(handle_protocol_checksum_error)(
    const TransmissionProtocol *protocol) {
  DPRINTF(
      "Checksum error detected (CommandID=%x, Size=%x, Bytes Read=%x, "
      "Chksum=%x, RTOKEN=%x)\n",
      protocol->command_id, protocol->payload_size, protocol->bytes_read,
      protocol->final_checksum, TPROTO_GET_RANDOM_TOKEN(protocol->payload));
}

static inline void __not_in_flash_func(chandler_consume_rom3_sample)(
    uint16_t sample) {
  uint16_t addr_lsb = (uint16_t)(sample ^ CHANDLER_ADDRESS_HIGH_BIT);

  tprotocol_parse(addr_lsb, handle_protocol_command,
                  handle_protocol_checksum_error);
}

// Invoke this function to process the commands from the active loop in the
// main function
void __not_in_flash_func(chandler_loop)() {
  commemul_poll(chandler_consume_rom3_sample);

  if (!protocolPending) {
    // No command to process
    return;
  }

  // Shared by all commands
  // Read the random token from the command and increment the payload
  // pointer to the first parameter available in the payload
  uint32_t randomToken = TPROTO_GET_RANDOM_TOKEN(pendingProtocol.payload);
  uint16_t *payloadPtr = (uint16_t *)pendingProtocol.payload;
  uint16_t commandId = pendingProtocol.command_id;
  if ((commandId == 0) && (pendingProtocol.payload_size == 0) &&
      (pendingProtocol.final_checksum == 0)) {
    // Invalid command, clear the pending slot and return
    DPRINTF("Invalid command received. Ignoring.\n");
    chandler_clear_pending_protocol();
    return;
  }

  // DPRINTF("Command ID: %4x. Size: %d. Random token: 0x%08X,
  // Checksum:0x%04X\n",
  //         pendingProtocol.command_id, pendingProtocol.payload_size,
  //         randomToken, pendingProtocol.final_checksum);

  // Jump the random token
  TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);

  // #if defined(_DEBUG) && (_DEBUG != 0)
  //     uint16_t *ptr = ((uint16_t *)(lastProtocol).payload);

  //     // Jump the random token
  //     TPROTO_NEXT32_PAYLOAD_PTR(ptr);

  //     // Read the payload parameters
  //     uint16_t payloadSizeTmp = 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= CHANDLER_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D3: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  //     payloadSizeTmp += 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= CHANDLER_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D4: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  //     payloadSizeTmp += 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= CHANDLER_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D5: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  //     payloadSizeTmp += 4;
  //     if ((lastProtocol.payload_size > payloadSizeTmp) &&
  //         (lastProtocol.payload_size <= CHANDLER_PARAMETERS_MAX_SIZE)) {
  //       DPRINTF("Payload D6: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(ptr));
  //       TPROTO_NEXT32_PAYLOAD_PTR(ptr);
  //     }
  // #endif

  for (CommandCallbackNode *cur = callbackListHead; cur; cur = cur->next) {
    if (cur->cb) cur->cb(&pendingProtocol, payloadPtr);
  }
#if defined(CYW43_WL_GPIO_LED_PIN)
  if (blink_isSequenceActive()) {
    incrementalCmdCount++;
    TPROTO_SET_RANDOM_TOKEN64(
        memoryRandomTokenAddress,
        (((uint64_t)incrementalCmdCount) << 32) | randomToken);
    chandler_clear_pending_protocol();
    return;
  }
  blink_activityPulse();
#endif
  // DPRINTF("Command %x executed.IncrementalCmdCount: %x.",
  //         lastProtocol.command_id, incrementalCmdCount);
  incrementalCmdCount++;
  TPROTO_SET_RANDOM_TOKEN64(
      memoryRandomTokenAddress,
      (((uint64_t)incrementalCmdCount) << 32) | randomToken);

  chandler_clear_pending_protocol();
}
