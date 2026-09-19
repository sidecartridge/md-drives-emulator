# Developer tools

Host-side tools for working on md-drives-emulator with the hardware attached: a SidecarTridge
Multi-device on an Atari ST, with a Raspberry Pi Debug Probe wired to the RP2040's SWD pins and to
its debug UART (GPIO 0/1). Python tools use the standard library only. Brought over from the
`md-devops` microfirmware and adapted to this firmware's layout.

## Debug console: `console.py`

Captures the debug console of a `debug` build (921,600 baud) to `tools/dev/logs/console.log`, with
a timestamp on every line, and shows it in the terminal. Use it instead of a serial terminal such
as CoolTerm: only one program can open the port.

```bash
python3 tools/dev/console.py watch          # leave running in a terminal
```

`watch` finds the Debug Probe by its USB name (`--port` to choose another device), waits for it when
it is unplugged, and reopens it when it returns. While it runs, other commands read the log:

```bash
python3 tools/dev/console.py since-boot                     # everything since the last boot
python3 tools/dev/console.py since-boot --boot 2            # the boot before that
python3 tools/dev/console.py tail 100
python3 tools/dev/console.py grep 'repeat of chunk' --since-boot
python3 tools/dev/console.py wait 'GEMDRIVE initialized' --timeout 30
```

`grep` and `wait` take Python regular expressions and exit with 3 when nothing matches. `wait` only
matches lines that arrive after it starts, so start it before the action that should print the
line. The log rotates to `console.log.1` at 32 MB.

The settings dump can print bytes that make macOS `grep` treat the log as binary and print
nothing; use `console.py grep` or `grep -a`.

## Build, flash and verify: `flash.sh`

```bash
tools/dev/flash.sh debug                  # build, flash with picotool, check over SWD
tools/dev/flash.sh release --probe        # flash through the Debug Probe instead
tools/dev/flash.sh debug --build-only     # build only
tools/dev/flash.sh debug --src /tmp/src   # build a copy of rp/src (for example a patched linker script)
```

Builds out of tree in `tools/dev/builds/<type>`, incrementally. It does not touch `rp/build` or
the submodules, and warns when a submodule is not at the version `rp/build.sh` pins. The m68k
image is not rebuilt: after changing `target/atarist`, run `target/atarist/build.sh` first (it
regenerates `rp/src/include/target_firmware.h`), then `flash.sh`.

Every build carries a build ID: the git commit, `<sha7>`, or `<sha7>-dirty.<diff7>` when the tree
has uncommitted changes. The same tree always gives the same ID, and at the same checkout path a
byte-identical binary (release builds embed source paths, so another path gives other bytes). The
ID is stored in flash as the `release_build_id` string, and `rp.elf` is kept as
`tools/dev/builds/elf/<type>-<id>.elf` for resolving crash addresses later.

Flashing uses `picotool load -f -x`, which reboots the running firmware into BOOTSEL over USB; when
picotool cannot see the RP it falls back to the Debug Probe. Then `flash.sh` checks the result over
SWD with `swd.py`: the RP booted the ELF, its flash matches the ELF byte for byte, and it carries
the new build ID. On failure it exits with 1 and prints the console since the last boot.

## Debug probe: `swd.py`

The tools talk to the RP only through picotool, the Debug Probe and the console UART, never through
the firmware's own services, so they work with any microfirmware built from this template and with
a hung RP. Memory is read while the CPU keeps running.

