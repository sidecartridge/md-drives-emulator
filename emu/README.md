# The firmware in Hatari (EmuMD)

[EmuMD](https://github.com/neilrackett/emumd) builds this firmware's own C code for the computer as
a `.mdfw` and runs it in its fork of Hatari, with a Multi-device on the cartridge port: the ST boots
the firmware's cartridge image, and a folder stands in for the microSD card. It is the fast bench for
the drivers' logic, the ST side and the setup menu, on every machine Hatari emulates.

It times nothing (the emulated Multi-device is infinitely fast) and has no PIO, DMA or USB, so
timing, the bus and sign-off stay on the device, with the tools in `tools/dev/`.

## Set-up

macOS: `brew install cmake sdl2 libpng libslirp pkg-config`.

```sh
git submodule update --init emu/emumd
tools/dev/build_hatari_fork.sh                    # EmuMD's Hatari, once
mkdir -p emu/tos emu/sd
ln -s "$HOME/mister_wkspc/TOS/tos104us.img" emu/tos/tos104us.img
```

`emu/tos/` and `emu/sd/` (the emulated card) are not committed. `emu_tests.py` finds its TOS images
where `hatari_tests.py` does, and needs [atari-hd](https://github.com/sidecartridge/atari-hd) beside
this repository for its ACSI run (or `ATARI_HD` set to its `atari_hd.py`).

## Build and run

```sh
tools/dev/mdfw.sh build                           # build/drives-emulator.mdfw
tools/dev/mdfw.sh run --no-user-config            # a window, an ST with TOS 1.04
```

After changing `target/atarist/`, rebuild it first (`cd target/atarist && STCMD_NO_TTY=1 ./build.sh
"$PWD" release 0`): the plugin takes the cartridge image from `rp/src/include/target_firmware.h`.

`mdfw.ini` builds everything in `rp/src` but the hardware layer. Pointer truncation fails the build:
addresses are 64 bits here, so what derives from the cartridge window is kept in `uintptr_t`.

## The tests

```sh
tests/atarist/build.sh "$PWD/tests/atarist" release log   # with STCMD_NO_TTY=1, as for the target
tools/dev/emu_tests.py                                    # everything, TOS 1.04
tools/dev/emu_tests.py --harness fstests-acsi --tos 4.04  # FSTESTS from an ACSI image, on a Falcon
```

Each run makes its own card and flash in `tools/dev/logs/emu/<harness>-<TOS>/` and writes every
driver's setting to that flash. `fstests` and `floptest` run from GEMDRIVE's C:, `fstests-acsi` from
an image with GEMDRIVE off, and `rtctest` with Wi-Fi on, reaching NTP through this computer's network.

What does not count as a failure is listed by name in `emu_tests.py`: what EmuMD's FatFs cannot do,
being a thin layer over the computer's files (no file locks, no hidden bit, no file dates), and what
TOS's own file system answers from an ACSI image, as it does on the hardware.

## Gotchas

- **Never give Hatari a GEMDOS drive** (`--harddrive`): it takes the first 1 KB of the cartridge.
- **Stand-ins are for hardware only** (`emu/standins.c`). A stub of `acsi_init()`, which must run
  even with ACSI off, makes every boot warm-reset for ever.
- **A harness ends by restarting the device, and Hatari then stops answering**, even to SIGTERM.
  What the log holds by then is the result.
- **The firmware syncs an ACSI image a while after writing it**, and never does after that restart:
  the end of a log inside the image can be missing, so the run reads Hatari's console.
- **`mdfw run --headless` stops after 500 frames** unless `--frames` says otherwise. A Falcon takes
  some 2,000 frames to boot, and runs at about 18 a second here.
- **Without `--no-user-config`** Hatari reads your own `hatari.cfg`.
- **SELECT** is pressed by creating the file that `EMU_SELECT_FILE` names (`emu/standins.c`).
