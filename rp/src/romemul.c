/**
 * File: romemul.c
 * Author: Diego Parrilla Santamaría
 * Date: July 2023-February 2025
 * Copyright: 2023-25 - GOODDATA LABS SL
 * Description: C file that contains the main function of the ROM emulator.
 */

#include "romemul.h"

// Global variables to access them in the IRQ handlers
static int readAddrRomDmaChannel = -1;
static int lookupDataRomDmaChannel = -1;

// Default PIO to use
static PIO defaultPio = pio0;

// Interrupt handler for DMA completion
// We don't use at runtime, but they are useful for debugging
// Keep in mind that printing in an interrupt handler is not a good idea
// because it can cause delays in the processing of the data
void __not_in_flash_func(dma_irqHandlerLookup)(void) {
  // Read the address to process
  uint16_t addrLsb = dma_hw->ch[lookupDataRomDmaChannel].al3_read_addr_trig;

  dma_hw->ints1 = 1U << lookupDataRomDmaChannel;

  DPRINTF("DMA_LSB LOOKUP: $%x\n", addrLsb);
}

void __not_in_flash_func(dma_irqHandlerAddress)(void) {
  uint32_t addr = dma_hw->ch[readAddrRomDmaChannel].al3_read_addr_trig;
  uint16_t value = *((uint16_t *)addr);

  // Clear the interrupt request for the channel
  dma_hw->ints0 = 1U << readAddrRomDmaChannel;

  DPRINTF("DMA ADDR: $%x, VALUE: $%x\n", addr, value);
}

static int initMonitorRom4(PIO pio) {
  // Configure the monitor ROM4 state machine
  // Add the assembled program to the PIO into the memory where there are enough
  // space
  uint offsetMonitorROM4 = pio_add_program(pio, &monitor_rom4_program);

  // Claim a free state machine from the PIO read program
  uint smMonitorROM4 = pio_claim_unused_sm(pio, true);

  // Start the state machine, executing the PIO read program
  monitor_rom4_program_init(pio, smMonitorROM4, offsetMonitorROM4,
                            SAMPLE_DIV_FREQ);

  // Enable the state machine
  pio_sm_set_enabled(pio, smMonitorROM4, true);

  DPRINTF("ROM4 signal monitor initialized.\n");
  return smMonitorROM4;
}

static int initMonitorRom3(PIO pio) {
  // Configure the monitor ROM3 state machine
  // Add the assembled program to the PIO into the memory where there are enough
  // space
  uint offsetMonitorROM3 = pio_add_program(pio, &monitor_rom3_program);

  // Claim a free state machine from the PIO read program
  uint smMonitorROM3 = pio_claim_unused_sm(pio, true);

  // Start the state machine, executing the PIO read program
  // monitor rom3 and rom4 share the same init function
  monitor_rom4_program_init(pio, smMonitorROM3, offsetMonitorROM3,
                            SAMPLE_DIV_FREQ);

  // Enable the state machine
  pio_sm_set_enabled(pio, smMonitorROM3, true);

  DPRINTF("ROM3 signal monitor initialized.\n");
  return smMonitorROM3;
}

