#!/usr/bin/env python3
"""Check what the SELECT button does, on the running RP, without a finger.

Each case presses SELECT through swd.py's GPIO input override, then reads the
firmware's own state over SWD and says whether it matches today's behaviour:

  setup-bounce     setup menu, 15 ms press: ignored (debounce is 30 ms)
  setup-short      setup menu, short press: the RP resets
  runtime-bounce   emulation, 15 ms press: ignored
  runtime-short    emulation, short press: floppy A cycles to the next
                   configured slot (--expect cycle) or nothing happens
                   (--expect ignore, fewer than two slots configured)
  runtime-double   emulation, two short presses 0.5 s apart: floppy A
                   cycles twice (slot read after each press)
  long             any state, a press held past SELECT_LONG_RESET: factory
                   reset (the global settings are erased and the RP resets;
                   Booster then clears every app's settings). Needs --force
                   and saves the settings flash first (restore with `restore`)

  status           print the state the cases read
  backup FILE      save the settings flash (app configs, lookup, global config)
  restore FILE     write a backup back and reset the RP

The ST side of a floppy cycle (the ST reading the new disk) cannot be seen from
here: runtime-short prints the media-change flag, which the RP clears when the
ST reads the new disk's root directory; open A: on the desktop to see it clear.

Usage: python3 tools/dev/select_harness.py <case> [--elf ELF] [--expect ...]
"""

import argparse
import os
import struct
import sys
import time

import swd

TIMERAWL = 0x40054028
FLASH_END = 0x10200000  # 2 MB flash on the SidecarTridge Multi-device
BOUNCE_MS = 15
APP_STATES = {0: "emulation runtime", 1: "emulation init", 2: "NTP init",
              3: "NTP done", 255: "setup menu"}
FLOPPY_MEDIA_CHANGED = 2

SYMBOLS = ("appStatus", "pendingDriveACycle", "currentDriveASlot",
           "blinkSequenceActive", "blinkSequenceRemaining", "fullPathA",
           "jumpBooster", "__rom_in_ram_start__", "_config_flash_start")


class Harness:
    def __init__(self, elf: str | None):
        self.elf = swd.matching_elf(elf)
        self.sym = swd.elf_symbols(self.elf, *SYMBOLS)
        missing = [s for s in SYMBOLS if s not in self.sym]
        if missing:
            raise swd.SwdError(f"{self.elf} lacks {', '.join(missing)}")
        defs = swd.include_defines()
        self.long_ms = defs["SELECT_LONG_RESET"]
        self.media_a = (defs["CHANDLER_SHARED_VARIABLES_OFFSET"]
                        + defs["FLOPPYEMUL_SVAR_MEDIA_CHANGED_A"] * 4)

    def byte(self, name: str) -> int:
        return swd.read_memory(self.sym[name][0], 1)[0]

    def state(self) -> dict:
        addr, size = self.sym["fullPathA"]
        path = swd.read_memory(addr, size).split(b"\0")[0].decode(errors="replace")
        hi, lo = struct.unpack("<HH", swd.read_memory(
            self.sym["__rom_in_ram_start__"][0] + self.media_a, 4))
        return {
            "uptime_us": swd.read_word(TIMERAWL),
            "app": struct.unpack("<i", swd.read_memory(
                self.sym["appStatus"][0], 4))[0],
            "pending_cycle": self.byte("pendingDriveACycle"),
            "slot": self.byte("currentDriveASlot"),
            "led_sequence": self.byte("blinkSequenceActive"),
            "led_remaining": self.byte("blinkSequenceRemaining"),
            "path_a": path,
            "media_changed_a": (hi << 16) | lo,
            "jump_booster": self.byte("jumpBooster"),
        }

    def press_and_watch_led(self, hold_ms: int, samples: int = 40,
                            gap_ms: int = 25) -> bool:
        """Short press, then sample the LED count sequence, all in one
        OpenOCD run: starting another OpenOCD takes about as long as a
        one-slot count lasts (400 ms). True if the sequence was seen."""
        gpio = swd.include_defines()["SELECT_GPIO"]
        ctrl = swd.IO_BANK0 + 4 + 8 * gpio
        normal = swd.read_word(ctrl) & ~(3 << swd.INOVER_SHIFT)
        pressed = normal | (swd.INOVER_HIGH << swd.INOVER_SHIFT)
        addr = self.sym["blinkSequenceActive"][0]
        cmds = [f"mww 0x{ctrl:08x} 0x{pressed:08x}", f"sleep {hold_ms}",
                f"mww 0x{ctrl:08x} 0x{normal:08x}"]
        for _ in range(samples):
            cmds += [f"mdb 0x{addr:08x}", f"sleep {gap_ms}"]
        try:
            out = swd.openocd(*cmds)
        finally:
            if swd.read_word(ctrl) & (3 << swd.INOVER_SHIFT):
                swd.openocd(f"mww 0x{ctrl:08x} 0x{normal:08x}")
        print(f"SELECT (GPIO {gpio}) held {hold_ms} ms")
        return any(line.split(":")[1].split()[0] == "01"
                   for line in out.splitlines() if line.startswith("0x"))

    def press(self, hold_ms: int, force: bool = False) -> None:
        kind = "long" if hold_ms >= self.long_ms else "short"
        swd.cmd_select(argparse.Namespace(press=kind, hold_ms=hold_ms,
                                          force=force))

    def wait_reset(self, before_us: int, timeout: float = 15.0) -> bool:
        """True once the RP has restarted (its microsecond timer went back)."""
        end = time.time() + timeout
        while time.time() < end:
            time.sleep(1.0)
            try:
                if swd.read_word(TIMERAWL) < before_us:
                    return True
            except swd.SwdError:
                pass  # the debug port drops while the chip restarts
        return False


