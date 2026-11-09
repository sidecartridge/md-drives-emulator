#!/usr/bin/env python3
"""Read and change the drives-emulator firmware's settings in an emumd flash
file (mdfw run -O flash=FILE).

The firmware keeps its settings in a 4 KB flash sector: entries of 127 bytes,
a key of 30, a type of 1 (int 0, string 1, bool 2), a value of 96, all text,
one after another from the sector's start. The sector is the app's: the
Booster's lookup table at 0x1FE000 holds 38-byte entries, an app UUID of 36
and a sector number of 2 (little-endian), counted from 0x1E0000. Only keys the
firmware already wrote can be changed, so a flash file made by a run of the
firmware (the seed) comes first.

Usage:
  md_settings.py FLASH                          list the app's settings
  md_settings.py FLASH -o OUT KEY=VALUE ...     write a copy with those changed
"""
import argparse
import sys
from pathlib import Path

LOOKUP = 0x1FE000
LOOKUP_ENTRY = 38
UUID_SIZE = 36
CONFIG = 0x1E0000
SECTOR = 0x1000
KEY, TYPE, VALUE = 30, 1, 96
ENTRY = KEY + TYPE + VALUE
APP_UUID = "44444444-4444-4444-8444-444444444444"  # mdfw.ini
TYPES = {0: "int", 1: "string", 2: "bool"}


def app_sector(flash, uuid=APP_UUID):
    """The offset of the app's settings sector in the flash image."""
    for off in range(LOOKUP, LOOKUP + SECTOR - LOOKUP_ENTRY + 1, LOOKUP_ENTRY):
        entry = flash[off:off + LOOKUP_ENTRY]
        if entry[0] in (0, 0xFF):
            break
        if entry[:UUID_SIZE].decode(errors="replace") == uuid:
            return CONFIG + int.from_bytes(entry[UUID_SIZE:], "little") * SECTOR
    raise ValueError(f"app {uuid} not in the lookup table: run the firmware once with this flash")


def entries(flash, sector):
    """(offset, key, type, value) of each entry in the sector, in order."""
    out = []
    for off in range(sector, sector + SECTOR - ENTRY + 1, ENTRY):
        if flash[off] in (0, 0xFF):
            break
        key = flash[off:off + KEY].split(b"\0")[0].decode()
        value = flash[off + KEY + TYPE:off + ENTRY].split(b"\0")[0].decode(errors="replace")
        out.append((off, key, flash[off + KEY], value))
    return out


def set_values(flash, changes):
    """A copy of the flash image with the given {key: value} changed."""
    flash = bytearray(flash)
    found = {key: off for off, key, _, _ in entries(flash, app_sector(flash))}
    for key, value in changes.items():
        if key not in found:
            raise KeyError(f"{key}: no such setting in this flash")
        raw = value.encode()
        if len(raw) >= VALUE:
            raise ValueError(f"{key}: {len(raw)} bytes, at most {VALUE - 1}")
        start = found[key] + KEY + TYPE
        flash[start:start + VALUE] = raw + b"\0" * (VALUE - len(raw))
    return bytes(flash)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("flash", type=Path)
    ap.add_argument("-o", "--out", type=Path)
    ap.add_argument("changes", nargs="*", metavar="KEY=VALUE")
    args = ap.parse_args()
    flash = args.flash.read_bytes()
    if not args.changes:
        for _, key, typ, value in entries(flash, app_sector(flash)):
            print(f"{key:20} {TYPES.get(typ, typ):6} {value}")
        return 0
    if not args.out:
        ap.error("changes need -o OUT (the seed is never changed)")
    changes = dict(c.split("=", 1) for c in args.changes)
    try:
        args.out.write_bytes(set_values(flash, changes))
    except (KeyError, ValueError) as err:
        sys.exit(f"md_settings.py: {err}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
