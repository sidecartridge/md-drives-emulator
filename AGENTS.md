# AGENTS.md — Drives Emulator Workspace Notes

Welcome to the `md-drives-emulator` workspace. This file captures the local rules and build habits that matter when working on this repository.

## 1. Environment Setup
- **Workspace root:** `$HOME/mister_wkspc/md-drives-emulator`
- **SDK paths used by the RP build:**
  - `PICO_SDK_PATH=$REPO_ROOT/pico-sdk`
  - `PICO_EXTRAS_PATH=$REPO_ROOT/pico-extras`
  - `FATFS_SDK_PATH=$REPO_ROOT/fatfs-sdk`
- **Tooling used by this repo:**
  - Raspberry Pi Pico SDK / Extras
  - ARM GCC toolchain for RP2040
  - `stcmd` for the Atari target build
- **TTY note:** `stcmd` wants a terminal. Without one (an agent, a tool wrapper, CI) set `STCMD_NO_TTY=1`, as the `Build` workflow does: `STCMD_NO_TTY=1 ./build.sh "$PWD" release`.
- **Hardware tools:** with a Raspberry Pi Debug Probe attached, `tools/dev/flash.sh <debug|release>` builds out of tree, flashes and verifies over SWD; `tools/dev/console.py watch` captures the debug UART (921,600 baud); `tools/dev/swd.py` inspects and drives a running RP. See `tools/dev/README.md`.

## 2. Common Commands
```bash
# Incremental RP build from the existing build directory
cmake --build rp/build -j4

# Configure the RP build directory
cmake -S rp/src -B rp/build

# RP helper build script
./rp/build.sh pico_w release

# Full repo build
./build.sh pico_w debug 44444444-4444-4444-8444-444444444444

# Build the Atari target from inside ./target/atarist
cd target/atarist
./build.sh "$PWD" release

# Build the Atari GEMDRIVE tests through atarist-docker-toolkit/stcmd
./tests/atarist/build.sh "$PWD/tests/atarist" release
```

- Use `44444444-4444-4444-8444-444444444444` as the local validation UUID unless the user asks for a different one.

## 3. Build Notes & Gotchas

- Hatari's GEMDOS drive (`--harddrive`) is the reference for GEMDRIVE's file calls, but not for how a program is loaded: for `Pexec` 0 and 3 on its drive it asks TOS for a bare basepage (mode 5 below TOS 2.00, mode 7 from 2.00) and loads and relocates the program itself (`src/gemdos.c`, `GemDOS_Pexec`), just as GEMDRIVE does. For what TOS's own loader does, load a program from a floppy under Hatari: FLOPTEST's `PROG.TOS` is there for that. Measured that way, TOS 1.04 and 1.06 leave the basepage's `p_flags` at 0 and TOS 1.62 and later copy the header's flags into it - which the Compendium had put at 1.04.
- `rp/build.sh` is not a lightweight incremental build. It:
  - reinitializes submodules
  - checks out pinned SDK tags
  - patches `fatfs-sdk`
  - drops `ffconf.h.bak` in the repo root
  - deletes and recreates `rp/build`
- Prefer `cmake -S rp/src -B rp/build` and `cmake --build rp/build -j4` for normal RP validation.
- The top-level `build.sh` wipes and recreates `dist/`.
- The Atari target build uses `stcmd` inside [target/atarist/build.sh]($HOME/mister_wkspc/md-drives-emulator/target/atarist/build.sh).
- Run the Atari target build from inside `./target/atarist`, so `"$PWD"` resolves to that folder:
  - `cd target/atarist`
  - `./build.sh "$PWD" release`
