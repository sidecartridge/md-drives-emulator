# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

This repo is a microfirmware app for the **SidecarTridge Multi-device**, emulating hard disks (ACSI + GEMDrive) and floppy drives for the Atari ST/STe/Mega ST/Mega STe. It is split between code running on the RP2040 (`rp/`) and 68k code running on the Atari side (`target/atarist/`). See `AGENTS.md` for the authoritative set of workspace rules — the notes below summarize the parts most relevant to day-to-day code work.

## Build & Test Commands

Preferred incremental workflow for RP firmware changes:
```bash
cmake -S rp/src -B rp/build           # configure (once)
cmake --build rp/build -j4            # incremental build
```

Full RP rebuild (wipes `rp/build`, re-patches `fatfs-sdk`, re-pins SDK tags):
```bash
./rp/build.sh pico_w release
```

Full repo build (also wipes `dist/`):
```bash
./build.sh pico_w debug 44444444-4444-4444-8444-444444444444
```
Use the UUID `44444444-4444-4444-8444-444444444444` for local validation unless the user says otherwise.

Atari target firmware (run from inside `target/atarist/` so `$PWD` resolves correctly):
```bash
cd target/atarist && ./build.sh "$PWD" release
```
The Atari build produces `target_firmware.h` which is auto-copied to `rp/src/include/`. After any Atari-side change, rebuild the RP too.

Atari GEMDRIVE test binary (produces `tests/atarist/dist/FSTESTS.TOS`):
```bash
./tests/atarist/build.sh "$PWD/tests/atarist" release
```
Pass any non-empty third argument to enable file logging. Tests run from the Atari desktop while GEMDRIVE points at a writable folder.

Toolchain: Pico SDK + Pico Extras + ARM GCC for RP2040; `stcmd` (via `atarist-docker-toolkit`) for the Atari side — `stcmd` may need a PTY when run through an agent wrapper. Submodules `pico-sdk`, `pico-extras`, `fatfs-sdk` are vendored — do not edit them unless explicitly asked.

## Architecture — the parts that span multiple files

### Two cores, two bus paths

The RP2040 emulates both the cartridge ROM and the Atari-side command channel simultaneously. Two PIO programs run together from startup in `rp/src/emul.c`:
- `romemul` — the **ROM4** path ($FA0000–$FAFFFF), services cartridge ROM reads.
- `commemul` — the **ROM3** path ($FB0000–$FBFFFF), sampled-command protocol; `term.c` and `chandler.c` ingest ROM3 samples via `commemul_poll()`.

Both paths can touch shared bus control signals, so any PIO/DMA/linker change is timing-sensitive. A successful build does **not** prove cartridge bus behavior is correct — hardware validation is required for bus-facing changes.

### Shared-memory protocol

The Atari and RP communicate through shared memory tokens. The `send_sync` / `send_write_sync` macros on the Atari side send commands via ROM3 bus reads, then poll a random token at `$FAF000` until the RP writes a matching value. ACK ordering matters: delays behind slow handlers can cause retransmit. Each `.s` file includes its own copy of `sidecart_functions.s`, so **COMMAND_TIMEOUT is per-file** — changing it in one file does not affect others.

### ROM4 window memory map ($FA0000–$FAFFFF, 64 KB)

```
$FA0000  Cart header + main.s code (~32 KB)
$FA1000  GEMDRIVE code (gemdrive.s)
$FA2800  FLOPPY code (floppy.s)
$FA3400  RTC code (rtc.s)
$FA5400  ACSI code (acsi.s)
$FA8000  Framebuffer / exchange buffer (shared, 8 KB)
$FA8208  Shared variables (token + SVARs per driver)
$FAA440  PUN_INFO (160 B) + BPB tables (704 B)
$FAA7A0  IMAGE_BUFFER (~22 KB, used for ACSI sector delivery)
```

The cart window is **read-only from the Atari CPU**. The RP populates it. Atari-side code sends commands to the RP to "write" shared variables (`CMD_SET_SHARED_VAR`).

### ACSI hard disk emulation

Full block-device emulation of ACSI hard disks from raw disk image files on the SD card. Supports Peter Putnik's **PPDRIVER TOS&DOS dual-BPB format** and HDDRIVER.