static int initRomEmulator(PIO pio, IRQInterceptionCallback requestCallback,
                           IRQInterceptionCallback responseCallback) {
  // Configure DMAs
  // Claim the first available DMA channel for read_addr_rom_dma_channel
  readAddrRomDmaChannel = dma_claim_unused_channel(true);
  DPRINTF("DMA channel for read_addr_rom_dma_channel: %d\n",
          readAddrRomDmaChannel);
  if (readAddrRomDmaChannel == -1) {
    // Handle the error, perhaps by halting the program or logging an error
    // message
    DPRINTF("Failed to claim a DMA channel for read_addr_rom_dma_channel.\n");
    dma_channel_unclaim(readAddrRomDmaChannel);
    return -1;
  }

  // Claim another available DMA channel for lookup_data_rom_dma_channel
  lookupDataRomDmaChannel = dma_claim_unused_channel(true);
  DPRINTF("DMA channel for lookup_data_rom_dma_channel: %d\n",
          lookupDataRomDmaChannel);
  if (lookupDataRomDmaChannel == -1) {
    // Handle the error
    DPRINTF("Failed to claim a DMA channel for lookup_data_rom_dma_channel.\n");
    // Optionally release the previously claimed channel if you want to clean up
    dma_channel_unclaim(lookupDataRomDmaChannel);
    return -1;
  }

  // Now, read_addr_rom_dma_channel and lookup_data_rom_dma_channel hold the
  // channel numbers for your tasks, and you can use them throughout your code.

  // Configure the read PIO state machine
  // Add the assembled program to the PIO into the memory where there are enough
  // space
  uint offsetReadROM = pio_add_program(pio, &romemul_read_program);

  // Claim a free state machine from the PIO read program
  uint smReadROM = pio_claim_unused_sm(pio, true);

  // Start the state machine, executing the PIO read program
  romemul_read_program_init(pio, smReadROM, offsetReadROM, READ_ADDR_GPIO_BASE,
                            READ_ADDR_PIN_COUNT, READ_SIGNAL_GPIO_BASE,
                            SAMPLE_DIV_FREQ);

  // Need to clear _input shift counter_, as well as FIFO, because there may be
  // partial ISR contents left over from a previous run. sm_restart does this.
  pio_sm_clear_fifos(pio, smReadROM);
  pio_sm_restart(pio, smReadROM);
  pio_sm_set_enabled(pio, smReadROM, true);

  // DMA configuration
  // Lookup data DMA: the address of the data to read from the ROM is injected
  // from the chained previous DMA channel (read_addr_rom_dma_channel) into the
  // read address trigger register. Then push the 16 bit result of the lookup
  // into the FIFO
  dma_channel_config cdmaLookup =
      dma_channel_get_default_config(lookupDataRomDmaChannel);
  channel_config_set_transfer_data_size(&cdmaLookup, DMA_SIZE_16);
  channel_config_set_read_increment(&cdmaLookup, false);
  channel_config_set_write_increment(&cdmaLookup, false);
  channel_config_set_dreq(&cdmaLookup, pio_get_dreq(pio, smReadROM, true));
  channel_config_set_chain_to(&cdmaLookup, readAddrRomDmaChannel);
  dma_channel_configure(lookupDataRomDmaChannel, &cdmaLookup,
                        &pio->txf[smReadROM], NULL, 1, false);

  // Read address DMA: the address to read from the ROM is obtained from the
  // FIFO and injected into the read address trigger register of the lookup data
  // DMA channel chained.
  dma_channel_config cdma =
      dma_channel_get_default_config(readAddrRomDmaChannel);
  channel_config_set_transfer_data_size(&cdma, DMA_SIZE_32);
  channel_config_set_read_increment(&cdma, false);
  channel_config_set_write_increment(&cdma, false);
  channel_config_set_dreq(&cdma, pio_get_dreq(pio, smReadROM, false));
  dma_channel_configure(readAddrRomDmaChannel, &cdma,
                        &dma_hw->ch[lookupDataRomDmaChannel].al3_read_addr_trig,
                        &pio->rxf[smReadROM], 1, true);

  // If there is a requestCallback function, then enable the DMA IRQ and set the
  // callback Otherwise, simply don't enable the DMA IRQ Use the DMA_IRQ_1 for
  // the read_addr_rom_dma_channel
  if (requestCallback != NULL) {
    DPRINTF("Enabling DMA IRQ for read_addr_rom_dma_channel.\n");
    dma_channel_set_irq1_enabled(readAddrRomDmaChannel, true);
    irq_set_exclusive_handler(DMA_IRQ_1, requestCallback);
    irq_set_enabled(DMA_IRQ_1, true);
  }
  // If there is a responseCallback function, then enable the DMA IRQ and set
  // the callback Otherwise, simply don't enable the DMA IRQ Use the DMA_IRQ_1
  // for the lookup_data_rom_dma_channel
  if (responseCallback != NULL) {
    DPRINTF("Enabling DMA IRQ for lookup_data_rom_dma_channel.\n");
    dma_channel_set_irq1_enabled(lookupDataRomDmaChannel, true);
    irq_set_exclusive_handler(DMA_IRQ_1, responseCallback);
    irq_set_enabled(DMA_IRQ_1, true);
  }

  DPRINTF("ROM emulator initialized.\n");
  return smReadROM;
}
void dma_setResponseCB(IRQInterceptionCallback responseCallback) {
  // Change the the response callback function
  if (responseCallback != NULL) {
    DPRINTF(
        "Changing DMA callback function for lookup_data_rom_dma_channel.\n");
    // Disable DMA IRQ before modifying
    dma_channel_set_irq1_enabled(lookupDataRomDmaChannel, false);
    irq_set_enabled(DMA_IRQ_1, false);

    // Remove any existing handler first
    irq_remove_handler(DMA_IRQ_1, irq_get_exclusive_handler(DMA_IRQ_1));

    // Now safely set the new one
    irq_set_exclusive_handler(DMA_IRQ_1, responseCallback);

    // Re-enable
    dma_channel_set_irq1_enabled(lookupDataRomDmaChannel, true);
    irq_set_enabled(DMA_IRQ_1, true);
    DPRINTF("DMA callback function changed.\n");
  }
}