- The Atari GEMDRIVE test binary is built with [tests/atarist/build.sh]($HOME/mister_wkspc/md-drives-emulator/tests/atarist/build.sh) and produces `tests/atarist/dist/FSTESTS.TOS`.
- **Every Atari `.s` module at `target/atarist/src/*.s` must close with this tail block, placed AFTER `include "inc/sidecart_functions.s"`:**
  ```asm
          even
          nop
          nop
          nop
          nop
          nop
          nop
          nop
          nop
  <module>_end:
  ```
  This is not optional. `target/atarist/firmware.py` strips trailing `0x00` bytes from `BOOT.BIN` before generating `rp/src/include/target_firmware.h`, so whatever non-zero bytes are emitted last become the final words of the C array. On the RP side, `COPY_FIRMWARE_TO_RAM` copies only `target_firmware_length` words into the 64 KB `__rom_in_ram_start__` region; the rest of that RAM is uninitialized. Two things subsequently over-read past the trimmed end:
  1. `COMMAND_SYNC_WRITE_CODE_SIZE = 4 + (_end_sync_write_code_in_stack - _start_sync_write_code_in_stack)` — the `+4` safety margin means `send_sync_write_command_to_sidecart`'s code-copy loop reads 4 bytes past the polling code.
  2. 68000 instruction prefetch on the last two words of the polling loop.

  Without the NOP tail, the polling code (the last thing emitted by `include "inc/sidecart_functions.s"`) ends up as the tail of the trimmed firmware, the 4-byte over-read lands on uninitialized RP RAM, and the garbage either gets executed from `_dskbufp` or confuses TOS. The symptom is intermittent 4-bomb bus errors on write paths only — very hard to diagnose. `floppy.s`, `gemdrive.s`, and `rtc.s` already follow this convention. Any new `.s` module added must do the same.

## 4. Editing Guardrails
- Do not modify vendored code unless the user explicitly asks for it:
  - `/pico-sdk`
  - `/pico-extras`
  - `/fatfs-sdk`
