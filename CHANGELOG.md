# Changelog

## v1.1.0 (2026-03-30)
This release focuses on RP2040 memory layout cleanup, splitting ROM emulation from command capture, removing obsolete DMA-era plumbing, and tightening several hot paths and board-support subsystems. It also adds experimental **ACSI hard disk emulation** at the BIOS level and a companion `scripts/atari-hd/atari_hd.py` image creator.

### Changes
- **Single RP linker script**: The RP build now uses a single linker script, simplifying the memory layout and build configuration.
- **192 KB RAM and 64 KB ROM area**: The RP memory map now reserves a smaller ROM-in-RAM window and frees more RAM for runtime features.
- **ROM4-only ROM emulation path**: `romemul` has been simplified to focus only on ROM4, with less legacy branching and lighter code.
- **ROM3 communication path**: A separate `commemul` path now captures command traffic from ROM3 and feeds the software protocol parser.
- **Simpler command plumbing**: Old DMA handlers and semaphore-heavy command code were removed or simplified now that command capture no longer depends on the legacy DMA path.
- **Performance tuning**: Several hot paths were optimized, including fast safe protocol message copies, lighter ROM emulator code, and general performance cleanup in the command path.
- **GPIO tuning**: GPIO drive strength was adjusted to improve signal stability on the cartridge bus.
- **SDK pin update**: The RP build scripts and pinned submodules were updated to Pico SDK / Pico Extras `2.2.0`.
- **On-demand RTC networking**: WiFi STA bring-up for RTC/NTP no longer happens on every boot. The network stack now starts only when the RTC flow is about to fetch NTP time and is deinitialized immediately afterward.
- **Batched ACSI reads/writes**: `CMD_READ_SECTOR_BATCH` / `CMD_WRITE_SECTOR_BATCH` pack multiple sectors per RP round-trip. Idempotent chunk-write protocol carries an in-batch offset so retries after transient timeouts don't corrupt the image.
- **Write-behind image flush**: ACSI and floppy write paths now defer `f_sync` via `acsi_tick()` / `floppy_tick()` after `DRIVES_FLUSH_INTERVAL_MS` (3 s) of inactivity instead of flushing per sector.
- **Fastseek on ACSI image context**: random-access reads skip the FAT walk via FatFS cluster linkmaps.
- **BCB buffer rebind**: on ACSI-enabled boots the Atari side reassigns TOS's BCB `BUFR` pointers to a 34 KB pool carved from `_membot` (via a CA_INIT bit-26 cart header), so ≥4 KB logical sectors don't overflow TOS's default 1 KB buffers. A warm reset releases the pool when ACSI is turned off.
- **Separate ACSI ID and drive letter**: the physical ACSI bus ID and the starting drive-letter slot are now configured independently. Config key renamed `ACSI_FIRST_UNIT` → `ACSI_ID`; default ID is now 7.
- **FatFS tuning**: `ffconf.h` `FF_VOLUMES` typo fix; `FF_FS_LOCK` lowered from 16 to 8 to free heap.
- **RTC boot feedback**: The setup exit flow now shows WiFi/NTP progress on screen, including connection attempts, failure reasons, and the assigned IP address when available.
- **Lean lwIP profile**: `lwipopts.h` was trimmed for the current RTC use case, focusing on DHCP, DNS, and UDP/NTP instead of carrying the previous broader TCP-oriented profile.
- **USB Mass Storage cleanup**: The USB device now exposes only the MSC interface, dropping the unused CDC composite path and simplifying enumeration.
- **Higher DMA priority**: DMA access priority was adjusted to favor memory access more aggressively.

