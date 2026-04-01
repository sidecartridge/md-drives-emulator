#include "include/constants.h"
#include "sd_timeouts.h"

#define SD_TIMEOUT_SDIO_COMMAND_R1_MS 10
#define SD_TIMEOUT_SDIO_COMMAND_R2_MS 2
#define SD_TIMEOUT_SDIO_COMMAND_R3_MS 2
#define SD_TIMEOUT_SDIO_RX_POLL_MS 1000
#define SD_TIMEOUT_SDIO_TX_POLL_MS 5000
#define SD_TIMEOUT_SDIO_BEGIN_MS 1000
#define SD_TIMEOUT_SDIO_STOP_TRANSMISSION_MS 200

sd_timeouts_t sd_timeouts = {
    .sd_command = SD_TIMEOUT_COMMAND_MS,
    .sd_command_retries = SD_TIMEOUT_COMMAND_RETRIES,
    .sd_lock = SD_TIMEOUT_SD_LOCK_MS,
    .sd_spi_read = SD_TIMEOUT_SPI_READ_MS,
    .sd_spi_write = SD_TIMEOUT_SPI_WRITE_MS,
    .sd_spi_write_read = SD_TIMEOUT_SPI_WRITE_READ_MS,
    .spi_lock = SD_TIMEOUT_SPI_LOCK_MS,
    .rp2040_sdio_command_R1 = SD_TIMEOUT_SDIO_COMMAND_R1_MS,
    .rp2040_sdio_command_R2 = SD_TIMEOUT_SDIO_COMMAND_R2_MS,
    .rp2040_sdio_command_R3 = SD_TIMEOUT_SDIO_COMMAND_R3_MS,
    .rp2040_sdio_rx_poll = SD_TIMEOUT_SDIO_RX_POLL_MS,
    .rp2040_sdio_tx_poll = SD_TIMEOUT_SDIO_TX_POLL_MS,
    .sd_sdio_begin = SD_TIMEOUT_SDIO_BEGIN_MS,
    .sd_sdio_stopTransmission = SD_TIMEOUT_SDIO_STOP_TRANSMISSION_MS,
};
