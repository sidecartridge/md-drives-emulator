/**
 * File: chandler.c
 * Author: Diego Parrilla Santamaría
 * Date: November 2023-April 2025
 * Copyright: 2023 2025 - GOODDATA LABS SL
 * Description: Command Handler for the SidecarTridge protocol
 */

#include "chandler.h"

#include "pico/sem.h"  // semaphore API

static TransmissionProtocol lastProtocol;
static semaphore_t command_sem;

static uint32_t incrementalCmdCount = 0;

static uint32_t memorySharedAddress = 0;
static uint32_t memoryRandomTokenAddress = 0;
static uint32_t memoryRandomTokenSeedAddress = 0;

// Head of the callback list
static CommandCallbackNode *callbackListHead = NULL;

// State variables
static bool ledOff = false;
static absolute_time_t lastTransitionTime;  // Time of the last state change
static absolute_time_t now = {0};           // Current time

void __not_in_flash_func(chandler_init)() {
  DPRINTF("Initializing Command Handler...\n");  // Print alwayse

  memorySharedAddress = (unsigned int)&__rom_in_ram_start__;
  memoryRandomTokenAddress = memorySharedAddress + CHANDLER_RANDOM_TOKEN_OFFSET;
  memoryRandomTokenSeedAddress =
      memorySharedAddress + CHANDLER_RANDOM_TOKEN_SEED_OFFSET;
  sem_init(&command_sem, 0, 1);
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
  // The command can be processed if there is no protocol already in
  // progress
  if (sem_try_acquire(&command_sem)) {
    // Copy the content of protocol to last_protocol
    // Sanity check: clamp payload_size to avoid overflow
    uint16_t size = protocol->payload_size;
    if (size > MAX_PROTOCOL_PAYLOAD_SIZE) {
      size = MAX_PROTOCOL_PAYLOAD_SIZE;
    }

    // Copy only used payload bytes
    memcpy(lastProtocol.payload, protocol->payload, size);

    // Copy the 8-byte header directly
    lastProtocol.command_id = protocol->command_id;
    lastProtocol.payload_size = protocol->payload_size;
    lastProtocol.bytes_read = protocol->bytes_read;
    lastProtocol.final_checksum = protocol->final_checksum;

    sem_release(&command_sem);
  } else {
    // If a protocol is already in progress, ignore the new one
    DPRINTF(
        "PROTOCOL %04x(%04x) ALREADY IN PROGRESS. IGNORING THE NEW COMMAND: "
        "%04x(%04x)\n",
        lastProtocol.command_id, lastProtocol.final_checksum,
        protocol->command_id, protocol->final_checksum);
  }
};

static inline void __not_in_flash_func(handle_protocol_checksum_error)(
    const TransmissionProtocol *protocol) {
  DPRINTF(
      "Checksum error detected (CommandID=%x, Size=%x, Bytes Read=%x, "
      "Chksum=%x, RTOKEN=%x)\n",
      protocol->command_id, protocol->payload_size, protocol->bytes_read,
      protocol->final_checksum, TPROTO_GET_RANDOM_TOKEN(protocol->payload));
}

// Interrupt handler for DMA completion
void __not_in_flash_func(chandler_dma_irq_handler_lookup)(void) {
  // Read the rom3 signal and if so then process the command

  dma_hw->ints1 = 1U << 2;

  // Read once to avoid redundant hardware access
  uint32_t addr = dma_hw->ch[2].al3_read_addr_trig;

  // Check ROM3 signal (bit 16)
  // We expect that the ROM3 signal is not set very often, so this should help
  // the compilar to run faster
  if (__builtin_expect(addr & 0x00010000, 0)) {
    // Invert highest bit of low word to get 16-bit address
    uint16_t addr_lsb = (uint16_t)(addr ^ CHANDLER_ADDRESS_HIGH_BIT);

    tprotocol_parse(addr_lsb, handle_protocol_command,
                    handle_protocol_checksum_error);
  }
}

// Invoke this function to process the commands from the active loop in the
// main function
void __not_in_flash_func(chandler_loop)() {
  sem_acquire_blocking(&command_sem);

  // Shared by all commands
  // Read the random token from the command and increment the payload
  // pointer to the first parameter available in the payload
  uint32_t randomToken = TPROTO_GET_RANDOM_TOKEN(lastProtocol.payload);
  uint16_t *payloadPtr = (uint16_t *)lastProtocol.payload;
  uint16_t commandId = lastProtocol.command_id;
  DPRINTF("Command ID: %4x. Size: %d. Random token: 0x%08X, Checksum:0x%04X\n",
          lastProtocol.command_id, lastProtocol.payload_size, randomToken,
          lastProtocol.final_checksum);

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
    if (cur->cb) cur->cb(&lastProtocol, payloadPtr);
  }

#if defined(CYW43_WL_GPIO_LED_PIN)
  // If LED is on, check if it has been off for at least LED_ON_DELAY_US and
  // turn it on
  now = get_absolute_time();
  if (absolute_time_diff_us(lastTransitionTime, now) >= LED_ON_DELAY_US) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);  // Turn LED off
    ledOff = true;
    lastTransitionTime = now;  // Update the transition time
  } else {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);  // Turn LED on
    ledOff = false;
    lastTransitionTime = now;  // Update the transition time
  }
#endif
  // DPRINTF("Command %x executed.IncrementalCmdCount: %x.",
  //         lastProtocol.command_id, incrementalCmdCount);
  incrementalCmdCount++;
  TPROTO_SET_RANDOM_TOKEN64(
      memoryRandomTokenAddress,
      (((uint64_t)incrementalCmdCount) << 32) | randomToken);
}