- Keep ROM bus timing changes isolated and minimal. A build passing does not prove the cartridge bus behavior is correct on hardware.
- Prefer incremental validation after each RP-side change when touching PIO, DMA, or linker behavior.
- `romemul` is the ROM4 path. `commemul` is the ROM3 sampled-command path.
- `romemul` and `commemul` currently run together from RP startup in [emul.c]($HOME/mister_wkspc/md-drives-emulator/rp/src/emul.c).
- `term.c` and `chandler.c` both ingest ROM3 samples via `commemul_poll()`. Do not reintroduce the old ROM4 DMA IRQ command path unless the user explicitly wants that rollback.
- ROM3/ROM4 work is timing-sensitive because both paths can touch shared bus control signals. If you change either PIO program, assume hardware validation is required even if the firmware builds.
- Protocol ACK timing matters. The remote side can retransmit if shared-memory token ACK writes are delayed behind slow handlers. Treat ACK-order changes as behavior changes, not refactors.
- GEMDRIVE `Dfree` reports clamped cluster counts so that `b_free × b_clsize × b_secsize` stays ≤ 0x7FFFFFFF: TOS and the desktop do that multiplication in 32-bit longs, and honest FAT32 numbers from a card over 4 GB wrap around (a 32 GB card showed ~720 MB). Do not "fix" the clamp by reporting the real counts.
- Release workflow: a new version starts with `release/vX.Y.Z` branched from `main` (the name is what `version.txt` will contain). Each epic gets its own branch cut from the release branch, `epic/NN-<slug>`, and its pull request targets the release branch, never `main`; it is merged after Diego verifies it on hardware. `main` receives the release branch once, when the version is done. Commit, push and open PRs only when asked.
- Run `docs/epics/cockpit.sh` immediately after every commit, and after any change to a story or epic file. It regenerates `docs/epics/STATUS.md`, which is the dashboard Diego reads; a status written in frontmatter but not regenerated there is a status he cannot see. The file says not to edit it by hand, and it means it.
- Planning notes (epics, stories, iterations) live in `docs/`, which is gitignored and machine-local. Never name an epic, story, iteration or task in anything committed or pushed — comments, docs, changelog, commit messages, PR descriptions (epic branch names, `epic/NN-<slug>`, are the one exception). Write the information itself, not a pointer to a document the reader cannot open.
- The ST-side send routines in `inc/sidecart_functions.s` return with Z set exactly when d0 is 0, and `acsi.s` relies on the flags rather than testing d0. Any change to the wait loops must keep the closing `tst.w d0`.
- GEMDRIVE `Fwrite` chunks are deduplicated: the RP serves a sequence number at `GEMDRIVE_WRITE_CHK`, `gemdrive.s` echoes it in d4 of `CMD_WRITE_BUFF_CALL`, and the RP answers a repeated sequence with the stored byte count instead of writing again. The per-fd memo and the served-sequence bump must stay before the `WRITE_BYTES` answer, and the ST-side read of `GEMDRVEMUL_WRITE_CHK` must stay outside the retry loop.
- GEMDRIVE calls that take a file handle (`Fclose`, `Fread`, `Fwrite`, `Fseek`, `Fdatime`) must route with `detect_emulated_file_handler` (handle ≥ the published base, 16384), never with the drive-letter or current-drive checks: a handle does not tell you the current drive. Path-based calls use `detect_emulated_drive_letter`.
- The RTC/NTP WiFi flow is now on-demand. Boot no longer performs an unconditional STA init/connect path. Setup exit chooses `APP_MODE_NTP_INIT` only when `RTC_ENABLED` is true; otherwise it goes straight to `APP_EMULATION_INIT`.
- `APP_MODE_NTP_INIT` now owns the full temporary RTC/NTP network transaction in [emul.c]($HOME/mister_wkspc/md-drives-emulator/rp/src/emul.c): clean `network_deInit()`, `network_wifiInit(WIFI_MODE_STA)`, STA connect retries, `rtc_queryNTPTime()`, then `network_deInit()` again.
- Keep the Pico W LED policy as-is: `blink.c` may still call `network_initChipOnly()` for LED access. Do not assume “no WiFi at boot” means “no CYW43 chip init at boot”.
- Because of that chip-only LED init, the deferred RTC/NTP path must start from a clean `network_deInit()` before calling `network_wifiInit(WIFI_MODE_STA)`. This is intentional and should not be removed casually.
- The setup menu no longer shows a live network line. User-facing RTC network information now appears only during the RTC/NTP exit flow: WiFi init, connect attempts, failure messages, and the assigned IP address.
- Floppy slot cycling for drive A persists 10 slots in flash. Slot 1 remains the original `FLOPPY_DRIVE_A`; slots 2..10 are extra settings appended at the end of the app config table for upgrade safety.
- The setup menu entry for the multi-image floppy A list is `CTRL+A`, labeled "Configure multiple images".
- In that submenu, plain `2..9` and `0` select slots `2..10`, while `SHIFT + 2..9` or `0` clears that slot.
- The terminal reentry path now passes both a shift flag and the original keyboard scan code. This matters because shifted number-row keys arrive as punctuation in ASCII, so the slot-clear path relies on scan codes, not just the shifted character.
- Floppy media-change state is RP-owned. Atari-side `floppy.s` must only read the shared media-change flags. The current working behavior is: raise `MED_CHANGED` on a successful drive-A slot swap, and clear it on the RP side only after the first successful read of the new disk's root-directory start sector.
- Keep Atari-side `floppy.s` read-only with respect to shared media-change state. Prior attempts to clear media-change from Atari trap code caused instability.
- The current floppy slot-change behavior has been validated with the media-change flag clearing after the first successful read of the new root-directory start sector. Treat that timing as fragile unless hardware testing proves otherwise.
- Short `SELECT` in runtime cycles floppy A only when at least two drive-A slots are configured. If only slot 1 exists, the short press is ignored to avoid disrupting active emulation.
- SELECT is handled on core 0 by `select_poll()` (`select.c`), called from the main loop, the SD-error wait and the Wi-Fi connect polling callback; core 1 is not started. Do not bring back a core-1 watcher: core 1 running SDK code from flash while core 0 erases it (or the reverse) crashed both cores, which froze every long-press factory reset. `select_poll()` must never block: the debounce is time-stamp based. Short press fires on release; long press (`SELECT_LONG_RESET`) fires while still held. The long press is a factory reset by design (global settings erased; Booster then clears every app's settings).
- `main.s` starts the cartridge modules in this order: `poolfix.s`, floppy, GEMDRIVE, ACSI, RTC. The pool fix must be first: it checks that the GEMDOS vector still points into the ROM and reads GEMDOS's private pool addresses from instructions at fixed offsets from that entry (`$2EBC` at -1796, `$2A79` at -1102). Floppy must come before GEMDRIVE and ACSI: it sets `_bootdev` to A:, and the emulated hard disks then set C: (GEMDRIVE when it is C:, ACSI when its first volume is C:). TOS runs `\AUTO\` from `_bootdev` only if that drive's bit is in `_drvbits`.
- The pool fix (`poolfix.s`, `$FA4C00`) exists because TOS 1.04/1.06 GEMDOS compacts its OS pool with a broken routine (`getosm()`/`mdfind()`), which corrupts the pool once a Show Info walk exhausts it after programs have run: drives stop opening, the desktop hangs, the RP sees no more commands. POOLFIX3.PRG refuses to install when trap #1 is already hooked. Our hook compacts before the next call after Pterm0/Ptermres/Mfree/Mshrink/Pterm so that fewer than 4 descriptor slots are ever free. Its code is our own implementation of Atari's documented algorithm on the TOS data layouts; the TOS source reconstruction (`th-otto/tos1x`) has no license, so do not copy from it. The module's variables are in the cartridge window and are written by the RP: `pf_enabled` at the fixed address `$FA4C04` (right after the `bra.w` at `$FA4C00`; `poolfix_init()` sets it from `POOLFIX_ENABLED`, setup menu `[K]`, default on), and `pf_flag`/`pf_next` through `POOLFIX_SET_LONG`/`POOLFIX_COMPACTED`, limited to `$FA4C00-$FA5400`. Keep `pf_enabled` at `$FA4C04`: `POOLFIX_ENABLED_ADDR` in `poolfix.h` must match.
- `send_sync` keeps d1-d7 and destroys d0 and a0-a3; `send_write_sync` keeps d1-d6 and a4 and destroys d0, d7 and a0-a3 (the table is in `inc/sidecart_macros.s`). The senders leave a0-a3 pointing into the ROM3 command window (`$FB8000`), so a payload read through one of them is read from the command window itself and every read is sampled by the RP as a protocol word: the command is rejected with a checksum error, identically on every retry. Take what you need into a kept register *before* the send, and reload address registers after it. `.Pexec` in `gemdrive.s` shows both halves: it takes the trap frame into d3 before the drive check (which sends commands) and reloads a0 from the shared window afterwards. Moving that capture after the check cost an afternoon on 2026-09-20: the AES's `Pexec(3, "XCONTROL.ACC")` was rejected six times per boot, the accessory never loaded and each boot lost ~15 s to retries.
- Trap #1 hooks must not push anything on the caller's stack before they know they handle the call. TOS 1.04 starts GEM (`aes/gemjstrt.S` in the TOS source reconstruction) on a 132-byte stack (`ustak`), with `Super()` putting the supervisor stack there too, and calls GEMDOS from it (Super, Mshrink, Malloc) with about 110 bytes left above the AES's resolution variables (`gl_rschange`, `gl_restype`) and then GEMDOS globals. GEMDRIVE used to save 48 bytes of registers before its dispatch, and the pool fix 60 more plus `send_sync`: on some boots GEM's standard handles were zeroed, so every TOS program's console output went to MIDI (handle 0 is BIOS device 3 on TOS 1.x), and the same overflow can reach the resolution variables (one suspected cause of GEM starting in low resolution despite `DESKTOP.INF`; a remaining case is still open). GEMDRIVE now looks the opcode up with scratch registers first (`d0`, `a1`; `d1` holds the MegaSTE speed) and saves registers only for calls it handles; the pool fix switches to its own 1 KB stack (`Malloc`ed at install, owned by the initial process; top in `pf_stack`) before any register save or RP command, leaving 4 bytes on the caller's. A later GEMDRIVE handler that nests GEMDOS calls still runs on the caller's stack: keep that path lean.
- A hook that a trap's RTE returns into runs in the mode of whoever made the call, and on TOS 1.00/1.02 GEMDRIVE has one: `Pexec` there has no `PE_GO_AND_FREE`, so `.pexec_mshrink_exit` replaces the return address in the exception frame (`STACK_SIZE_HACK_PEXEC(sp)`, 48 bytes of saved registers + the 2-byte SR) and frees the child's memory when the child ends. The desktop and the AES call `Pexec` from supervisor mode, so for years it only ever ran there; an ordinary program calls it from user mode, where the first 2 KB of memory is a bus error. The `reentry_gem_lock` that used to sit around the `Mfree` sends a command, and `send_sync` reads `_dskbufp` at `$4C6`: two bombs for every user-mode program that started another one on TOS 1.00 or 1.02, and the corruption showed up as the *parent's* variables turning to garbage. Nothing on that path may read system variables or talk to the RP; the lock was never needed anyway, because `Mfree` is not a call GEMDRIVE dispatches. Note also that TOS 1.00 does not free a `PE_GO` child by itself (measured: without the `Mfree` each child's basepage climbs by the size of its TPA), so the `Mfree` has to stay.
- The RP2040's GPIO edge detector reads the pad *before* the input override, so presses forced by `swd.py select` never raise the SELECT edge interrupt. The interrupt only adds history for presses that start and end while core 0 is busy; the debounce must keep timing the sampled level itself, or forced presses (and anything else the interrupt misses) skip it. Test changes with `tools/dev/select_harness.py`, and the interrupt path with a real finger press during a long GEMDRIVE copy.
- Slot-index LED feedback uses the non-blocking counted blink sequence in `blink.c`.
- Runtime floppy/command activity LED is separate from the slot-index sequence. It now goes through `blink_activityPulse()` plus `blink_poll()` and should behave as a short access pulse, not a steady-on indicator.
- `chandler.c` should not drive the Pico W LED GPIO directly anymore for normal activity. Keep activity LED ownership in `blink.c`.
- The legacy setup-menu floppy utilities were removed. Do not reintroduce `Format Image` or `Convert MSA to ST` in the Drives Emulator menu; that workflow belongs in the File Manager microfirmware now.
- USB device mode is now MSC-only. The old CDC composite path has been removed from the TinyUSB config and descriptors.
- USB MSC read/write callbacks now support chunked host transfers, including multi-sector and partial-sector accesses. Do not regress them back to the old single-sector `offset == 0` assumption.
- USB MSC LED behavior is intentionally inverted from floppy/GEMDRIVE activity: when USB mass storage is mounted, the Pico W LED stays on, goes off when MSC traffic starts (`blink_trafficDip()`), and `blink_poll()` turns it back on after `BLINK_TRAFFIC_QUIET_US` without traffic. Do not toggle the LED per transfer: each toggle is an SPI transaction to the CYW43.
- `chandler_loop()` writes the answer token before `blink_activityPulse()`: on a Pico W every LED update is a CYW43 bus transaction, and the ST is polling for that token. Keep anything slow that is not part of the answer after the token write.
- USB MSC overlaps SD and USB work through one chunk buffer in `usb_mass.c`: `usb_mass_poll()` (called right after every `tud_task()`) writes the chunk parked by the last write callback and reads ahead the next sequential chunk while the USB interrupt streams packets. Invariants: every MSC callback (read, write, eject, SYNCHRONIZE CACHE) finishes a parked write first; a write discards any read-ahead; a parked write that fails sets a sticky `write_failed` that fails all later writes until the device reconnects. `usb_mass_get_mounted()` is false after the host ejects the card.
- TinyUSB 0.18 (bundled with Pico SDK 2.2.0) can panic with `ep XX was already available`: `hw_endpoint_lock_update()` is an empty stub, so main-loop endpoint arming races the USB IRQ. The chip halts and the host drops the disk. It cannot be worked around from the app: the no-OS osal re-enables the USB IRQ inside `tud_task()`. Fixed in TinyUSB 0.21 (`rp2usb_lock`); the plan is to take it with Pico SDK 2.3.2. It shows on release builds only (debug-build timing hides it); `swd.py postmortem` shows `panic` under `send_csw` / `usbd_edpt_xfer`.

