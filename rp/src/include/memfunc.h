/**
 * @file memfunc.h
 * @brief Header file with the macros and functions of the memory functions.
 * Author: Diego Parrilla Santamaría
 * Date: August 2024-2025
 * Copyright: 2024-2025 - GOODDATA LABS SL
 * Description: Header file with the macros and functions of the memory
 * functions.
 */

#ifndef MEMFUNC_H
#define MEMFUNC_H

#include <stdint.h>

#include "constants.h"
#include "debug.h"
#include "hardware/dma.h"
#include "hardware/structs/xip_ctrl.h"

static inline volatile uint8_t *memfunc_byte_ptr(const uint32_t address,
                                                 const uint32_t offset) {
  return (volatile uint8_t *)((uintptr_t)address + (uintptr_t)offset);
}

static inline void memfunc_write_u8(const uint32_t address,
                                    const uint32_t offset,
                                    const uint8_t value) {
  volatile uint8_t *ptr = memfunc_byte_ptr(address, offset);
  ptr[0] = value;
}

static inline void memfunc_write_u16(const uint32_t address,
                                     const uint32_t offset,
                                     const uint16_t value) {
  volatile uint8_t *ptr = memfunc_byte_ptr(address, offset);
  ptr[0] = (uint8_t)(value & 0xFFu);
  ptr[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static inline void memfunc_write_u32(const uint32_t address,
                                     const uint32_t offset,
                                     const uint32_t value) {
  volatile uint8_t *ptr = memfunc_byte_ptr(address, offset);
  ptr[0] = (uint8_t)(value & 0xFFu);
  ptr[1] = (uint8_t)((value >> 8) & 0xFFu);
  ptr[2] = (uint8_t)((value >> 16) & 0xFFu);
  ptr[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static inline uint8_t memfunc_read_u8(const uint32_t address,
                                      const uint32_t offset) {
  volatile uint8_t *ptr = memfunc_byte_ptr(address, offset);
  return ptr[0];
}

static inline uint16_t memfunc_read_u16(const uint32_t address,
                                        const uint32_t offset) {
  volatile uint8_t *ptr = memfunc_byte_ptr(address, offset);
  return (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
}

static inline uint32_t memfunc_read_u32(const uint32_t address,
                                        const uint32_t offset) {
  volatile uint8_t *ptr = memfunc_byte_ptr(address, offset);
  return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) |
         ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

#define COPY_FIRMWARE_TO_RAM(emulROM, emulROM_length)  \
  do {                                                 \
    COPY_FIRMWARE_TO_RAM_DMA(emulROM, emulROM_length); \
  } while (0)

#define COPY_FIRMWARE_TO_RAM_DMA(emulROM, emulROM_length)                     \
  do {                                                                        \
    while (!(xip_ctrl_hw->stat & XIP_STAT_FIFO_EMPTY))                        \
      (void)xip_ctrl_hw->stream_fifo;                                         \
    xip_ctrl_hw->stream_addr = (uint32_t)&(emulROM)[0];                       \
    xip_ctrl_hw->stream_ctr = (emulROM_length) / 2;                           \
    const uint dma_chan = dma_claim_unused_channel(true);                     \
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);        \
    channel_config_set_read_increment(&cfg, false);                           \
    channel_config_set_write_increment(&cfg, true);                           \
    channel_config_set_dreq(&cfg, DREQ_XIP_STREAM);                           \
    dma_channel_configure(dma_chan, &cfg,                                     \
                          (void *)&__rom_in_ram_start__, /* Write addr */     \
                          (const void *)XIP_AUX_BASE,    /* Read addr */      \
                          (emulROM_length) / 2,          /* Transfer count */ \
                          true /* Start immediately! */                       \
    );                                                                        \
    while (dma_channel_is_busy(dma_chan)) {                                   \
      tight_loop_contents();                                                  \
    }                                                                         \
  } while (0)

#define CHANGE_ENDIANESS_BLOCK16(dest_ptr_word, size_in_bytes) \
  do {                                                         \
    uint16_t *word_ptr = (uint16_t *)(dest_ptr_word);          \
    for (uint16_t j = 0; j < (size_in_bytes) / 2; ++j) {       \
      word_ptr[j] = (word_ptr[j] << 8) | (word_ptr[j] >> 8);   \
    }                                                          \
  } while (0)

#define COPY_AND_CHANGE_ENDIANESS_BLOCK16(src_ptr_word, dest_ptr_word,    \
                                          size_in_bytes)                  \
  do {                                                                    \
    uint16_t *src_word_ptr = (uint16_t *)(src_ptr_word);                  \
    uint16_t *dest_word_ptr = (uint16_t *)(dest_ptr_word);                \
    for (uint16_t j = 0; j < (size_in_bytes) / 2; ++j) {                  \
      dest_word_ptr[j] = (src_word_ptr[j] << 8) | (src_word_ptr[j] >> 8); \
    }                                                                     \
  } while (0)

#define SWAP_LONGWORD(data) \
  ((((uint32_t)data << 16) & 0xFFFF0000) | (((uint32_t)data >> 16) & 0xFFFF))

#define WRITE_AND_SWAP_LONGWORD(address, offset, data)                        \
  do {                                                                        \
    memfunc_write_u32(                                                        \
        (uint32_t)(address), (uint32_t)(offset),                              \
        ((((uint32_t)(data) << 16) & 0xFFFF0000u) |                           \
         (((uint32_t)(data) >> 16) & 0x0000FFFFu)));                          \
  } while (0)

#define WRITE_LONGWORD_RAW(address, offset, data)                             \
  do {                                                                        \
    memfunc_write_u32((uint32_t)(address), (uint32_t)(offset),                \
                      (uint32_t)(data));                                      \
  } while (0)

#define WRITE_BYTE(address, offset, data)                                     \
  do {                                                                        \
    memfunc_write_u8((uint32_t)(address), (uint32_t)(offset),                 \
                     (uint8_t)(data));                                        \
  } while (0)

#define WRITE_WORD(address, offset, data)                                     \
  do {                                                                        \
    memfunc_write_u16((uint32_t)(address), (uint32_t)(offset),                \
                      (uint16_t)(data));                                      \
  } while (0)

#define MEMSET16BIT(memory_address, offset, size, value)              \
  do {                                                                \
    for (int i = 0; i < size; i++) {                                  \
      WRITE_WORD((memory_address), (offset) + i * 2, (uint16_t)(value)); \
    }                                                                 \
  } while (0)

#define READ_BYTE(address, offset) \
  (memfunc_read_u8((uint32_t)(address), (uint32_t)(offset)))

#define READ_WORD(address, offset) \
  (memfunc_read_u16((uint32_t)(address), (uint32_t)(offset)))

#define READ_LONGWORD(address, offset) \
  (memfunc_read_u32((uint32_t)(address), (uint32_t)(offset)))

#define READ_AND_SWAP_LONGWORD(address, offset) \
  (SWAP_LONGWORD(READ_LONGWORD((address), (offset))))

#define COPY_AND_SWAP_16BIT_DMA(dest, source, num_bytes)           \
  do {                                                             \
    size_t _rounded_bytes = ((num_bytes) + 1) & ~1;                \
    size_t _num_words = _rounded_bytes / 2;                        \
    int _dma_channel = dma_claim_unused_channel(true);             \
    dma_channel_config _dma_cfg =                                  \
        dma_channel_get_default_config(_dma_channel);              \
    channel_config_set_transfer_data_size(&_dma_cfg, DMA_SIZE_16); \
    channel_config_set_read_increment(&_dma_cfg, true);            \
    channel_config_set_write_increment(&_dma_cfg, true);           \
    channel_config_set_bswap(&_dma_cfg, true);                     \
    dma_channel_configure(                                         \
        _dma_channel, /* Channel */                                \
        &_dma_cfg,    /* Channel config */                         \
        (dest),       /* Destination address */                    \
        (source),     /* Source address */                         \
        (_num_words), /* Number of 16-bit words to transfer */     \
        false         /* Don't start yet */                        \
    );                                                             \
    dma_channel_start(_dma_channel);                               \
    dma_channel_wait_for_finish_blocking(_dma_channel);            \
    dma_channel_unclaim(_dma_channel);                             \
  } while (0)

/**
 * @brief Macro to set a shared variable.
 *
 * This macro sets a shared variable at the specified index to the given value.
 *
 * @param p_shared_variable_index The index of the shared variable.
 * @param p_shared_variable_value The value to set for the shared variable.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define SET_SHARED_VAR(p_shared_variable_index, p_shared_variable_value,      \
                       memory_shared_address, memory_shared_variables_offset) \
  do {                                                                        \
    DPRINTF("Setting shared variable %d to %x\n", p_shared_variable_index,    \
            p_shared_variable_value);                                         \
    WRITE_WORD((memory_shared_address),                                        \
               (memory_shared_variables_offset) +                              \
                   ((p_shared_variable_index) * 4) + 2,                        \
               (p_shared_variable_value) & 0xFFFFu);                           \
    WRITE_WORD((memory_shared_address),                                        \
               (memory_shared_variables_offset) +                              \
                   ((p_shared_variable_index) * 4),                            \
               (p_shared_variable_value) >> 16);                               \
  } while (0)

/**
 * @brief Macro to get a shared variable.
 *
 * This macro retrieves the 32-bit value of a shared variable at the specified
 * index from the shared memory and assigns it to the provided destination
 * variable.
 *
 * @param p_shared_variable_index The index of the shared variable.
 * @param p_shared_variable_result The variable to store the retrieved value.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define GET_SHARED_VAR(p_shared_variable_index, p_shared_variable_result,     \
                       memory_shared_address, memory_shared_variables_offset) \
  do {                                                                        \
    uint16_t high =                                                           \
        READ_WORD((memory_shared_address),                                     \
                  (memory_shared_variables_offset) +                           \
                      ((p_shared_variable_index) * 4));                        \
    uint16_t low =                                                            \
        READ_WORD((memory_shared_address),                                     \
                  (memory_shared_variables_offset) +                           \
                      ((p_shared_variable_index) * 4) + 2);                    \
    *p_shared_variable_result = ((uint32_t)high << 16) | low;                 \
    DPRINTF("Getting shared variable %d = %x\n", p_shared_variable_index,     \
            p_shared_variable_result);                                        \
  } while (0)

/**
 * @brief Macro to set a private shared variable.
 *
 * This macro sets a private shared variable at the specified index to the given
 * value.
 *
 * @param p_shared_variable_index The index of the private shared variable.
 * @param p_shared_variable_value The value to set for the private shared
 * variable.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define SET_SHARED_PRIVATE_VAR(p_shared_variable_index,                        \
                               p_shared_variable_value, memory_shared_address, \
                               memory_shared_variables_offset)                 \
  do {                                                                         \
    DPRINTF("Setting private shared variable %d to %d\n",                      \
            p_shared_variable_index, p_shared_variable_value);                 \
    DPRINTF("Memory address: %x\n",                                            \
            (memory_shared_address + memory_shared_variables_offset +          \
             (p_shared_variable_index * 4)));                                  \
    WRITE_WORD((memory_shared_address),                                        \
               (memory_shared_variables_offset) +                              \
                   ((p_shared_variable_index) * 4) + 2,                        \
               (p_shared_variable_value) & 0xFFFFu);                           \
    WRITE_WORD((memory_shared_address),                                        \
               (memory_shared_variables_offset) +                              \
                   ((p_shared_variable_index) * 4),                            \
               (p_shared_variable_value) >> 16);                               \
  } while (0)

/**
 * @brief Macro to set a bit of a private shared variable.
 *
 * This macro sets a specific bit of a private shared variable at the specified
 * index.
 *
 * @param p_shared_variable_index The index of the private shared variable.
 * @param bit_position The position of the bit to set.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define SET_SHARED_PRIVATE_VAR_BIT(p_shared_variable_index, bit_position,    \
                                   memory_shared_address,                    \
                                   memory_shared_variables_offset)           \
  do {                                                                       \
    DPRINTF("Setting bit %d of private shared variable %d\n", bit_position,  \
            p_shared_variable_index);                                        \
    uint32_t addr = memory_shared_address + memory_shared_variables_offset + \
                    (p_shared_variable_index * 4);                           \
    uint32_t value = READ_WORD(addr, 2) | ((uint32_t)READ_WORD(addr, 0) << 16); \
    value |= (1 << bit_position);                                            \
    DPRINTF("Memory address: %x, New Value: %x\n", addr, value);             \
    WRITE_WORD(addr, 2, value & 0xFFFFu);                                    \
    WRITE_WORD(addr, 0, value >> 16);                                        \
  } while (0)

/**
 * @brief Macro to clear a bit of a private shared variable.
 *
 * This macro clears a specific bit of a private shared variable at the
 * specified index.
 *
 * @param p_shared_variable_index The index of the private shared variable.
 * @param bit_position The position of the bit to clear.
 * @param memory_shared_address The base address of the shared memory.
 * @param memory_shared_variables_offset The offset of the shared variables
 * within the shared memory.
 */
#define CLEAR_SHARED_PRIVATE_VAR_BIT(p_shared_variable_index, bit_position,  \
                                     memory_shared_address,                  \
                                     memory_shared_variables_offset)         \
  do {                                                                       \
    DPRINTF("Clearing bit %d of private shared variable %d\n", bit_position, \
            p_shared_variable_index);                                        \
    uint32_t addr = memory_shared_address + memory_shared_variables_offset + \
                    (p_shared_variable_index * 4);                           \
    uint32_t value = READ_WORD(addr, 2) | ((uint32_t)READ_WORD(addr, 0) << 16); \
    value &= ~(1 << bit_position);                                           \
    DPRINTF("Memory address: %x, New Value: %x\n", addr, value);             \
    WRITE_WORD(addr, 2, value & 0xFFFFu);                                    \
    WRITE_WORD(addr, 0, value >> 16);                                        \
  } while (0)

#endif  // MEMFUNC_H
