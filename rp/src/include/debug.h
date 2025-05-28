/**
 * File: debug.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header file for basic traces and debug messages
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "pico/stdlib.h"

/**
 * @brief A macro to print debug
 *
 * @param fmt The format string for the debug message, similar to printf.
 * @param ... Variadic arguments corresponding to the format specifiers in the
 * fmt parameter.
 */
#if defined(_DEBUG) && (_DEBUG != 0)
#define DPRINTF(fmt, ...)                                               \
  do {                                                                  \
    const char *file =                                                  \
        strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__; \
    fprintf(stderr, "%s:%d:%s(): " fmt "", file, __LINE__, __func__,    \
            ##__VA_ARGS__);                                             \
  } while (0)
#define DPRINTFRAW(fmt, ...)             \
  do {                                   \
    fprintf(stderr, fmt, ##__VA_ARGS__); \
  } while (0)

// Symbols from the linker script
extern char __StackTop;
extern char __bss_end__;
extern char __data_start__;
extern char __data_end__;
extern char __etext;  // End of text section

static inline void print_memory_usage() {
  // Get stack pointer
  register void *sp asm("sp");

  // Get current heap end via a dummy malloc (and immediately free)
  void *heap_cur = malloc(1);
  free(heap_cur);

  size_t heap_used = (size_t)((char *)heap_cur - &__bss_end__);
  size_t stack_used = (size_t)((char *)&__StackTop - (char *)sp);

  // Sections
  size_t data_size = (size_t)(&__data_end__ - &__data_start__);
  size_t text_size = (size_t)((char *)&__etext -
                              (char *)0x10000000);  // Text in FLASH from origin

  // RAM size used (you're using 128 KB of RAM, from the linker script)
  const size_t total_sram = 128 * 1024;

  // FLASH size used (from FLASH origin to __etext)
  const size_t flash_used = text_size;
  const size_t total_flash = 1024 * 1024;

  size_t total_used = heap_used + stack_used + data_size;
  size_t total_free = (total_sram > total_used) ? total_sram - total_used : 0;

  DPRINTF("=== Memory Usage ===\n");
  DPRINTF(".text (FLASH)   : %u bytes (%.2f%% of 1MB)\n",
          (unsigned int)text_size, (100.0f * text_size) / total_flash);
  DPRINTF(".data (RAM)     : %u bytes (%.2f%% of 128KB)\n",
          (unsigned int)data_size, (100.0f * data_size) / total_sram);
  DPRINTF("Heap used       : %u bytes (%.2f%% of 128KB)\n",
          (unsigned int)heap_used, (100.0f * heap_used) / total_sram);
  DPRINTF("Stack used      : %u bytes (%.2f%% of 128KB)\n",
          (unsigned int)stack_used, (100.0f * stack_used) / total_sram);
  DPRINTF("Total RAM used  : %u bytes (%.2f%%)\n",
          (unsigned int)(total_used + data_size),
          (100.0f * (total_used + data_size)) / total_sram);
  DPRINTF("Total RAM free  : %u bytes (%.2f%%)\n", (unsigned int)total_free,
          (100.0f * total_free) / total_sram);
  DPRINTF("====================\n");
}

#else
#define DPRINTF(fmt, ...)
#define DPRINTFRAW(fmt, ...)
static inline void print_memory_usage() {}
#endif

typedef void (*IRQInterceptionCallback)();

#endif  // DEBUG_H