Key design points:
- **`bflags = 1`** in the BPB is critical — without it TOS interprets FAT as FAT12 instead of FAT16, truncating cluster numbers and corrupting the FAT walk. This was the root cause of the original "large files garble" bug.
- **Batch sector reads** (`CMD_READ_SECTOR_BATCH` / `acsi_read_sectors_batch`): a single RP round-trip reads up to `ACSIEMUL_BATCH_MAX_BYTES` ($5800 = 22528) bytes, eliminating per-sector command overhead. Toggle via `USE_BATCH_READ equ 1` in `acsi.s`. Falls back to per-sector for requests exceeding the image buffer.
- **BCB buffer rebind**: TOS's default 1024-byte BCB buffers overflow on 4096-byte logical sectors. `acsi_rebind_bcb_buffers` in `acsi.s` allocates a 34 KB pool (carved from `_membot` by the bit-26 cart hook) and reassigns all BCB `BUFR` pointers to 4096-byte slots. Rebind runs on every boot/reset (no idempotence guard — warm resets create fresh BCBs).
- **Pre-GEMDOS memory reservation** (`cart_early_header` in `main.s`): a second cart header fires at CA_INIT bit 26 (before GEMDOS init). It raises `_membot` by `ACSIEMUL_BCB_POOL_BYTES` so the reserved region never enters TOS's free pool. Only fires when `ACSIEMUL_SVAR_ENABLED_ADDR` ≠ `$DEAD0000` (the RP's explicit "disabled" sentinel). On cold boot (SVAR uninitialized = 0), reserves speculatively. If ACSI turns out disabled after the setup menu, `rom_function` triggers a warm reset to reclaim the 34 KB.
- **`__not_in_flash_func`** is mandatory for any RP function called during active PIO bus serving (sector reads, byte-swap, status writes). Without it, XIP flash cache contention with PIO causes protocol failures that only manifest with DEBUG=0 (no DPRINTF delays to mask the race).
- **ACSI disabled sentinel**: the RP writes `$DEAD0000` (not `$00000000`) to `SVAR_ENABLED` when ACSI is disabled. This distinguishes "explicitly disabled" from "cold boot uninitialized" (which is 0). The Atari side checks `cmp.l #$FFFFFFFF` for enabled, `cmp.l #$DEAD0000` for disabled.

### GEMDRIVE hard disk emulation

Intercepts GEMDOS trap #1 to provide a virtual hard disk backed by a folder on the SD card.

Key design points:
- **Drive-letter gating**: `Dsetdrv()` and `Dsetpath()` in `create_virtual_hard_disk` are only called when the drive is C: (drive number 2). For other letters, these are skipped so TOS doesn't boot from / run AUTO programs from the GEMDRIVE drive. The drive still appears in `_drvbits` for desktop access.
- **`_bootdev`** is only set for C:. Non-C: drives skip it.
- **COMMAND_TIMEOUT** in `gemdrive.s` must be `$6FFF` or higher — FatFS operations (Fopen with SD card directory scan) can exceed the old `$FFF` (~20 ms) timeout, causing `send_write_sync` retries that duplicate file descriptors (every Fopen executes twice, leaking fds).

### Floppy drive emulation

Media-change state is RP-owned. Atari-side `floppy.s` must only *read* the shared media-change flags. Working behavior: RP raises `MED_CHANGED` on drive-A slot swap and clears it after the first successful read of the new disk's root-directory start sector.

Floppy A multi-slot: 10 persistent slots in flash. Setup submenu `CTRL+A` configures them. Runtime short-`SELECT` cycles A if ≥2 slots configured.

### LED ownership

`blink.c` owns the Pico W LED. Runtime activity goes through `blink_activityPulse()` + `blink_poll()`. USB MSC inverts: LED on when mounted, off during transfer. `blink.c` may call `network_initChipOnly()` for LED access even when WiFi is down.

### RTC/NTP WiFi

On-demand only. Boot does no unconditional STA init. `APP_MODE_NTP_INIT` in `emul.c` owns the full transient cycle. The leading `network_deInit()` before `network_wifiInit()` is required because `blink.c` may have done a chip-only init.

## Critical implementation rules

- **`__not_in_flash_func`** on all RP functions in the ACSI/GEMDRIVE/floppy read/write hot path. XIP flash contention with PIO causes silent protocol failures.
- **No `PRIu32` / `PRIx32` / other `PRI*` macros.** Cast explicitly: `(unsigned long)x` with `%lu`.
- **`COMMAND_TIMEOUT` is per-file** — each `.s` file includes its own `sidecart_functions.s`. Changing acsi.s's timeout does not affect gemdrive.s.
- **Cart window ($FA0000+) is read-only from the Atari CPU.** Writes are silently ignored by the bus. Use `CMD_SET_SHARED_VAR` commands to update SVARs.
- **Every `.s` module must close with `even / nop × ≥8 / <module>_end:` AFTER `include "inc/sidecart_functions.s"`.** `target/atarist/firmware.py` strips trailing zeros from `BOOT.BIN` before generating `target_firmware.h`, and `COMMAND_SYNC_WRITE_CODE_SIZE` reads 4 bytes past `_end_sync_write_code_in_stack`. Without the NOP tail, the polling code becomes the last non-zero bytes of the firmware, the RP's `ROM_IN_RAM` region past `target_firmware_length` is uninitialized, and the over-read copies garbage into `_dskbufp` (or the 68000 prefetches garbage in modes 0/2). The symptom is 4-bomb bus errors only on write paths, intermittently — acsi.s hit this and it cost a full day to diagnose. Floppy/gemdrive/rtc already follow this convention; keep it.
- Keep debug traces low-noise. Per-sector Rwabs traces were removed for performance (each `send_sync CMD_DEBUG` is a full bus round-trip).
- When touching PIO, DMA, or linker code, validate incrementally on real hardware.

Keep `AGENTS.md` updated when new workflow rules or hardware gotchas are discovered.