```bash
python3 tools/dev/swd.py running tools/dev/builds/debug/rp.elf   # booted this firmware?
python3 tools/dev/swd.py verify tools/dev/builds/debug/rp.elf    # flash identical to the ELF?
python3 tools/dev/swd.py build-id                                # which build is on the RP?
python3 tools/dev/swd.py read 0x2003e0c0 8000 fb.bin             # dump memory
python3 tools/dev/swd.py program tools/dev/builds/debug/rp.elf   # flash through the probe
python3 tools/dev/swd.py screen menu.png                         # the setup menu as the ST shows it
python3 tools/dev/swd.py text                                    # the setup menu as text
python3 tools/dev/swd.py shared                                  # token + shared variables
python3 tools/dev/swd.py resume                                  # release cores a debugger left halted
python3 tools/dev/swd.py reset                                   # reset the whole chip, watchdog-style
python3 tools/dev/swd.py select short                            # press SELECT (short press)
python3 tools/dev/swd.py key g                                   # a keystroke, as if typed on the ST
python3 tools/dev/swd.py app countdown_stop                      # stop the setup-menu countdown
python3 tools/dev/swd.py app gemdrive_stall 2 100                # stall 2 write answers 10 s each
python3 tools/dev/swd.py app gemdrive_fail_write 1               # the next write chunk fails
python3 tools/dev/swd.py app heap_hold 16                        # hold 16 KB more heap (0 releases)
python3 tools/dev/swd.py inject 0x0001 0x0067 0                  # any protocol command
python3 tools/dev/swd.py crash                                   # why did it last reboot?
python3 tools/dev/swd.py postmortem                              # halt, backtraces, resume
python3 tools/dev/swd.py heap                                    # heap size, peak, free space
python3 tools/dev/swd.py heap --watch 5 --csv tools/dev/logs/heap.csv   # sample during a test
```

`screen` renders the 320×200 framebuffer at `DISPLAY_BUFFER_OFFSET` of the cartridge window as a
PNG (scaled 2×, `--scale`). It shows what the RP draws for the ST: the setup menu, not GEM or a
running program. `text` prints the terminal's character buffer (the `screen` array of term.c); the
bottom status line is drawn straight to the framebuffer and only shows in `screen`. `shared`
prints the random token, the token seed and the shared variables, named after the `*_SVAR_*` /
`*_SHARED_VARIABLE_*` indexes in `rp/src/include` (all drivers share one array). They take the
window address from the ELF; without `--elf` they use the cached ELF whose build ID the RP
carries, so flash the build with `flash.sh` first.

`program` and `reset` restart the chip through the watchdog (PSM `WDSEL` + `WATCHDOG_CTRL.TRIGGER`),
never with OpenOCD's `reset`. Up to v1.1.0 this firmware launched core 1 (the SELECT watcher)
within milliseconds of booting, and OpenOCD's multi-core reset sequence touched core 1 again just
after that: core 1 died in the middle of its first trace holding the SDK's stdio mutex, and every
piece of debug output then waited out the 1 s `PICO_STDIO_DEADLOCK_TIMEOUT_MS` (a debug boot of
220 s instead of 0.7 s, and a dead SELECT button). SELECT is now watched on core 0 and core 1 is
not started, but a watchdog-style reset is still the one that leaves the chip exactly as a power-on
reset does, with DMA and PIO stopped. If an old build shows that symptom, run `swd.py reset` or
power-cycle. The same applies to a VS Code debug session's restart button.

A halted RP can still be read. Halting core 1 also pauses the RP2040's timer, so after a debugger
halt run `resume`, which releases both cores; OpenOCD's own `resume` fails in a new OpenOCD run.

`select` needs no firmware code: it forces the SELECT pin's input high through the RP2040's GPIO
input override for 300 ms (`short`) or `SELECT_LONG_RESET` + 1 s (`long`). A long press needs
`--force`, because it is a factory reset: it erases the global settings, and Booster then clears
every app's settings. `select release` clears an override left
behind.

`key`, `app` and `inject` need a `debug` build. They write a small mailbox in RAM
(`rp/src/include/devhooks.h`, found by its `devhooksMailbox` symbol) and wait for the main loop to
acknowledge it. `key` and `inject` queue a protocol command as if the ST had sent it; the firmware
routes it to whichever parser is active — the setup terminal in the setup menu, the drives'
command handler during emulation — so `key` works at the setup menu and `inject` can reach the
GEMDRIVE/floppy/RTC/ACSI handlers during emulation. `app NAME` runs the app command defined as
`DEVHOOKS_APP_<NAME>` in `rp/src/include/emul.h`:

- `countdown_stop` — stop the setup-menu boot countdown, as if a key was pressed.
- `gemdrive_stall CHUNKS [DECISECONDS]` — make the next CHUNKS GEMDRIVE write chunks stall after
  the data is committed but before the ST is answered: the exact shape of a lost write answer.
  Use ≥ 100 deciseconds so the stall outlasts the ST's write timeout and forces a retry; the
  console then shows `Repeat of write chunk N, answering M again` and the copied file must be
  byte-identical to the source. This is the hardware validation for the Fwrite chunk dedup.
