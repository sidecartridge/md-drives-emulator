# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

This repo is a microfirmware app for the **SidecarTridge Multi-device**, emulating hard disks (GEMDrive) and floppy drives for the Atari ST/STe/Mega ST/Mega STe. It is split between code running on the RP2040 (`rp/`) and 68k code running on the Atari side (`target/atarist/`). See `AGENTS.md` for the authoritative set of workspace rules — the notes below summarize the parts most relevant to day-to-day code work.

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

Atari GEMDRIVE test binary (produces `tests/atarist/dist/FSTESTS.TOS`, run from the Atari desktop while GEMDRIVE is pointing at a writable folder):
```bash
./tests/atarist/build.sh "$PWD/tests/atarist" release
```
Pass any non-empty third argument to enable file logging in the test binary. To add a test, add `test_*()` to the appropriate `tests/atarist/src/*_tests.c` suite (`files_tests.c`, `folder_tests.c`, `folder_listing_tests.c`, `workdir_tests.c`, `chksum_tests.c`) and register it in its `run_*_tests(int presskey)`. For a new suite, add the `.c`/`.h`, include from `main.c`, call `run_<name>_tests(FALSE)` from `run()`, and add the object to `tests/atarist/Makefile`. `main.c` currently calls all suites with `FALSE`, so runs don't pause between cases.

Toolchain: Pico SDK + Pico Extras + ARM GCC for RP2040; `stcmd` (via `atarist-docker-toolkit`) for the Atari side — `stcmd` may need a PTY when run through an agent wrapper. Submodules `pico-sdk`, `pico-extras`, `fatfs-sdk` are vendored — do not edit them unless explicitly asked.

## Architecture — the parts that span multiple files

**Two cores, two bus paths.** The RP2040 emulates both the cartridge ROM and the Atari-side command channel simultaneously. Two PIO programs run together from startup in `rp/src/emul.c`:
- `romemul` — the **ROM4** path, services cartridge ROM reads.
- `commemul` — the **ROM3** sampled-command path; `term.c` and `chandler.c` both ingest ROM3 samples via `commemul_poll()`.

Both paths can touch shared bus control signals, so any PIO/DMA/linker change is timing-sensitive. A successful build does **not** prove cartridge bus behavior is correct — hardware validation is required for bus-facing changes. Do **not** reintroduce the old ROM4 DMA IRQ command path.

**Shared-memory protocol with ACK timing.** The Atari side and RP communicate through shared memory tokens. ACK ordering matters: delays behind slow handlers can cause the remote side to retransmit. Treat ACK-order changes as behavior changes, not refactors.

**Floppy media-change state is RP-owned.** Atari-side `target/atarist/src/floppy.s` must only *read* the shared media-change flags. Working behavior: RP raises `MED_CHANGED` on a successful drive-A slot swap and clears it after the first successful read of the new disk's root-directory start sector. Prior attempts to clear from Atari trap code caused instability — keep `floppy.s` read-only w.r.t. media-change state.

**Floppy A multi-slot.** Drive A persists 10 slots in flash. Slot 1 is the original `FLOPPY_DRIVE_A`; slots 2..10 are extra settings appended to the app config table (append-only for upgrade safety). Setup submenu `CTRL+A`: `2..9`/`0` assign, `SHIFT+2..9`/`0` clear — the clear path relies on **keyboard scan codes**, not shifted ASCII, because shifted number-row keys arrive as punctuation. Runtime short-`SELECT` cycles A only if ≥2 slots are configured.

**LED ownership.** `blink.c` owns the Pico W LED. Runtime floppy/command activity goes through `blink_activityPulse()` + `blink_poll()` (short pulse, not steady-on). Slot-index feedback uses a non-blocking counted-blink sequence. `chandler.c` must **not** drive the LED GPIO directly for activity. USB MSC inverts this: LED is on when mounted, off during active transfer, on again when done. `blink.c` may still call `network_initChipOnly()` for LED access even when WiFi is otherwise down.

**On-demand RTC/NTP WiFi.** Boot does no unconditional STA init. Setup exit enters `APP_MODE_NTP_INIT` only if `RTC_ENABLED`, otherwise goes straight to `APP_EMULATION_INIT`. `APP_MODE_NTP_INIT` in `emul.c` owns the full transient cycle: `network_deInit()` → `network_wifiInit(WIFI_MODE_STA)` → STA connect retries → `rtc_queryNTPTime()` → `network_deInit()`. The leading `network_deInit()` is required because `blink.c` may have done a chip-only init for the LED — do not remove it. User-facing network info (init, connect attempts, failures, assigned IP) only appears during this flow, not on the setup menu.

**lwIP profile is intentionally lean.** Tuned for DHCP + DNS + UDP-for-NTP only. TCP is disabled; mDNS is optional (`LWIP_MDNS_RESPONDER`). Do not re-enable TCP/mDNS/HTTP features casually — the slim profile is part of boot-time optimization work. `network.c`/`.h` track the `md-browser` implementation as of March 2026. Prefer stack buffers over heap in the network path (WiFi STA path avoids `strdup()` for DNS/password).

**USB is MSC-only.** The old composite CDC+MSC path and descriptors are removed. MSC read/write callbacks support chunked multi-sector and partial-sector host transfers — do not regress to the old single-sector `offset == 0` assumption.

**Removed from setup menu.** `Format Image` and `Convert MSA to ST` now live in the File & Download Manager microfirmware. Do not reintroduce them here.

## Runtime behavior worth knowing

- **Setup screen** shows for 5s on power-on; pressing `E`/`X` or letting the countdown elapse starts emulation. `SELECT` + Atari reset re-enters setup without a power cycle. At runtime, short `SELECT` cycles drive A (if ≥2 slots configured).
- **Reset-resistant**: Atari reset does not disturb emulation state. Power cycle returns to setup.
- **Default folders** auto-created on first use: `/hd` (GEMDrive) and `/floppies` (floppy images). Both user-reconfigurable from setup.
- **USB MSC** is only available while in the setup menu — not during active emulation.
- **SD SPI clock** is a global Booster App setting (`SD card bus speed (KHz)`), not a Drives-Emulator setting. Range 1–24 MHz (clamped), default 12.5 MHz. Changes in behavior under high clocks are usually SD-timing, not firmware.
- **Floppy images** accept `.ST` (read-only) and `.ST.RW` (read/write). `CTRL+A` submenu configures the 10-slot A list.

## Conventions

- **No `PRIu32` / `PRIx32` / other `PRI*` macros.** Cast explicitly and use plain specifiers — `(unsigned long)x` with `%lu`, `(unsigned int)x` with `%u`/`%04X`, etc.
- Keep debug traces low-noise — prefer batch or state-transition logs over per-cycle spam.
- When touching PIO, DMA, or linker code, validate incrementally; don't batch several bus-affecting changes into one untested build.

Keep `AGENTS.md` updated when new workflow rules or hardware gotchas are discovered; this file should stay a summary of the big picture.
