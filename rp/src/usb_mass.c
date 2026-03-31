/**
 * File: usb_mass.c
 * Author: Diego Parrilla Santamaría
 * Date: June 2024
 * Copyright: 2024 - GOODDATA LABS SL
 * Description: USB Mass storage device for the SD card
 */

#include "include/usb_mass.h"

static DWORD sz_drv;
static DWORD sz_sect = 0;
static BYTE msc_sector_buf[FF_MAX_SS];

static bool ejected = false;
static bool mounted = false;

static void usb_mass_activity_begin(void) {
#ifdef BLINK_H
  blink_off();
#endif
}

static void usb_mass_activity_end(void) {
#ifdef BLINK_H
  blink_on();
#endif
}

static bool usb_mass_range_valid(uint32_t lba, uint32_t offset,
                                 uint32_t bufsize) {
  if (sz_sect == 0) return false;

  uint64_t start_lba = (uint64_t)lba + (offset / sz_sect);
  uint64_t sectors_touched =
      ((uint64_t)(offset % sz_sect) + bufsize + sz_sect - 1) / sz_sect;

  return sectors_touched == 0 || (start_lba + sectors_touched) <= sz_drv;
}

static int32_t usb_mass_read_chunked(uint8_t lun, uint32_t lba, uint32_t offset,
                                     void *buffer, uint32_t bufsize) {
  if (bufsize == 0) return 0;
  if (!usb_mass_range_valid(lba, offset, bufsize)) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
    return -1;
  }

  usb_mass_activity_begin();

  uint32_t current_lba = lba + (offset / sz_sect);
  uint32_t sector_offset = offset % sz_sect;
  uint32_t remaining = bufsize;
  uint8_t *dst = (uint8_t *)buffer;
  DRESULT res;

  if (sector_offset != 0) {
    uint32_t chunk =
        remaining < (sz_sect - sector_offset) ? remaining : (sz_sect - sector_offset);
    res = disk_read(0, msc_sector_buf, current_lba, 1);
    if (res != RES_OK) {
      tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
      usb_mass_activity_end();
      return -1;
    }

    memcpy(dst, msc_sector_buf + sector_offset, chunk);
    dst += chunk;
    remaining -= chunk;
    current_lba++;
  }

  if (remaining >= sz_sect) {
    uint32_t full_sectors = remaining / sz_sect;
    res = disk_read(0, dst, current_lba, full_sectors);
    if (res != RES_OK) {
      tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
      usb_mass_activity_end();
      return -1;
    }

    uint32_t bytes_read = full_sectors * sz_sect;
    dst += bytes_read;
    remaining -= bytes_read;
    current_lba += full_sectors;
  }

  if (remaining > 0) {
    res = disk_read(0, msc_sector_buf, current_lba, 1);
    if (res != RES_OK) {
      tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
      usb_mass_activity_end();
      return -1;
    }

    memcpy(dst, msc_sector_buf, remaining);
  }

  usb_mass_activity_end();
  return (int32_t)bufsize;
}

static int32_t usb_mass_write_chunked(uint8_t lun, uint32_t lba,
                                      uint32_t offset, uint8_t *buffer,
                                      uint32_t bufsize) {
  if (bufsize == 0) return 0;
  if (!usb_mass_range_valid(lba, offset, bufsize)) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
    return -1;
  }

  usb_mass_activity_begin();

  uint32_t current_lba = lba + (offset / sz_sect);
  uint32_t sector_offset = offset % sz_sect;
  uint32_t remaining = bufsize;
  uint8_t *src = buffer;
  DRESULT res;

  if (sector_offset != 0) {
    uint32_t chunk =
        remaining < (sz_sect - sector_offset) ? remaining : (sz_sect - sector_offset);
    res = disk_read(0, msc_sector_buf, current_lba, 1);
    if (res != RES_OK) {
      tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x00);
      usb_mass_activity_end();
      return -1;
    }

    memcpy(msc_sector_buf + sector_offset, src, chunk);
    res = disk_write(0, msc_sector_buf, current_lba, 1);
    if (res != RES_OK) {
      tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x00);
      usb_mass_activity_end();
      return -1;
    }

    src += chunk;
    remaining -= chunk;
    current_lba++;
  }

  if (remaining >= sz_sect) {
    uint32_t full_sectors = remaining / sz_sect;
    res = disk_write(0, src, current_lba, full_sectors);
    if (res != RES_OK) {
      tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x00);
      usb_mass_activity_end();
      return -1;
    }

    uint32_t bytes_written = full_sectors * sz_sect;
    src += bytes_written;
    remaining -= bytes_written;
    current_lba += full_sectors;
  }

  if (remaining > 0) {
    res = disk_read(0, msc_sector_buf, current_lba, 1);
    if (res != RES_OK) {
      tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x00);
      usb_mass_activity_end();
      return -1;
    }

    memcpy(msc_sector_buf, src, remaining);
    res = disk_write(0, msc_sector_buf, current_lba, 1);
    if (res != RES_OK) {
      tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0C, 0x00);
      usb_mass_activity_end();
      return -1;
    }
  }

  usb_mass_activity_end();
  return (int32_t)bufsize;
}