def show(label: str, st: dict) -> None:
    app = APP_STATES.get(st["app"], str(st["app"]))
    print(f"{label}: {app}; floppy A slot {st['slot']} {st['path_a'] or '-'}; "
          f"media changed A={st['media_changed_a']}; pending cycle="
          f"{st['pending_cycle']}; LED sequence={st['led_sequence']} "
          f"(remaining {st['led_remaining']}); uptime "
          f"{st['uptime_us'] / 1e6:.1f} s")


def need_state(h: Harness, app: int) -> dict:
    st = h.state()
    if st["app"] != app:
        raise swd.SwdError(f"needs the RP in '{APP_STATES[app]}', it is in "
                           f"'{APP_STATES.get(st['app'], st['app'])}'")
    return st


def verdict(ok: bool, what: str) -> int:
    print(("PASS  " if ok else "FAIL  ") + what)
    return 0 if ok else 1


def case_bounce(h: Harness, app: int) -> int:
    before = need_state(h, app)
    show("before", before)
    h.press(BOUNCE_MS)
    time.sleep(1.5)
    after = h.state()
    show("after ", after)
    return verdict(after["uptime_us"] > before["uptime_us"]
                   and after["slot"] == before["slot"]
                   and after["app"] == before["app"],
                   f"a {BOUNCE_MS} ms press is ignored")


def case_setup_short(h: Harness) -> int:
    before = need_state(h, 255)
    show("before", before)
    h.press(swd.SELECT_SHORT_MS)
    reset = h.wait_reset(before["uptime_us"])
    if reset:
        time.sleep(3)
        show("after ", h.state())
    return verdict(reset, "a short press in the setup menu resets the RP "
                   "(reset the ST too)")


def case_runtime_short(h: Harness, expect: str) -> int:
    before = need_state(h, 0)
    show("before", before)
    counted = h.press_and_watch_led(swd.SELECT_SHORT_MS)
    after = h.state()
    show("after ", after)
    print(f"      LED count sequence seen right after the press: {counted}")
    no_reset = after["uptime_us"] > before["uptime_us"]
    if expect == "ignore":
        return verdict(no_reset and after["slot"] == before["slot"]
                       and after["path_a"] == before["path_a"]
                       and not counted,
                       "a short press with fewer than two slots does nothing")
    ok = (no_reset and after["slot"] != before["slot"]
          and after["path_a"] != before["path_a"]
          and after["media_changed_a"] == FLOPPY_MEDIA_CHANGED
          and counted)
    rc = verdict(ok, "a short press cycles floppy A: new slot and image, "
                 "media change raised, LED counting the slot")
    if ok:
        print("      open A: on the ST desktop; `status` should then show "
              "media changed A=0 (cleared by the root-directory read)")
    return rc


