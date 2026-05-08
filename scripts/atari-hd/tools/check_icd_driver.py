#!/usr/bin/env python3
"""Validate an ICD AHDI-compatible driver binary and report its SHA-256.

Usage:
    python scripts/atari-hd/tools/check_icd_driver.py PATH

Story 008 (epic-004 / real-hardware compatibility) prerequisite:
making a bootable AHDI image (story 011) needs a user-supplied ICD
driver file (typically `ICDBOOT.PRG` from the ICD Pro 6.5.5
distribution) to embed in the boot partition. The image-builder
copies the bytes verbatim and writes them as `/ICDBOOT.SYS` at
the root of the boot partition's FAT16 filesystem (extension
renamed `.PRG` -> `.SYS` to match how real ICD-formatted disks
store the driver — the IPL searches for the `.SYS` name). This
helper checks the source file looks like a real Atari .PRG before
story 011's pipeline reads it.

Exit codes:
    0  file is a syntactically-plausible Atari .PRG; SHA-256 printed
    1  file missing, unreadable, too small, or the .PRG magic word
       (0x601A) doesn't match

The .PRG magic is a 68000 BRA.S that the Atari TOS loader uses to
distinguish program files from arbitrary blobs. Every ICD driver
version we've seen (ICD Pro 6.x's ICDBOOT.SYS, the ICD ADV driver,
the ICD HD utility) starts with it. If your file doesn't, it's
either compressed/corrupted or not the right binary.

Stdlib only -- this is a prerequisite that runs on whatever Python
the user has handy. No third-party deps.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import sys


# 68000 BRA.S short opcode (0x60) + 8-bit unsigned displacement (0x1A).
# Every Atari .PRG / .TOS / .APP starts with this 2-byte word -- the
# 0x1A displacement skips the 28-byte program header to the start of
# the actual code segment.
ATARI_PRG_MAGIC = b"\x60\x1A"

# Hard cap on what we'll consider a plausible AHDI driver binary.
# ICDBOOT.SYS in real ICD Pro distributions is well under 64 KiB; we
# allow 1 MiB to be generous against future versions but still catch
# accidental wrong-file paths (an ISO or floppy image would blow this
# cap immediately).
ICD_DRIVER_MAX_BYTES = 1 * 1024 * 1024

# Hard floor: the .PRG magic + a minimal 28-byte program header.
ICD_DRIVER_MIN_BYTES = 30


def _sha256_of_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(64 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _check(path: str) -> int:
    """Return 0 when `path` passes every check, 1 otherwise. Prints the
    findings to stdout / stderr in either case so the user can act on
    the failure without re-running."""
    try:
        size = os.path.getsize(path)
    except OSError as e:
        print(f"ERROR: cannot stat {path!r}: {e}", file=sys.stderr)
        return 1

    if size < ICD_DRIVER_MIN_BYTES:
        print(f"ERROR: {path!r} is {size} bytes -- too small to be a "
              f"valid Atari .PRG (need at least {ICD_DRIVER_MIN_BYTES})",
              file=sys.stderr)
        return 1

    if size > ICD_DRIVER_MAX_BYTES:
        print(f"ERROR: {path!r} is {size} bytes -- too large to be an "
              f"AHDI driver (max {ICD_DRIVER_MAX_BYTES}); did you point "
              "this at a disk image instead of the driver?",
              file=sys.stderr)
        return 1

    try:
        with open(path, "rb") as f:
            magic = f.read(2)
    except OSError as e:
        print(f"ERROR: cannot read {path!r}: {e}", file=sys.stderr)
        return 1

    if magic != ATARI_PRG_MAGIC:
        print(f"ERROR: {path!r} does not start with the Atari .PRG "
              f"magic word 0x601A (got 0x{magic.hex()}). This is "
              "either a compressed archive or not an Atari executable.",
              file=sys.stderr)
        return 1

    digest = _sha256_of_file(path)

    print(f"OK  {path}")
    print(f"    size:   {size} bytes")
    print(f"    magic:  0x{magic.hex()} (Atari .PRG)")
    print(f"    sha256: {digest}")
    print()
    print("Record this SHA-256 alongside the source URL you downloaded")
    print("from. Future verifications can compare against this value.")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        prog="check_icd_driver.py",
        description=(
            "Validate an ICD AHDI driver binary (e.g. ICDBOOT.SYS) and "
            "print its SHA-256. Usage prerequisite for the bootable "
            "AHDI image flow in epic-004 / story 011."))
    p.add_argument("path", metavar="PATH",
                   help="Path to the ICD driver file (typically "
                        "ICDBOOT.PRG extracted from an ICD Pro "
                        "distribution).")
    args = p.parse_args(argv)
    return _check(args.path)


if __name__ == "__main__":
    sys.exit(main())