- `heap_hold KB` — hold KB more kilobytes of heap, on top of what is already held (`heap_hold 0`
  releases everything). Result 0 when the allocation is refused, so repeated calls walk the heap
  down to a known remainder. Used to check that allocation failures produce errors, not crashes.
- `gemdrive_fail_write [CHUNKS]` — make the next CHUNKS GEMDRIVE write chunks fail as an SD error,
  through the real error path. The ST's `Fwrite` must then return promptly (a short count, or
  the error when nothing was written) instead of looping; the console shows `failing this chunk
  on purpose`.

`crash` prints the watchdog reason and scratch registers of the last reboot without stopping the
RP, with code addresses resolved to source lines by `addr2line`.

`postmortem` halts the RP and prints both cores' backtraces, the registers, the watchdog registers
and key variables through GDB (`$ARM_GDB_PATH/bin/arm-none-eabi-gdb`, as in `.vscode/launch.json`),
then resumes it; `--leave-halted` keeps it stopped for `swd.py resume`. Halting stops the
cartridge bus, so the ST sees a dead cartridge until the RP resumes.

`heap` reads newlib's own malloc state while the RP keeps running, so it needs no firmware code
and works on release builds. It prints the heap's size (from the end of `.bss` to
`__StackLimit`), the arena taken from it so far, the **peak** arena ever reached
(`__malloc_max_sbrked_mem`) with how close that came to the stack, and, by walking the heap's
chunks, the bytes in use, the free bytes inside the arena, how many free blocks they are in and the
largest one. The heap only grows (memory freed stays in the arena for reuse), so the peak also
catches short-lived allocations between samples. A shortage shows as a peak with little room left
before the stack, or as plenty of free bytes but a small largest block (fragmentation). `--watch
SECONDS` samples until Ctrl-C; `--csv FILE` appends every sample for later comparison. If the
heap changes while it is read, the chunk walk is retried once and otherwise reported as failed.

OpenOCD is `$OPENOCD`, `openocd` on `PATH`, or `../pico/openocd/src/openocd`; its scripts come
from `$PICO_OPENOCD_PATH`, the variable `.vscode/launch.json` uses. A command that fails on a
momentary debug-port drop (common while the firmware changes its clock early in boot) is retried.
Close a VS Code debug session first: only one program can use the probe.

## SELECT regression checks: `select_harness.py`

Presses SELECT through `swd.py select` and reads the firmware's own state over SWD (app state,
floppy A slot and image, the media-change flag the ST reads, the LED count sequence, uptime) to
check each thing the button does. Works on release and debug builds; it only needs the ELF of the
running build, found by build ID like `swd.py`.

```bash
python3 tools/dev/select_harness.py status                         # what the cases read
python3 tools/dev/select_harness.py setup-bounce                   # setup menu: 15 ms press ignored
python3 tools/dev/select_harness.py setup-short                    # setup menu: short press resets
python3 tools/dev/select_harness.py runtime-bounce                 # emulation: 15 ms press ignored
python3 tools/dev/select_harness.py runtime-short                  # emulation: floppy A cycles
python3 tools/dev/select_harness.py runtime-short --expect ignore  # emulation, < 2 slots: nothing
python3 tools/dev/select_harness.py runtime-double                 # two presses 0.5 s apart
python3 tools/dev/select_harness.py long --force                   # 10 s press: factory reset
python3 tools/dev/select_harness.py restore tools/dev/logs/settings-<time>.bin
```

Each case first checks the RP is in the state it needs and exits with 2 if not; it prints PASS or
FAIL (exit 0 or 1). A short press in the setup menu reboots the RP under the running ST, so reset
the ST after `setup-short`. `long` saves the settings flash to `tools/dev/logs/` before pressing
(or to the file given) and prints the `restore` command; `restore` writes the backup back with the
cores and DMA stopped, as `swd.py program` does, then resets the RP. After `runtime-short`, open A:
on the ST desktop: `status` should then show the media-change flag cleared, which proves the ST
read the new disk.
