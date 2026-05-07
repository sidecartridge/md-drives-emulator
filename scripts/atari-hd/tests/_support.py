"""Shared bootstrap and helper parsers for the atari_hd test suite.

Tests import the script under test via:

    from _support import atari_hd

The sys.path manipulation below makes that import resolve to
scripts/atari-hd/atari_hd.py regardless of the caller's cwd.

Partition-table parsers themselves now live in atari_hd.py (lifted
there in epic-003 / story 007 so the TUI can use them too); we
re-export here to keep the test-side import path stable.
"""

import sys
from pathlib import Path

# Make scripts/atari-hd/ importable so `import atari_hd` works from any
# cwd. unittest discover roots at scripts/atari-hd/tests/, which puts
# this directory on sys.path; we add the parent so the script under
# test resolves too.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import atari_hd  # noqa: E402

# Re-export parsers + slot-offset tuples for the existing test imports.
AHDI_SLOT_OFFSETS = atari_hd.AHDI_SLOT_OFFSETS
MBR_SLOT_OFFSETS = atari_hd.MBR_SLOT_OFFSETS
parse_ahdi_entry = atari_hd.parse_ahdi_entry
parse_ahdi_root = atari_hd.parse_ahdi_root
parse_mbr_entry = atari_hd.parse_mbr_entry
parse_mbr_root = atari_hd.parse_mbr_root
parse_ebr = atari_hd.parse_ebr
parse_xgm_descriptor = atari_hd.parse_xgm_descriptor
parse_bpb = atari_hd.parse_bpb


# 512-byte physical sector. Used by the chain walker; copied here rather
# than imported from atari_hd to keep _support.py self-describing.
_SECTOR_SIZE = 512


def _read_sector(image_path, lba):
    with open(image_path, "rb") as f:
        f.seek(lba * _SECTOR_SIZE)
        return f.read(_SECTOR_SIZE)


def walk_image(image_path, format_id):
    """Recover the partition list from a built image: parse the root
    sector and chase any extended chain. Returns a list of dicts in
    plan order (primaries first, then logicals from the chain).

    For AHDI: each entry has keys start_lba, size_sectors, ident.
    For MBR (PPDRIVER / HDDRIVER): start_lba, size_sectors, part_type.

    The walker is the inverse of build_image()'s partition-table writes
    and is the only honest end-to-end check for the writers' offsets,
    chain pointers, and relative-LBA conventions.
    """
    sec0 = _read_sector(image_path, 0)
    if format_id == atari_hd.FORMAT_AHDI:
        return _walk_ahdi(image_path, sec0)
    return _walk_mbr(image_path, sec0)


def _walk_ahdi(image_path, sec0):
    slots = parse_ahdi_root(sec0)
    result = []
    chain_base = None
    chain_link = None
    for slot in slots:
        if slot["flag"] == 0:
            continue
        if slot["ident"] == b"XGM":
            chain_base = slot["start_lba"]
            chain_link = slot["start_lba"]
            continue
        result.append({
            "start_lba":    slot["start_lba"],
            "size_sectors": slot["size_sectors"],
            "ident":        slot["ident"],
        })
    while chain_link is not None:
        desc = parse_xgm_descriptor(_read_sector(image_path, chain_link))
        logical = desc["logical"]
        # Logical's start is RELATIVE to the descriptor's own LBA.
        abs_start = chain_link + logical["start_lba"]
        result.append({
            "start_lba":    abs_start,
            "size_sectors": logical["size_sectors"],
            "ident":        logical["ident"],
        })
        link = desc["link"]
        if link["flag"] == 0:
            break
        # Link's start is RELATIVE to the chain base.
        chain_link = chain_base + link["start_lba"]
    return result


# MBR partition type for an extended (LBA) container.
# Extended-container type bytes recognised by the walker. PPDRIVER
# emits 0x0F (LBA); HDDRIVER emits 0x05 (CHS). Both are valid markers
# for an extended chain.
_MBR_EXTENDED_TYPES = atari_hd.MBR_EXTENDED_TYPES


def _walk_mbr(image_path, sec0):
    mbr = parse_mbr_root(sec0)
    result = []
    ext_base = None
    ext_link = None
    for slot in mbr["slots"]:
        if slot["part_type"] == 0:
            continue
        if slot["part_type"] in _MBR_EXTENDED_TYPES:
            ext_base = slot["rel_start_lba"]
            ext_link = slot["rel_start_lba"]
            continue
        result.append({
            "start_lba":    slot["rel_start_lba"],
            "size_sectors": slot["sector_count"],
            "part_type":    slot["part_type"],
        })
    while ext_link is not None:
        ebr = parse_ebr(_read_sector(image_path, ext_link))
        logical = ebr["slots"][0]
        # Logical's start is RELATIVE to the EBR's own LBA.
        abs_start = ext_link + logical["rel_start_lba"]
        result.append({
            "start_lba":    abs_start,
            "size_sectors": logical["sector_count"],
            "part_type":    logical["part_type"],
        })
        link = ebr["slots"][1]
        if link["part_type"] not in _MBR_EXTENDED_TYPES:
            break
        # Next-EBR link's start is RELATIVE to the chain base.
        ext_link = ext_base + link["rel_start_lba"]
    return result
