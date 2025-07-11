/**
 * File: tprotocol.h
 * Author: Diego Parrilla Santamaría
 * Date: August 2023 - January 20205
 * Copyright: 2023-25 - GOODDATA LABS SL
 * Description: Parse the protocol used to communicate with the ROM
 */

#ifndef TPROTOCOL_H
#define TPROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "debug.h"

#define PROTOCOL_CLEAR_MEMORY \
  0  // Set to 1 to clear the memory before starting the protocol

#define PROTOCOL_HEADER 0xABCD
#define PROTOCOL_READ_RESTART_MICROSECONDS 10000
#define MAX_PROTOCOL_PAYLOAD_SIZE \
  2048 + 64  // 2048 bytes of payload plus 64 bytes of overhead for safety

#define SHOW_COMMANDS 0  // Set to 1 to show commands received

/**
 * @brief Macro to get a random token from a payload.
 *
 * This macro takes a payload as input and returns a random token generated from
 * the payload.
 *
 * @param payload The payload from which to generate the token.
 * @return The generated random token.
 */
#define TPROTO_GET_RANDOM_TOKEN(payload)                           \
  ({                                                               \
    volatile uint32_t *__ptr = (volatile uint32_t *)(payload);     \
    ((*__ptr & 0xFFFF0000) >> 16) | ((*__ptr & 0x0000FFFF) << 16); \
  })

/**
 * @brief Macro to set a random token to a memory address.
 *
 * This macro sets a random token to the specified memory address.
 *
 * @param mem_address The memory address to set the token to.
 * @param token The token value to set.
 */
// #define TPROTO_SET_RANDOM_TOKEN(mem_address, token) \
//   *((volatile uint32_t *)(mem_address)) = token;

#define TPROTO_SET_RANDOM_TOKEN(mem_address, token) \
  do {                                              \
    *((volatile uint32_t *)(mem_address)) = token;  \
  } while (0)

#define TPROTO_SET_RANDOM_TOKEN64(mem_address, token64) \
  do {                                                  \
    *((volatile uint64_t *)(mem_address)) = token64;    \
  } while (0)

#define TPROTO_NEXT32_PAYLOAD_PTR(payload) (payload += 2)

#define TPROTO_NEXT16_PAYLOAD_PTR(payload) (payload += 1)

#define TPROTO_GET_PAYLOAD_PARAM32(payload) \
  (((uint32_t)(payload)[1] << 16) | (payload)[0])

#define TPROTO_GET_PAYLOAD_PARAM16(payload) (((uint16_t)(payload)[0]))

#define TPROTO_GET_NEXT32_PAYLOAD_PARAM32(payload) \
  (payload += 2, (((uint32_t)(payload)[1] << 16) | (payload)[0]))

#define TPROTO_GET_NEXT16_PAYLOAD_PARAM32(payload) \
  (payload += 1, (((uint32_t)(payload)[1] << 16) | (payload)[0]))

#define TPROTO_GET_NEXT32_PAYLOAD_PARAM16(payload) \
  (payload += 2, ((uint16_t)(payload)[0]))

#define TPROTO_GET_NEXT16_PAYLOAD_PARAM16(payload) \
  (payload += 1, ((uint16_t)(payload)[0]))

typedef enum {
  HEADER_DETECTION,
  COMMAND_READ,
  PAYLOAD_SIZE_READ,
  PAYLOAD_READ_START,
  PAYLOAD_READ_INPROGRESS,
  PAYLOAD_READ_END
} TPParseStep;

typedef struct __attribute__((packed, aligned(4))) {
  uint16_t command_id;    // Command ID
  uint16_t payload_size;  // Size of the payload
  uint16_t bytes_read;  // To keep track of how many bytes of the payload we've
                        // read so far.
  uint16_t final_checksum;  // Accumulate a 16-bit sum of all data read
  uint16_t
      payload[MAX_PROTOCOL_PAYLOAD_SIZE / 2];  // Pointer to the payload data
} TransmissionProtocol;

// Function to handle the commands received
typedef void (*ProtocolCallback)(const TransmissionProtocol *);

// Function to handle what to do if the checksum is wrong
typedef void (*ProtocolChecksumErrorCallback)(const TransmissionProtocol *);