## 5. Formatting Rules
- Do **not** use `PRIu32`, `PRIx32`, or related `PRI*` format macros in this repo.
- When printing fixed-width integers, use normal `printf` format specifiers with explicit casts instead.
  - Example: cast to `unsigned long` and print with `%lu`
  - Example: cast to `unsigned int` and print with `%u` or `%04X`
- Keep debug traces readable and low-noise. Prefer batch or state-transition logs over per-cycle spam.

## 6. Network Notes
- `network.c` / `network.h` are aligned with the `md-browser` implementation as of March 2026.
- mDNS support is optional and guarded by `LWIP_MDNS_RESPONDER`. If it is disabled in `lwipopts.h`, the mDNS code should compile out cleanly.
- Prefer stack buffers over heap allocation in the network path unless there is a strong reason not to. The current WiFi STA path avoids `strdup()` for DNS/password parsing.
- `lwipopts.h` is now tuned for the current RTC-only runtime use: DHCP for the IP address, DNS resolution, and UDP for NTP. TCP is disabled in the current profile.
- Do not casually re-enable TCP/mDNS/HTTP-related lwIP features unless a new runtime feature really needs them. The current lighter profile is part of the boot-time optimization work.

Keep this file updated when new repo-specific workflow rules or hardware gotchas are discovered.