bool usb_mass_get_mounted(void) { return mounted; }

bool usb_mass_init() {
  DPRINTF("CFG_TUD_MAX_SPEED: %d\n", CFG_TUD_MAX_SPEED);
  return usb_mass_start();
}

bool usb_mass_start(void) {
  // Init USB
  DPRINTF("Init USB\n");
  ejected = false;
  mounted = false;
  // init device stack on configured roothub port
  bool ok = tud_init(BOARD_TUD_RHPORT);
  if (!ok) {
    DPRINTF("ERROR: TinyUSB init failed\n");
    return false;
  }

  // Turn on the LED
#ifdef BLINK_H
  blink_on();
#endif

  return ok;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {
  DPRINTF("Device mounted\n");
  mounted = true;
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
  DPRINTF("Device unmounted\n");
  mounted = false;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  DPRINTF("Device suspended\n");
  mounted = false;
  //  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
  DPRINTF("Device resumed\n");
  mounted = true;
  //  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16,
// 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4]) {
  (void)lun;

  const char vid[] = "SidecarT";
  const char pid[] = "MultideviceMass";
  const char rev[] = RELEASE_VERSION;

  memcpy(vendor_id, vid, strlen(vid));
  memcpy(product_id, pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));

  DPRINTF("Inquiry\n");
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  (void)lun;

  // RAM disk is ready until ejected
  if (ejected) {
    // Additional Sense 3A-00 is NOT_FOUND
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
    return false;
  }

  return true;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and
// SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size Application update
// block count and block size
void __not_in_flash_func(tud_msc_capacity_cb)(uint8_t lun,
                                              uint32_t *block_count,
                                              uint16_t *block_size) {
  (void)lun;

  DPRINTF("Capacity\n");
  BYTE pdrv = 0;
  DRESULT dr = disk_ioctl(pdrv, GET_SECTOR_COUNT, &sz_drv);
  if (dr != RES_OK) {
    DPRINTF("disk_ioctl GET_SECTOR_COUNT failed: %d\n", dr);
    return;
  }
  DPRINTF("Sector count: %lu\n", sz_drv);
#if FF_MAX_SS != FF_MIN_SS
  dr = disk_ioctl(pdrv, GET_SECTOR_SIZE, (UINT)&sz_sect);
  if (dr != RES_OK) {
    DPRINTF("disk_ioctl GET_SECTOR_SIZE failed: %d\n", dr);
    return;
  }
#else
  sz_sect = FF_MAX_SS;
#endif
  DPRINTF("Sector size: %u\n", sz_sect);

  *block_count = sz_drv;
  *block_size = sz_sect;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool __not_in_flash_func(tud_msc_start_stop_cb)(uint8_t lun,
                                                uint8_t power_condition,
                                                bool start, bool load_eject) {
  (void)lun;
  (void)power_condition;

  DPRINTF("Start/Stop Unit\n");

  if (load_eject) {
    if (start) {
      // load disk storage
      DPRINTF("LOAD DISK STORAGE\n");
    } else {
      // unload disk storage
      DPRINTF("UNLOAD DISK STORAGE\n");
      ejected = true;
    }
  }

  return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize) {
  return usb_mass_read_chunked(lun, lba, offset, buffer, bufsize);
}

bool tud_msc_is_writable_cb(uint8_t lun) {
  (void)lun;
  return !USBDRIVE_READ_ONLY;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize) {
  return usb_mass_write_chunked(lun, lba, offset, buffer, bufsize);
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer,
                        uint16_t bufsize) {
  // read10 & write10 has their own callback and MUST not be handled here

  DPRINTF("SCSI Cmd %02X\n", scsi_cmd[0]);

  void const *response = NULL;
  int32_t resplen = 0;

  // most scsi handled is input
  bool in_xfer = true;

  switch (scsi_cmd[0]) {
    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
      // Host is about to read/write etc ... better not to disconnect disk
      resplen = 0;
      break;
    default:
      // Set Sense = Invalid Command Operation
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

      // negative means error -> tinyusb could stall and/or response with failed
      // status
      resplen = -1;
      break;
  }

  // return resplen must not larger than bufsize
  if (resplen > bufsize) resplen = bufsize;

  if (response && (resplen > 0)) {
    if (in_xfer) {
      memcpy(buffer, response, (size_t)resplen);
    } else {
      // SCSI output
    }
  }

  return (int32_t)resplen;
}