static uint32_t last_header_found = 0;
static uint32_t new_header_found = 0;

static TPParseStep nextTPstep = HEADER_DETECTION;

// Placeholder structure for parsed data (declared in tprotocol.h)
static TransmissionProtocol transmission = {0};

// This function is called once we finish reading the command + payload
static inline __attribute__((always_inline)) void __not_in_flash_func(
    process_command)(ProtocolCallback callback) {
#if defined(_DEBUG) && (_DEBUG != 0) && defined(SHOW_COMMANDS) && \
    (SHOW_COMMANDS != 0)
  DPRINTF(
      "COMMAND: %d / PAYLOAD SIZE: %d / CHECKSUM: 0x%04X / RTOKEN: 0x%04X\n",
      transmission.command_id, transmission.payload_size,
      transmission.final_checksum,
      TPROTO_GET_RANDOM_TOKEN(transmission.payload));
#endif

  if (callback) {
    callback(&transmission);
  }

#if PROTOCOL_CLEAR_MEMORY == 1
  // Reset for next message
  memset(&transmission, 0, sizeof(TransmissionProtocol));
#endif

  // last_header_found = 0;
  // nextTPstep = HEADER_DETECTION;
}

/**
 * @brief Parses protocol data and processes commands.
 *
 * This function processes a 16-bit data value based on the current protocol
 * state. It updates the protocol state, accumulates checksum, and calls
 * appropriate callbacks when a command is fully received or a checksum error
 * occurs.
 *
 * @param data The incoming 16-bit data.
 * @param callback Function pointer that is called upon successful command
 * parsing.
 * @param protocolChecksumErrorCallback Function pointer that is called when a
 * checksum error is detected.
 */
static inline void __not_in_flash_func(tprotocol_parse)(
    uint16_t data, ProtocolCallback callback,
    ProtocolChecksumErrorCallback protocolChecksumErrorCallback) {
  // Time-based logic to detect if we should restart parsing
  new_header_found = timer_hw->timerawl;
  if (new_header_found - last_header_found >
      PROTOCOL_READ_RESTART_MICROSECONDS) {
    nextTPstep = HEADER_DETECTION;
  }

  switch (nextTPstep) {
    case HEADER_DETECTION:
      if (data == PROTOCOL_HEADER) {
        // Move to command read
        nextTPstep = COMMAND_READ;
      }
      last_header_found = new_header_found;
      break;

    case COMMAND_READ:
      transmission.command_id = data;
      nextTPstep = PAYLOAD_SIZE_READ;
      break;

    case PAYLOAD_SIZE_READ:
      transmission.payload_size = data;
    case PAYLOAD_READ_START:
      transmission.bytes_read = 0;
      nextTPstep = PAYLOAD_READ_INPROGRESS;
      if (data == 0) {
        nextTPstep = PAYLOAD_READ_END;
      }
      break;
    case PAYLOAD_READ_INPROGRESS:
      // Store the 16-bit chunk into the payload array
      asm("strh %0, [%1]"
          :
          : "r"(data), "r"(&transmission.payload[(transmission.bytes_read / 2)])
          : "memory");
      // *((uint16_t *)(&transmission.payload[transmission.bytes_read])) =
      // data;
      transmission.bytes_read += 2;
      if (transmission.bytes_read >= transmission.payload_size) {
        nextTPstep = PAYLOAD_READ_END;
      }
      break;
    case PAYLOAD_READ_END:
      // Calculate the checksum now
      transmission.final_checksum =
          transmission.command_id + transmission.payload_size;
      uint16_t *payload_ptr = transmission.payload;
      transmission.final_checksum =
          transmission.command_id + transmission.payload_size;
      for (uint16_t i = 0; i < transmission.payload_size / 2; i++) {
        transmission.final_checksum += *(payload_ptr++);
      }

      last_header_found = 0;
      nextTPstep = HEADER_DETECTION;

      // "data" is the checksum
      if (data == transmission.final_checksum) {
        // Checksum matches
        process_command(callback);
      } else {
        // Checksum mismatch. Notify the caller
        protocolChecksumErrorCallback(&transmission);
      }
      break;
    default:
      // Invalid state, reset to header detection
      nextTPstep = HEADER_DETECTION;
      break;
  }
};

#endif  // TPROTOCOL_H