int init_romemul(IRQInterceptionCallback requestCallback,
                 IRQInterceptionCallback responseCallback,
                 bool copyFlashToRAM) {
  // Grant high bus priority to the DMA, so it can shove the processors out
  // of the way. This should only be needed if you are pushing things up to
  // >16bits/clk here, i.e. if you need to saturate the bus completely.
  bus_ctrl_hw->priority =
      BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

  // Copy the content of the FLASH to RAM before initializing the emulator code
  // If not initialized, assume somebody else will copy "something" to RAM
  // eventually...
  if (copyFlashToRAM) {
    const uint16_t *srcAddr =
        (const uint16_t *)(XIP_BASE + FLASH_ROM_LOAD_OFFSET);
    COPY_FIRMWARE_TO_RAM(srcAddr, ROM_SIZE_WORDS * ROM_BANKS);
  }

  int smMonitorROM4 = initMonitorRom4(defaultPio);
  if (smMonitorROM4 < 0) {
    DPRINTF("Error initializing ROM4 monitor. Error code: %d\n", smMonitorROM4);
    return -1;
  }

  int smMonitorROM3 = initMonitorRom3(defaultPio);
  if (smMonitorROM3 < 0) {
    DPRINTF("Error initializing ROM3 monitor. Error code: %d\n", smMonitorROM3);
    return -1;
  }

  int smReadROM =
      initRomEmulator(defaultPio, requestCallback, responseCallback);
  if (smReadROM < 0) {
    DPRINTF("Error initializing ROM emulator. Error code: %d\n", smReadROM);
    return -1;
  }

  // Push to the FIFO the Most Significant word of the addresses to read from
  // the ROM in the lower 17 bits of the 32 bits of the FIFO register. Only need
  // 16 bits from the rp2040 memory address, so shift right 17 bits to get the
  // 16 bits In the PIO program, the address is shifted left 1 bit to make room
  // for the ROM4 signal and the 16 bits of the address from the GPIO input. So
  // the address is created as follows: bits 31-17: MSB of the address from the
  // rp2040 memory. In our case 0x20020000 bit 16: ROM4 signal. Since is an
  // inverted signal, we set it to 0 for ROM4 and 1 if not ROM4 (ROM3) bits
  // 15-0: 16 bits of the address from the GPIO input The RAM memory address of
  // the rp2040 and the FLASH memory used are defined in the file memmap_rp.ld
  // Please do not modify these values, because they are carefully selected to
  // avoid conflicts and be performant.

  pio_sm_put_blocking(
      defaultPio, smReadROM,
      ((unsigned long int)&__rom_in_ram_start__ >> ROMEMUL_BUS_BITS));

  // Setting the signals after configuring the PIO makes the ROM emulator to not
  // put inconsistent data in the address or data bus at any time, avoiding
  // glitches.

  // Configure the output pins for the READ and WRITE signals.
  pio_gpio_init(defaultPio, READ_SIGNAL_GPIO_BASE);
  gpio_set_dir(READ_SIGNAL_GPIO_BASE, GPIO_OUT);
  gpio_set_pulls(READ_SIGNAL_GPIO_BASE, true, false);  // Pull up (true, false)
  gpio_put(READ_SIGNAL_GPIO_BASE, 1);

  pio_gpio_init(defaultPio, WRITE_SIGNAL_GPIO_BASE);
  gpio_set_dir(WRITE_SIGNAL_GPIO_BASE, GPIO_OUT);
  gpio_set_pulls(WRITE_SIGNAL_GPIO_BASE, true, false);  // Pull up (true, false)
  gpio_put(WRITE_SIGNAL_GPIO_BASE, 1);

  // Configure the input pins for ROM4
  pio_gpio_init(defaultPio, ROM4_GPIO);
  gpio_set_dir(ROM4_GPIO, GPIO_IN);
  gpio_set_pulls(ROM4_GPIO, true, false);  // Pull up (true, false)
  gpio_pull_up(ROM4_GPIO);

  // Configure the input pins for ROM3
  pio_gpio_init(defaultPio, ROM3_GPIO);
  gpio_set_dir(ROM3_GPIO, GPIO_IN);
  gpio_set_pulls(ROM3_GPIO, false, true);  // Pull up (true, false)
  gpio_pull_up(ROM3_GPIO);

  // Configure the output pins for the output data bus
  for (int i = 0; i < WRITE_DATA_PIN_COUNT; i++) {
    pio_gpio_init(defaultPio, WRITE_DATA_GPIO_BASE + i);
    gpio_set_dir(WRITE_DATA_GPIO_BASE + i, GPIO_OUT);
    gpio_set_pulls(WRITE_DATA_GPIO_BASE + i, false,
                   true);  // Pull down (false, true)
    gpio_put(WRITE_DATA_GPIO_BASE + i, 0);
  }
}