### New features
- **Dedicated ROM3 command reader**: Command input can now be captured through a dedicated ROM3 communication path while ROM emulation remains on ROM4.
- **Upgraded network stack**: `network.c` / `network.h` were upgraded from `md-browser`, bringing a safer STA path, cleaner DNS handling, optional mDNS hooks, and extra WiFi helper APIs.
- **Improved SELECT button handling**: The SELECT button logic was updated with the newer debounce and long-press behavior used in the booster bootloader.
- **Floppy A multi-image cycling**: Drive A can now persist up to 10 configured images and cycle to the next configured slot with a short SELECT press during runtime.
- **Multi-image slot setup menu**: The setup UI now includes a dedicated `CTRL+A` menu to configure extra floppy A slots, browse images for slots `2..10`, and clear a slot directly with `SHIFT + 2..9` or `0`.
- **Slot index LED feedback**: When cycling floppy A images, the Pico W LED now flashes the active slot number so the selected image index is visible without opening the setup menu.
- **ACSI block-level hard disk emulation (EXPERIMENTAL)**: hooks the BIOS hdv vectors and announces a real partition table with up to 14 FAT16 partitions (`C:`..`P:`) from a raw image on the microSD. Coexists with a real ACSI drive under PPDRIVER / HDDRIVER. Tested on TOS 1.04–2.06; not supported on EmuTOS.
- **Dual-BPB image support**: detects PPDRIVER and HDDRIVER TOS&DOS hybrid layouts and labels them on the boot summary (`TOS&DOS PP` / `TOS&DOS HD` / `Atari/FAT16`, plus `[TOS view]` / `[DOS view]`).
- **AHDI fallback partition parsing**: when no MBR signature is present the driver parses the Atari AHDI table at sector 0 and walks XGM extended chains, so native ICD / HDDRIVER AHDI-only images enumerate correctly.
- **Larger logical sector sizes**: FAT16 parser accepts `bytesPerSec ∈ {512, 1024, 2048, 4096, 8192}`.
- **ACSI boot-info summary**: when ACSI is enabled the setup exit flow prints image path, ACSI ID, disk type, partition count, and a per-partition row before handing off to TOS.
- **Setup-menu ACSI section**: new `[C]` toggle, `[I]mage` picker, `U[n]it (ACSI ID)` (0..7, default 7), and `Dri[v]e` fields. ACSI ID and drive letter are independent, with conflict detection against the GEMDRIVE drive letter.
- **SD-card speed indicator**: hidden by default; toggle with `[Z]` on the setup screen.
- **Atari HD image creator**: new `scripts/atari-hd/atari_hd.py` — Python 3.10 stdlib-only CLI that builds AHDI / PPDRIVER / HDDRIVER images with up to 14 partitions (primary + MBR/XGM extended chain), Hatari-style auto sector sizing, auto-alignment of partition size when the TOS companion's `spfat` divisibility would break, and an interactive TOS <1.04 compatibility mode. Shells out to `mkfs.vfat` / `mkdosfs`.

### Fixes
- **Memory allocation fixes**: Multiple allocation-related bugs were fixed.
- **Forward-slash path handling**: Forward-slash normalization was re-enabled where needed.
- **Cleanup of unused DMA code**: Old DMA handlers and related dead code that were no longer required have been removed.
- **USB Mass Storage transfers**: USB MSC read/write callbacks now handle chunked host transfers correctly, including multi-sector requests and partial-sector accesses.
- **USB initialization checks**: The setup path now treats USB initialization as a real success/failure path instead of assuming TinyUSB always starts correctly.
- **Card enumeration bug**: A small SD-card enumeration bug in the hardware configuration helpers was fixed.
- **Stability fixes**: ROM bus handling, command ingestion, and SELECT button debouncing were cleaned up for better runtime stability.
- **Floppy slot-swap media-change handling**: Drive A slot swaps now raise media change and clear it only after the first successful read of the new disk's root-directory start sector, which keeps TOS geometry refresh stable across image changes.
- **Submenu modifier handling**: Single-key submenu reentry now preserves modifier information so `CTRL` and `SHIFT` actions work correctly in the multi-image floppy setup flow.
- **Floppy activity LED timeout**: The Pico W LED no longer gets stuck on after floppy or command activity; it now emits a short activity pulse and turns off reliably when access finishes.
- **Faster boot when RTC is disabled**: The emulator now skips the blocking WiFi connection path entirely unless RTC/NTP actually needs it.
- **General dead-code cleanup**: Several unused commands, stale helpers, and other dead code paths were removed.
- **ACSI FAT16 / write-path fixes**: Correct `bflags = 1` in the ACSI BPB (was causing TOS to interpret the FAT as FAT12 and garble large files); `$DEAD0000` disabled-sentinel so the BCB pool hook can distinguish "explicitly off" from "cold boot uninitialized"; `acsi.s` closing `even / nop × 8 / acsi_end:` tail block to prevent `firmware.py`'s trailing-zero strip from turning over-reads into 4-bomb bus errors on writes; `moveq`/`move.w` high-word fixes in the write loop.
- **GEMDRIVE fixes**: `Dsetdrv` / `Dsetpath` / `_bootdev` are only set when GEMDRIVE is mapped to `C:`, so non-C: drives no longer auto-run `AUTO/` programs; `f_closedir` before `free` on FSFIRST defensive cleanup; SEEK_END positive offsets clamp to file size; `ext.l d0` removed from `Fread_core`; per-file `COMMAND_TIMEOUT` raised to `$6FFF`+ in `gemdrive.s` (avoids fd leaks from retransmits during slow directory scans).
- **Floppy write-behind retry cap**: `floppy_tick()` now caps flush retries at 5 and raises `FLOPPY_DISK_ERROR` instead of spinning forever; media-change arming skips malformed images with `rootSector = 0`.

