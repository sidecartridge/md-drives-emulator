"""Shared bootstrap and helper parsers for the atari_hd test suite.

Tests import the script under test via:

    from _support import atari_hd

The sys.path manipulation below makes that import resolve to
scripts/atari-hd/atari_hd.py regardless of the caller's cwd.
"""

import struct
import sys
from pathlib import Path

# Make scripts/atari-hd/ importable so `import atari_hd` works from any
# cwd. unittest discover roots at scripts/atari-hd/tests/, which puts
# this directory on sys.path; we add the parent so the script under
# test resolves too.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import atari_hd  # noqa: E402


# AHDI root-sector slot offsets (12 bytes per slot).
AHDI_SLOT_OFFSETS = (0x1C6, 0x1D2, 0x1DE, 0x1EA)

# MBR root-sector slot offsets (16 bytes per slot).
MBR_SLOT_OFFSETS = (0x1BE, 0x1CE, 0x1DE, 0x1EE)


def parse_ahdi_entry(buf, offset):
    """Parse a 12-byte AHDI entry at `offset` in `buf`.

    Layout (big-endian for the two 32-bit fields, per the AHDI/Atari
    convention):
        +0   flag        (1 byte)
        +1   ident       (3 bytes)  e.g. b"GEM" / b"BGM" / b"XGM"
        +4   start_lba   (4 bytes, big-endian)
        +8   size        (4 bytes, big-endian)
    """
    start_lba, size_sectors = struct.unpack_from(">II", buf, offset + 4)
    return {
        "flag":         buf[offset],
        "ident":        bytes(buf[offset + 1:offset + 4]),
        "start_lba":    start_lba,
        "size_sectors": size_sectors,
    }


def parse_ahdi_root(buf):
    """Parse the four AHDI slots from a root sector. Returns a list of
    four dicts (empty slots come back with flag=0 / ident=b'\\x00\\x00\\x00')."""
    return [parse_ahdi_entry(buf, off) for off in AHDI_SLOT_OFFSETS]


def parse_mbr_entry(buf, offset):
    """Parse a 16-byte MBR partition entry at `offset` in `buf`.

    Layout (little-endian for the two 32-bit fields, per IBM/Microsoft
    MBR convention):
        +0   boot          (1 byte; 0x80 marks active/bootable)
        +1   chs_first     (3 bytes; encoded by pack_chs)
        +4   part_type     (1 byte)
        +5   chs_last      (3 bytes)
        +8   rel_start_lba (4 bytes, little-endian; for EBRs this is
                            relative to the EBR sector or the chain base)
        +12  sector_count  (4 bytes, little-endian)
    """
    rel_start_lba, sector_count = struct.unpack_from("<II", buf, offset + 8)
    return {
        "boot":          buf[offset],
        "chs_first":     bytes(buf[offset + 1:offset + 4]),
        "part_type":     buf[offset + 4],
        "chs_last":      bytes(buf[offset + 5:offset + 8]),
        "rel_start_lba": rel_start_lba,
        "sector_count":  sector_count,
    }


def parse_mbr_root(buf):
    """Parse the four MBR slots + the 0x55AA signature. Returns
    {"slots": [<4 dicts>], "signature": bytes(2)}."""
    return {
        "slots":     [parse_mbr_entry(buf, off) for off in MBR_SLOT_OFFSETS],
        "signature": bytes(buf[510:512]),
    }


def parse_ebr(buf):
    """An EBR has the same on-disk layout as an MBR root (4 slot positions
    + signature); only slots 0 and 1 are populated in practice."""
    return parse_mbr_root(buf)


def parse_xgm_descriptor(buf):
    """Parse an AHDI XGM sub-descriptor: slot 0 = logical partition,
    slot 1 = next-descriptor link (or empty when chain ends). Slots 2/3
    are unused; no MBR signature."""
    return {
        "logical": parse_ahdi_entry(buf, AHDI_SLOT_OFFSETS[0]),
        "link":    parse_ahdi_entry(buf, AHDI_SLOT_OFFSETS[1]),
    }


def parse_bpb(buf, offset):
    """Parse a 512-byte FAT16 boot sector / BPB starting at `offset` in
    `buf`. Returns a dict keyed by field name. Real implementation lands
    in epic-002 / story 006."""
    raise NotImplementedError("parse_bpb is not implemented yet")