def case_runtime_double(h: Harness) -> int:
    """Two short presses 0.5 s apart in one OpenOCD run, reading the floppy A
    slot after each: with two slots, before/after alone cannot tell two
    cycles from none."""
    before = need_state(h, 0)
    show("before", before)
    gpio = swd.include_defines()["SELECT_GPIO"]
    ctrl = swd.IO_BANK0 + 4 + 8 * gpio
    normal = swd.read_word(ctrl) & ~(3 << swd.INOVER_SHIFT)
    pressed = normal | (swd.INOVER_HIGH << swd.INOVER_SHIFT)
    slot = h.sym["currentDriveASlot"][0]
    press = [f"mww 0x{ctrl:08x} 0x{pressed:08x}",
             f"sleep {swd.SELECT_SHORT_MS}", f"mww 0x{ctrl:08x} 0x{normal:08x}"]
    try:
        out = swd.openocd(*press, "sleep 200", f"mdb 0x{slot:08x}",
                          *press, "sleep 200", f"mdb 0x{slot:08x}")
    finally:
        if swd.read_word(ctrl) & (3 << swd.INOVER_SHIFT):
            swd.openocd(f"mww 0x{ctrl:08x} 0x{normal:08x}")
    slots = [int(line.split(":")[1].split()[0], 16)
             for line in out.splitlines() if line.startswith("0x")]
    time.sleep(1.0)
    after = h.state()
    show("after ", after)
    print(f"INFO  slot {before['slot']} -> {' -> '.join(map(str, slots))} "
          "(each press 300 ms, 0.5 s apart)")
    moved = len(slots) == 2 and slots[0] != before["slot"] and slots[1] != slots[0]
    return verdict(moved and after["uptime_us"] > before["uptime_us"],
                   "two presses 0.5 s apart cycle floppy A twice")


def settings_region(h: Harness) -> tuple[int, int]:
    start = h.sym["_config_flash_start"][0]
    return start, FLASH_END - start


def case_backup(h: Harness, path: str) -> int:
    start, length = settings_region(h)
    with open(path, "wb") as f:
        f.write(swd.read_memory(start, length))
    print(f"saved {length} bytes of settings flash from 0x{start:08x} to {path}")
    return 0


def case_restore(h: Harness, path: str) -> int:
    start, length = settings_region(h)
    if os.path.getsize(path) != length:
        raise swd.SwdError(f"{path} is not a {length}-byte settings backup")
    out = swd.openocd(*swd.quiesce_commands(),
                      f"flash write_image erase {path} 0x{start:08x} bin",
                      f"verify_image {path} 0x{start:08x} bin", check=False)
    swd.chip_reset()
    ok = "verified" in out
    return verdict(ok, f"settings flash restored from {path} and RP reset")


def case_long(h: Harness, force: bool, backup: str) -> int:
    if not force:
        raise swd.SwdError("a long press is a factory reset: add --force "
                           "(the settings flash is backed up first)")
    case_backup(h, backup)
    before = h.state()
    show("before", before)
    h.press(h.long_ms + 1000, force=True)
    reset = h.wait_reset(before["uptime_us"], timeout=20.0)
    print(f"      restore with: python3 tools/dev/select_harness.py restore "
          f"{backup}")
    return verdict(reset, f"a press held {h.long_ms / 1000:.0f} s factory-resets "
                   "and reboots the RP")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("case", choices=["status", "setup-bounce", "setup-short",
                                     "runtime-bounce", "runtime-short",
                                     "runtime-double", "long", "backup",
                                     "restore"])
    ap.add_argument("file", nargs="?", help="backup file (backup/restore)")
    ap.add_argument("--elf")
    ap.add_argument("--expect", choices=["cycle", "ignore"], default="cycle",
                    help="runtime-short: two or more floppy A slots (cycle) "
                    "or fewer (ignore)")
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    try:
        h = Harness(args.elf)
        if args.case == "status":
            show("now", h.state())
            return 0
        if args.case in ("backup", "restore"):
            if not args.file:
                raise swd.SwdError(f"{args.case} needs a FILE")
            return (case_backup if args.case == "backup"
                    else case_restore)(h, args.file)
        if args.case == "setup-bounce":
            return case_bounce(h, 255)
        if args.case == "runtime-bounce":
            return case_bounce(h, 0)
        if args.case == "setup-short":
            return case_setup_short(h)
        if args.case == "runtime-short":
            return case_runtime_short(h, args.expect)
        if args.case == "runtime-double":
            return case_runtime_double(h)
        return case_long(h, args.force, args.file or os.path.join(
            swd.HERE, "logs", time.strftime("settings-%Y%m%d-%H%M%S.bin")))
    except swd.SwdError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