### Removed
- **Format Image**: The old setup-menu floppy image formatter was removed. Image formatting is now expected to be done through the File Manager microfirmware.
- **Convert MSA to ST**: The old setup-menu MSA converter was removed. Image conversion is now expected to be done through the File Manager microfirmware.

---

## v1.0.6beta (2025-10-04) - Beta release
This is the first beta release. It includes all the new features and improvements, and it should not include any more new features. The code is still in development and may contain bugs, but it is more stable than previous alpha releases and ready to use for all users.

### Changes
- **Setup menu not in ROM**: The setup menu code has been moved out of the ROM and into the main RAM. This change allows for the USB mass storage feature to be used safely during the setup process.
- **USB mass storage only during setup menu**: The USB mass storage feature is now only active during the setup menu. This change prevents potential conflicts and improves system stability.
- **Forbid start emulation if USB mass storage is mounted**: Emulation cannot be started if the USB mass storage is mounted. This change prevents potential data corruption and ensures a stable emulation environment. Now, the `E'xit command to start the emulation disappears if the USB mass storage is mounted. It is re-enabled when the USB mass storage is unmounted.

### New features
No new features have been added in this release.

### Fixes
- **AUTO folder execution**: The system now can execute the full list of files in the AUTO folder, not just the first one. This change improves the user experience by allowing multiple files to be executed automatically during startup. AUTO folder support has been tested from TOS 1.04, 1.06, 1.62, 2.06 and EmuTOS. TOS 1.00 and TOS 1.02 can crash when accessing the AUTO folder due to a bug in these TOS versions.
- **Memory leak fixes**: Several memory leaks have been identified and fixed, improving the overall stability and performance of the system.
- **Memory optimizations**: Various memory optimizations have been implemented, reducing the overall memory footprint and improving system efficiency.
- Better error detection during USB mass storage initialization.

---

## v1.0.5alpha (2025-07-10) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
- **Faster ROM emulation startup**: The code to initialize the ROM emulation has been moved before reading the local configuration parameters. This change allows the system to start up faster, and it avoids the annoying "bombs" that could occur when the ROM emulation was not ready before accessing the ROM code.
- **Display a text during the boot**: A text is displayed during the boot process to indicate that the system is starting up. This change improves the user experience by providing feedback during the boot process.
- **Faster write to the command buffer**: The code to write to the command buffer has been optimized, improving the performance of the system when sending commands.
- **Optimized microSD card configuration**: After testing different configurations, the microSD card configuration has been optimized to improve performance and reliability. This change enhances the overall user experience when accessing files on the microSD card.
- **Disabled USB mass storage after boot**: The USB mass storage feature has been disabled after the boot process. This change prevents potential conflicts and improves system stability. The USB mass storage only works during the setup menu. 

### New features
No new features so far.

### Fixes
Everything is a massive and ongoing fix...

---

## v1.0.4alpha (2025-06-17) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
No changes in the current features.

### New features
- **Support for Mega STE**: The Mega STE is now fully supported and tested in EmuTOS and TOS 2.06.

### Fixes
Everything is a massive and ongoing fix...

---

## v1.0.3alpha (2025-06-17) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
- **Re-enable framebuffer**: Now instead of directly writing to the screen memory, the framebuffer is used to manage screen updates. This change improves the performance and responsiveness of the display.

- **Faster timeout**: When there is an error due to timeout writing in the command buffer, the timeout has been reduced to a factor of 10. This change improves the responsiveness of the system specially when accesing the GEMDRIVE mode.

### New features
Real Time Clock (RTC) support has been added. The RTC is now available in the system, allowing for accurate timekeeping and date management.

### Fixes
Everything is a massive and ongoing fix...

---

## v1.0.2alpha (2025-06-10) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
- **ROM BUS stabilization when using the microSD**: The ROM BUS has been stabilized when using the microSD. This change improves the reliability of data transfer and access in the system. It should fix the problem of random bombs when entering into microSD access related menus.
- **Improved microSD access**: The microSD access has been improved, enhancing the overall performance and reliability of file operations.

### New features
No new features have been added in this release.

### Fixes
Everything is a massive and ongoing fix...

---

## v1.0.1alpha (2025-06-05) - Alpha release
- First version

---
