#!/usr/bin/env python3
"""atari_hd.py - build Atari ST hard disk images for the SidecarTridge
Multi-device drives emulator.

Produces a hard disk image in one of three on-disk layouts:
  1) AHDI     - native Atari (no MBR); AHDI partition table at sector 0.
                Up to 4 partitions (primary slots only; no XGM chain in v1).
  2) PPDRIVER - PPera TOS&DOS hybrid; DOS MBR + dual BPB per partition
                (DOS at firstLBA, TOS at firstLBA+1) with OEM "PPGDODBC".
                Up to 4 partitions (MBR primary slots only).
  3) HDDRIVER - HDDRIVER TOS&DOS hybrid; DOS MBR (slots 0,1) + AHDI overlap
                markers (slots 2,3) + dual BPB per partition. Up to 2
                partitions (the overlap trick uses the same bytes MBR
                slots 2/3 would occupy).

The FAT16 filesystem is produced by mkfs.vfat (or mkdosfs) from the
dosfstools package. This script only assembles the partition table, the
hybrid BPBs, and the outer image bytes.

Inspired by Hatari's tools/atari-hd-image.sh but rewritten in Python and
extended with the hybrid layouts used by real Atari hard disk drivers.

Usage: interactive. Run with no arguments.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from typing import List, Optional

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

SECTOR_SIZE = 512
MIB = 1024 * 1024

# mkfs tool candidates, tried in order
MKFS_CANDIDATES = ("mkfs.vfat", "mkdosfs")

# Layout ids
FORMAT_AHDI = "AHDI"
FORMAT_PPDRIVER = "PPDRIVER"
FORMAT_HDDRIVER = "HDDRIVER"

# Limits
MIN_IMAGE_MB = 2              # absolute floor; mkfs.vfat refuses smaller
MAX_PARTITION_MB = 2048       # FAT16 ceiling with 32 KB clusters
MIN_PARTITION_MB = 2

# AHDI partition-size caps imposed by the TOS kernel's BGM handling. These
# are only enforced on the AHDI path (the hybrid formats go through the
# DOS view which doesn't care). "Strict" = TOS < 1.04 (520ST / 1040ST /
# Mega ST running the original TOS). Permissive = TOS 1.04 and later.
AHDI_MAX_PARTITION_MB_STRICT = 256   # TOS < 1.04 BGM cap
AHDI_MAX_PARTITION_MB = 512          # TOS 1.04+ BGM cap

# Threshold for AHDI's GEM vs. BGM partition-id choice. <= threshold uses
# "GEM" (small partition); above uses "BGM" (big).
AHDI_GEM_MAX_MB_STRICT = 16          # TOS < 1.04 GEM cap
AHDI_GEM_MAX_MB = 32                 # TOS 1.04+ GEM cap

# mkfs behavior: sectors per cluster fixed at 2 (matches Hatari's script and
# real HDDRIVER/PPDRIVER images we've seen on disk).
SECTORS_PER_CLUSTER = 2

# AHDI partition-table offsets inside sector 0
AHDI_SLOT0_OFFSET = 0x01C6
AHDI_SLOT1_OFFSET = 0x01D2
AHDI_SLOT2_OFFSET = 0x01DE
AHDI_SLOT3_OFFSET = 0x01EA
AHDI_ENTRY_SIZE = 12
AHDI_FLAG_EXISTENT = 0x01
AHDI_FLAG_BOOTABLE = 0x80

# MBR partition-table offsets
MBR_P0_OFFSET = 0x01BE
MBR_SIGNATURE_OFFSET = 510
MBR_SIGNATURE = bytes((0x55, 0xAA))

# BPB field offsets inside a boot sector (Hatari/DOS convention)
BPB_OEM_OFFSET = 3
BPB_OEM_LENGTH = 8
BPB_BYTES_PER_SEC_OFFSET = 11
BPB_SEC_PER_CLUS_OFFSET = 13
BPB_RESERVED_OFFSET = 14
BPB_FAT_COUNT_OFFSET = 16
BPB_ROOT_ENTRIES_OFFSET = 17
BPB_TOTAL_SEC16_OFFSET = 19
BPB_SEC_PER_FAT_OFFSET = 22

# Canonical CHS geometry used by Hatari's script
CHS_HEADS = 16
CHS_SECTORS_PER_TRACK = 32

# Sector size warning threshold (any size <= this goes through silently)
SECTOR_SIZE_WARN_THRESHOLD = 8192

# Per-format maximum partition count. TOS drive letters are C:..P: (14),
# which is the ceiling for every format once the extended chain is in play.
MAX_PARTITIONS = {
    FORMAT_AHDI: 14,       # up to 4 primary, rest via XGM chain
    FORMAT_PPDRIVER: 14,   # up to 4 primary, rest via MBR extended chain
    FORMAT_HDDRIVER: 14,   # 1 primary max, rest via MBR extended chain
}

# Per-format maximum of PRIMARY entries. Primary partitions occupy the
# first N root-sector slots directly. When the user wants more than a
# format's primary cap, the LAST available slot becomes an extended-chain
# link (XGM for AHDI, MBR extended container for PPERA/HDDRIVE) and the
# remaining partitions live as logicals inside that chain.
MAX_PRIMARY_PARTITIONS = {
    FORMAT_AHDI: 4,        # 4 AHDI root slots; slot 3 becomes XGM when N>4
    FORMAT_PPDRIVER: 4,    # full MBR primary use when N <= 4
    FORMAT_HDDRIVER: 1,    # HDDRIVER convention: one primary + AHDI marker
}

# Default image size (in MB) suggested to the user at the "Total image
# size" prompt. AHDI typically hosts smaller hobbyist disks; the hybrid
# TOS&DOS layouts are usually sized for real-world, multi-partition Atari
# drives so they default to 2 GB.
DEFAULT_IMAGE_MB = {
    FORMAT_AHDI: 256,
    FORMAT_PPDRIVER: 2048,
    FORMAT_HDDRIVER: 2048,
}

# MBR partition type for extended containers (LBA-addressable variant).
MBR_TYPE_FAT16 = 0x06
MBR_TYPE_EXTENDED = 0x0F


# --------------------------------------------------------------------------
# Platform / toolchain probes
# --------------------------------------------------------------------------

def abort_if_windows() -> None:
    if sys.platform.startswith("win"):
        sys.stderr.write(
            "atari_hd.py: Windows is not yet supported.\n"
            "Run this tool on macOS or Linux for now.\n"
            "(A WSL-based path would be a welcome contribution.)\n"
        )
        sys.exit(1)


def find_mkfs_tool() -> str:
    # mkfs.vfat / mkdosfs typically live in /sbin or an equivalent that's
    # not always on the interactive user's PATH (especially on macOS, where
    # Homebrew symlinks them into /usr/local/sbin or /opt/homebrew/sbin).
    extra_dirs = [
        "/sbin", "/usr/sbin",
        "/usr/local/sbin", "/usr/local/bin",
        "/opt/homebrew/sbin", "/opt/homebrew/bin",
    ]
    for name in MKFS_CANDIDATES:
        path = shutil.which(name)
        if path:
            return path
        for d in extra_dirs:
            candidate = os.path.join(d, name)
            if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                return candidate
    sys.stderr.write(
        "ERROR: neither mkfs.vfat nor mkdosfs was found on PATH.\n"
        "\n"
        "  macOS:  brew install dosfstools\n"
        "          (and ensure /usr/local/sbin or /opt/homebrew/sbin is on\n"
        "           your PATH, or run this script from a shell that has it)\n"
        "  Linux:  sudo apt-get install dosfstools   (or equivalent)\n"
    )
    sys.exit(1)


# --------------------------------------------------------------------------
# Geometry helpers
# --------------------------------------------------------------------------

def mb_to_sectors_512(mb: int) -> int:
    return (mb * MIB) // SECTOR_SIZE


def choose_logical_sector_size(partition_sectors_512: int) -> int:
    """Mirror Hatari's atari-hd-image.sh doubling rule: start at 512 bytes
    per logical sector with 2 sectors per cluster, double the logical sector
    size until cluster_count <= 32765.

    Returns a value from {512, 1024, 2048, 4096, 8192, 16384}."""
    clusters = partition_sectors_512 // SECTORS_PER_CLUSTER
    bps = 512
    while clusters > 32765:
        clusters //= 2
        bps *= 2
    return bps


def lba_to_chs(lba: int) -> tuple:
    """Convert an LBA to a (cylinder, head, sector) triple clamped to the
    classic MBR CHS range. Matches Hatari's atari-hd-image.sh."""
    max_lba = 1024 * CHS_HEADS * CHS_SECTORS_PER_TRACK
    if lba >= max_lba:
        return (1023, CHS_HEADS - 1, CHS_SECTORS_PER_TRACK)
    c = lba // (CHS_HEADS * CHS_SECTORS_PER_TRACK)
    h = (lba // CHS_SECTORS_PER_TRACK) % CHS_HEADS
    s = (lba % CHS_SECTORS_PER_TRACK) + 1
    return (c, h, s)


def pack_chs(c: int, h: int, s: int) -> bytes:
    return bytes((h & 0xFF, (s & 0x3F) | ((c >> 2) & 0xC0), c & 0xFF))


# --------------------------------------------------------------------------
# Partition-table writers
# --------------------------------------------------------------------------

def write_ahdi_entry(buf: bytearray, offset: int, flag: int, ident: bytes,
                     start_lba: int, size_sectors: int) -> None:
    """Write a 12-byte AHDI entry at `offset` inside `buf`.
       ident is exactly 3 bytes (e.g. b'GEM', b'BGM', b'XGM')."""
    if len(ident) != 3:
        raise ValueError("AHDI id must be 3 bytes")
    buf[offset] = flag & 0xFF
    buf[offset + 1:offset + 4] = ident
    buf[offset + 4:offset + 8] = start_lba.to_bytes(4, "big")
    buf[offset + 8:offset + 12] = size_sectors.to_bytes(4, "big")


def write_mbr_entry(buf: bytearray, offset: int, boot: int, part_type: int,
                    start_lba: int, sector_count: int,
                    rel_start_lba: Optional[int] = None) -> None:
    """Write a 16-byte MBR partition entry at `offset` inside `buf`.

    `start_lba` is the absolute LBA, used to compute the CHS fields.
    `rel_start_lba` is the value written into the start-LBA bytes at offset+8;
    it defaults to `start_lba`. Extended-chain EBR entries use
    rel_start_lba=relative-to-ebr / rel-to-container while CHS still comes
    from the absolute LBA — which matches what Linux fdisk / Windows write."""
    if rel_start_lba is None:
        rel_start_lba = start_lba
    c0, h0, s0 = lba_to_chs(start_lba)
    c1, h1, s1 = lba_to_chs(start_lba + sector_count - 1)
    buf[offset] = boot & 0xFF
    buf[offset + 1:offset + 4] = pack_chs(c0, h0, s0)
    buf[offset + 4] = part_type & 0xFF
    buf[offset + 5:offset + 8] = pack_chs(c1, h1, s1)
    buf[offset + 8:offset + 12] = rel_start_lba.to_bytes(4, "little")
    buf[offset + 12:offset + 16] = sector_count.to_bytes(4, "little")


def build_ebr_sector(logical_abs_start: int, logical_size: int,
                     ebr_abs_lba: int, ext_base_abs_lba: int,
                     next_ebr_abs_lba: Optional[int],
                     next_chain_size: int) -> bytes:
    """Build a 512-byte Extended Boot Record.

    EBR layout:
      - slot 0 at 0x1BE: the logical partition living behind THIS EBR.
        Its start-LBA field is RELATIVE to this EBR's own absolute LBA.
      - slot 1 at 0x1CE: pointer to the NEXT EBR in the chain, if any.
        Its start-LBA field is RELATIVE to the extended container's base
        (= the absolute LBA of the first EBR).
      - slots 2/3 left empty.
      - 0x55AA signature at byte 510.

    If `next_ebr_abs_lba` is None this is the last EBR in the chain and
    slot 1 is left zero.
    """
    buf = bytearray(SECTOR_SIZE)
    # Slot 0: the logical partition right behind this EBR.
    write_mbr_entry(buf, MBR_P0_OFFSET, boot=0x00, part_type=MBR_TYPE_FAT16,
                    start_lba=logical_abs_start, sector_count=logical_size,
                    rel_start_lba=logical_abs_start - ebr_abs_lba)
    # Slot 1: link to the next EBR, if the chain continues.
    if next_ebr_abs_lba is not None:
        write_mbr_entry(buf, MBR_P0_OFFSET + 16, boot=0x00,
                        part_type=MBR_TYPE_EXTENDED,
                        start_lba=next_ebr_abs_lba,
                        sector_count=next_chain_size,
                        rel_start_lba=next_ebr_abs_lba - ext_base_abs_lba)
    buf[MBR_SIGNATURE_OFFSET:MBR_SIGNATURE_OFFSET + 2] = MBR_SIGNATURE
    return bytes(buf)


def xgm_container_bounds(plan: "ImagePlan") -> tuple:
    """(start_lba, size_sectors) for the AHDI XGM chain region: from the
    first sub-descriptor's LBA through the end of the last logical."""
    if not plan.has_extended:
        raise ValueError("plan has no XGM chain")
    first_logical = plan.partitions[plan.primary_count]
    last_logical = plan.partitions[-1]
    start = first_logical.ebr_lba
    end = last_logical.start_lba + last_logical.size_sectors
    return start, end - start


def build_root_sector_ahdi(plan: "ImagePlan") -> bytes:
    """Pure AHDI root sector: no MBR signature. Slots 0..primary_count-1
    hold direct partitions. When the plan has an XGM chain, the next slot
    (slot 3 in the N>4 case) becomes the XGM link pointing at the first
    sub-descriptor; its start/size describe the whole chain region.

    AHDI IDs: 'GEM' for partitions ≤16 MB, 'BGM' otherwise."""
    buf = bytearray(SECTOR_SIZE)
    ahdi_offsets = [AHDI_SLOT0_OFFSET, AHDI_SLOT1_OFFSET,
                    AHDI_SLOT2_OFFSET, AHDI_SLOT3_OFFSET]

    for i in range(plan.primary_count):
        part = plan.partitions[i]
        flag = AHDI_FLAG_EXISTENT | (AHDI_FLAG_BOOTABLE if i == 0 else 0)
        write_ahdi_entry(buf, ahdi_offsets[i], flag,
                         ahdi_partition_id(part.size_mb, plan.strict_tos,
                                           is_first=(i == 0)),
                         part.start_lba, part.size_sectors)

    if plan.has_extended:
        xgm_start, xgm_size = xgm_container_bounds(plan)
        write_ahdi_entry(buf, ahdi_offsets[plan.primary_count],
                         AHDI_FLAG_EXISTENT, b"XGM",
                         xgm_start, xgm_size)

    # NOTE: intentionally no 0x55AA signature (this is a pure-AHDI image).
    return bytes(buf)


def build_xgm_descriptor_sector(logical_abs_start: int, logical_size: int,
                                logical_ident: bytes,
                                desc_abs_lba: int, xgm_base_abs_lba: int,
                                next_desc_abs_lba: Optional[int],
                                next_chain_size: int) -> bytes:
    """Build a 512-byte AHDI XGM sub-descriptor sector.

    Layout (mirrors the ICD / Atari native convention we reverse-
    engineered from working images):
      - AHDI slot 0 at 0x1C6: the logical partition behind this descriptor.
        Its start is RELATIVE to this descriptor's own absolute LBA (so
        it's typically just '1' -- the partition sits one sector after
        the descriptor).
      - AHDI slot 1 at 0x1D2: link to the next XGM sub-descriptor. Its
        start is RELATIVE to the XGM chain base (the absolute LBA of the
        first sub-descriptor). Omit when the chain ends.
      - Slots 2, 3 left empty.
      - No MBR signature.
    """
    buf = bytearray(SECTOR_SIZE)
    # Slot 0: logical partition.
    rel_start = logical_abs_start - desc_abs_lba
    write_ahdi_entry(buf, AHDI_SLOT0_OFFSET,
                     AHDI_FLAG_EXISTENT, logical_ident,
                     rel_start, logical_size)
    # Slot 1: link to next descriptor (if any).
    if next_desc_abs_lba is not None:
        write_ahdi_entry(buf, AHDI_SLOT1_OFFSET,
                         AHDI_FLAG_EXISTENT, b"XGM",
                         next_desc_abs_lba - xgm_base_abs_lba,
                         next_chain_size)
    return bytes(buf)


def extended_container_size(partitions: List["Partition"]) -> int:
    """Total physical sectors spanned by partitions[1:] together with their
    preceding EBR sectors. = sum(partition sizes) + N-1 EBR sectors."""
    if len(partitions) < 2:
        return 0
    last = partitions[-1]
    first_ebr = partitions[1].ebr_lba
    return (last.start_lba + last.size_sectors) - first_ebr


def extended_container_bounds(plan: "ImagePlan") -> tuple:
    """(start_lba, size_sectors) for the MBR extended container spanning
    every logical partition in `plan`. Undefined when plan.has_extended is
    False."""
    if not plan.has_extended:
        raise ValueError("plan has no extended container")
    first_logical = plan.partitions[plan.primary_count]
    last_logical = plan.partitions[-1]
    start = first_logical.ebr_lba
    end = last_logical.start_lba + last_logical.size_sectors
    return start, end - start


def build_root_sector_ppdriver(plan: "ImagePlan") -> bytes:
    """PPera TOS&DOS root sector.

    Primary partitions (up to 4) occupy MBR slots 0..primary_count-1. When
    more partitions were requested, the next slot is an extended container
    (type 0x0F) covering the logical partitions in the EBR chain. The dual
    BPB per partition lives inside the partition itself, not here."""
    buf = bytearray(SECTOR_SIZE)

    for i in range(plan.primary_count):
        part = plan.partitions[i]
        boot = 0x80 if i == 0 else 0x00
        write_mbr_entry(buf, MBR_P0_OFFSET + i * 16,
                        boot=boot, part_type=MBR_TYPE_FAT16,
                        start_lba=part.start_lba,
                        sector_count=part.size_sectors)

    if plan.has_extended:
        ext_start, ext_size = extended_container_bounds(plan)
        write_mbr_entry(buf, MBR_P0_OFFSET + plan.primary_count * 16,
                        boot=0x00, part_type=MBR_TYPE_EXTENDED,
                        start_lba=ext_start, sector_count=ext_size)

    buf[MBR_SIGNATURE_OFFSET:MBR_SIGNATURE_OFFSET + 2] = MBR_SIGNATURE
    return bytes(buf)


def build_root_sector_hddriver(plan: "ImagePlan") -> bytes:
    """HDDRIVER TOS&DOS root sector.

    HDDRIVER convention: ONE primary (MBR P0) plus, when needed, an MBR
    extended container (next slot). AHDI slot 2 holds the TOS-view overlap
    marker for the primary partition -- one AHDI marker is all the
    emulator's detector needs to classify the image as HDDRIVER."""
    buf = bytearray(SECTOR_SIZE)

    # The one primary partition (always present).
    p0 = plan.partitions[0]
    write_mbr_entry(buf, MBR_P0_OFFSET, boot=0x80, part_type=MBR_TYPE_FAT16,
                    start_lba=p0.start_lba, sector_count=p0.size_sectors)

    if plan.has_extended:
        ext_start, ext_size = extended_container_bounds(plan)
        write_mbr_entry(buf, MBR_P0_OFFSET + 16,
                        boot=0x00, part_type=MBR_TYPE_EXTENDED,
                        start_lba=ext_start, sector_count=ext_size)

    # AHDI slot 2 marker (TOS view of the primary partition). Written AFTER
    # the MBR entries so the overlap bytes at 0x1DE win.
    write_ahdi_entry(buf, AHDI_SLOT2_OFFSET,
                     AHDI_FLAG_EXISTENT | AHDI_FLAG_BOOTABLE,
                     ahdi_partition_id(p0.size_mb),
                     p0.start_lba + 1,
                     p0.size_sectors - 1)

    buf[MBR_SIGNATURE_OFFSET:MBR_SIGNATURE_OFFSET + 2] = MBR_SIGNATURE
    return bytes(buf)


# --------------------------------------------------------------------------
# TOS BPB synthesis (for PPDRIVER / HDDRIVER hybrid images)
# --------------------------------------------------------------------------

def synthesize_tos_bpb_from_dos512(dos_bpb: bytes, tos_bps: int,
                                   oem: Optional[bytes] = None) -> bytes:
    """Build a TOS companion BPB given the real PPDRIVER / HDDRIVER dual-BPB
    layout.

    The DOS BPB is the output of `mkfs.vfat -S 512 -s (2*ratio) -R (ratio+1) -a`
    (see run_mkfs_hybrid_dos). It uses 512-byte logical sectors so macOS
    msdosfs can mount it. The TOS companion uses `tos_bps` (>=1024) with the
    same physical cluster size, same physical FAT size, same root entries,
    and res=1 -- so both views land on the same physical FAT/root/data
    LBAs.

    Constraints enforced (raise ValueError otherwise):
      - DOS res == ratio + 1       (where ratio = tos_bps / 512)
      - DOS spc == 2 * ratio       (so cluster_size is the same in bytes)
      - DOS spfat % ratio == 0     (so TOS spfat is a clean integer)
    """
    if len(dos_bpb) != SECTOR_SIZE:
        raise ValueError("DOS BPB must be a 512-byte boot sector")
    if tos_bps < 1024 or (tos_bps & (tos_bps - 1)) != 0:
        raise ValueError("tos_bps must be a power of two >= 1024")

    ratio = tos_bps // SECTOR_SIZE
    dos_bps = int.from_bytes(dos_bpb[BPB_BYTES_PER_SEC_OFFSET:
                                     BPB_BYTES_PER_SEC_OFFSET + 2], "little")
    dos_spc = dos_bpb[BPB_SEC_PER_CLUS_OFFSET]
    dos_res = int.from_bytes(dos_bpb[BPB_RESERVED_OFFSET:
                                     BPB_RESERVED_OFFSET + 2], "little")
    dos_fats = dos_bpb[BPB_FAT_COUNT_OFFSET]
    dos_root = int.from_bytes(dos_bpb[BPB_ROOT_ENTRIES_OFFSET:
                                      BPB_ROOT_ENTRIES_OFFSET + 2], "little")
    dos_tot16 = int.from_bytes(dos_bpb[BPB_TOTAL_SEC16_OFFSET:
                                       BPB_TOTAL_SEC16_OFFSET + 2], "little")
    dos_spfat = int.from_bytes(dos_bpb[BPB_SEC_PER_FAT_OFFSET:
                                       BPB_SEC_PER_FAT_OFFSET + 2], "little")
    dos_tot32 = int.from_bytes(dos_bpb[32:36], "little")
    dos_total = dos_tot16 if dos_tot16 != 0 else dos_tot32

    if dos_bps != SECTOR_SIZE:
        raise ValueError(f"dual-BPB expected DOS bps=512, got {dos_bps}")
    if dos_res != ratio + 1:
        raise ValueError(
            f"dual-BPB expected DOS reserved={ratio + 1}, got {dos_res}")
    if dos_spc != 2 * ratio:
        raise ValueError(
            f"dual-BPB expected DOS spc={2 * ratio}, got {dos_spc}")
    if (dos_spfat % ratio) != 0:
        raise ValueError(
            f"DOS spfat={dos_spfat} does not divide evenly by ratio={ratio}")

    tos_spc = 2
    tos_res = 1
    tos_spfat = dos_spfat // ratio
    tos_total = dos_total // ratio

    # Build a fresh 512-byte TOS BPB. Start from the DOS one so OEM/jmp/etc
    # come through, then overwrite the numeric fields.
    tos = bytearray(dos_bpb)
    tos[BPB_BYTES_PER_SEC_OFFSET:BPB_BYTES_PER_SEC_OFFSET + 2] = \
        tos_bps.to_bytes(2, "little")
    tos[BPB_SEC_PER_CLUS_OFFSET] = tos_spc
    tos[BPB_RESERVED_OFFSET:BPB_RESERVED_OFFSET + 2] = \
        tos_res.to_bytes(2, "little")
    tos[BPB_FAT_COUNT_OFFSET] = dos_fats
    tos[BPB_ROOT_ENTRIES_OFFSET:BPB_ROOT_ENTRIES_OFFSET + 2] = \
        dos_root.to_bytes(2, "little")
    # tot16 holds the TOS total when it fits (it always does thanks to the
    # Hatari doubling rule that keeps cluster count <= 32765). tot32 is
    # cleared for clarity.
    if tos_total < 0x10000:
        tos[BPB_TOTAL_SEC16_OFFSET:BPB_TOTAL_SEC16_OFFSET + 2] = \
            tos_total.to_bytes(2, "little")
        tos[32:36] = (0).to_bytes(4, "little")
    else:
        tos[BPB_TOTAL_SEC16_OFFSET:BPB_TOTAL_SEC16_OFFSET + 2] = \
            (0).to_bytes(2, "little")
        tos[32:36] = tos_total.to_bytes(4, "little")
    tos[BPB_SEC_PER_FAT_OFFSET:BPB_SEC_PER_FAT_OFFSET + 2] = \
        tos_spfat.to_bytes(2, "little")

    if oem is not None:
        if len(oem) > BPB_OEM_LENGTH:
            raise ValueError("OEM string must be <= 8 bytes")
        oem_bytes = oem.ljust(BPB_OEM_LENGTH, b" ")
        tos[BPB_OEM_OFFSET:BPB_OEM_OFFSET + BPB_OEM_LENGTH] = oem_bytes

    return bytes(tos)


def overwrite_oem(bpb: bytes, oem: bytes) -> bytes:
    if len(bpb) != SECTOR_SIZE:
        raise ValueError("boot sector must be 512 bytes")
    if len(oem) > BPB_OEM_LENGTH:
        raise ValueError("OEM string must be <= 8 bytes")
    out = bytearray(bpb)
    out[BPB_OEM_OFFSET:BPB_OEM_OFFSET + BPB_OEM_LENGTH] = oem.ljust(
        BPB_OEM_LENGTH, b" ")
    return bytes(out)


# --------------------------------------------------------------------------
# mkfs.vfat invocation
# --------------------------------------------------------------------------

def run_mkfs(mkfs_path: str, out_path: str,
             partition_sectors_512: int, sector_size: int,
             sectors_per_cluster: int, reserved_sectors: int,
             label: str, disable_fat_align: bool) -> None:
    """Invoke mkfs.vfat to produce a FAT16 image at `out_path` sized to
    exactly `partition_sectors_512 * 512` bytes, with the requested logical
    sector size, cluster size, and reserved-sector count.

    `disable_fat_align=True` passes `-a` so mkfs.vfat honors `-R` literally
    instead of rounding up to a cluster boundary. That's required for the
    dual-BPB hybrid layout where DOS_res must be exactly ratio+1."""
    total_bytes = partition_sectors_512 * SECTOR_SIZE
    # mkfs.vfat's -C takes a block count in 1 KiB units.
    blocks_1k = total_bytes // 1024

    # mkfs.vfat with -C refuses to overwrite an existing file. Our caller
    # uses mkstemp (which *creates* the file), so we must remove it here
    # before handing the path to mkfs.
    try:
        os.unlink(out_path)
    except FileNotFoundError:
        pass
    except OSError as e:
        raise RuntimeError(f"cannot clear {out_path}: {e}") from e

    cmd = [
        mkfs_path,
        "-F", "16",
        "-S", str(sector_size),           # logical sector size
        "-s", str(sectors_per_cluster),   # sectors per cluster
        "-R", str(reserved_sectors),      # reserved sectors (boot area)
        "-n", label,                       # volume label
    ]
    if disable_fat_align:
        cmd.append("-a")
    cmd.extend(["-C", out_path, str(blocks_1k)])
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        raise RuntimeError(f"mkfs tool vanished between check and run: "
                           f"{mkfs_path}")

    if result.returncode != 0:
        raise RuntimeError(
            f"mkfs.vfat failed (exit {result.returncode}).\n"
            f"  command : {' '.join(cmd)}\n"
            f"  stdout  : {result.stdout.strip()}\n"
            f"  stderr  : {result.stderr.strip()}\n"
        )


def read_sector(path: str, lba: int) -> bytes:
    with open(path, "rb") as f:
        f.seek(lba * SECTOR_SIZE)
        data = f.read(SECTOR_SIZE)
    if len(data) != SECTOR_SIZE:
        raise RuntimeError(f"short read at LBA {lba} of {path}")
    return data


def write_sector(path: str, lba: int, data: bytes) -> None:
    if len(data) != SECTOR_SIZE:
        raise ValueError("write_sector requires exactly 512 bytes")
    with open(path, "r+b") as f:
        f.seek(lba * SECTOR_SIZE)
        f.write(data)


def copy_file_into_image(src_path: str, dst_path: str,
                         dst_offset_bytes: int) -> None:
    """Stream the contents of src_path into dst_path at byte offset
    dst_offset_bytes, preserving surrounding bytes. 64 KiB chunks."""
    chunk_size = 64 * 1024
    with open(src_path, "rb") as src, open(dst_path, "r+b") as dst:
        dst.seek(dst_offset_bytes)
        while True:
            buf = src.read(chunk_size)
            if not buf:
                break
            dst.write(buf)


# --------------------------------------------------------------------------
# Image build orchestration
# --------------------------------------------------------------------------

@dataclass
class Partition:
    name: str
    size_mb: int
    # Derived during planning:
    size_sectors: int = 0           # 512-byte physical sectors
    start_lba: int = 0              # in 512-byte physical sectors
    ebr_lba: int = 0                # 0 = primary; otherwise LBA of this
                                    # partition's Extended Boot Record

    # Parameters passed to mkfs.vfat for the DOS-side FAT16 filesystem:
    dos_bps: int = 512              # bytesPerSec in DOS BPB
    dos_spc: int = 0                # sectors-per-cluster in DOS BPB
    dos_res: int = 0                # reserved-sector count in DOS BPB

    # Parameters for the synthesized TOS companion BPB (hybrid formats only;
    # ignored on AHDI).
    tos_bps: int = 0                # bytesPerSec for the TOS view


@dataclass
class ImagePlan:
    format_id: str
    image_path: str
    image_mb: int
    partitions: List[Partition] = field(default_factory=list)
    # True when the user asked for strict TOS < 1.04 compatibility. Only
    # meaningful on the AHDI path (the hybrid formats ignore it).
    strict_tos: bool = False
    # Derived:
    image_sectors: int = 0
    # Partition-table split (computed by plan_image):
    primary_count: int = 0     # partitions 0..primary_count-1 are primary
    has_extended: bool = False  # true if an MBR extended container is used


def format_max_partition_mb(format_id: str, strict_tos: bool) -> int:
    """Maximum allowed partition size (MB) for a given format and TOS-compat
    mode, across ALL slots. AHDI honors the TOS BGM cap (256/512); the
    hybrid formats use the FAT16 ceiling."""
    if format_id == FORMAT_AHDI:
        return (AHDI_MAX_PARTITION_MB_STRICT if strict_tos
                else AHDI_MAX_PARTITION_MB)
    return MAX_PARTITION_MB


def partition_cap_mb(format_id: str, strict_tos: bool,
                     partition_index: int) -> int:
    """Per-slot partition-size cap.

    On AHDI the first partition is the TOS boot partition: it must be a
    GEM entry, which TOS caps at 16 MB (< 1.04) or 32 MB (1.04+). Slots
    2..N can be GEM (within the same threshold) or BGM (up to 256/512).
    Hybrid formats have no per-slot distinction."""
    if format_id == FORMAT_AHDI and partition_index == 0:
        return AHDI_GEM_MAX_MB_STRICT if strict_tos else AHDI_GEM_MAX_MB
    return format_max_partition_mb(format_id, strict_tos)


def first_partition_start_lba(format_id: str) -> int:
    """AHDI leaves LBA 1 as padding (matches HDDRIVER tooling); hybrid
    layouts put the DOS BPB at LBA 1 directly."""
    return 2 if format_id == FORMAT_AHDI else 1


def partition_layout(format_id: str, n: int) -> dict:
    """Decide how many partitions land in MBR primary slots vs. the MBR
    extended chain.

    - AHDI: up to 4 primary (AHDI-table slots); no extended support in v1.
    - PPDRIVER: up to MAX_PRIMARY_PARTITIONS (4). When N > 4, fall back
      to the standard DOS convention of (cap-1) primaries plus one MBR
      extended container holding the rest as logicals (3 primary + N-3
      logical, up to 14 total).
    - HDDRIVER: one primary plus an extended container. Matches the
      real-world HDDRIVER TOS&DOS hybrid layout.

    Returns {'primary_count', 'has_extended', 'logical_count'}.
    """
    if n < 1:
        raise ValueError("at least one partition is required")
    if n > MAX_PARTITIONS[format_id]:
        raise ValueError(
            f"{format_id} supports at most {MAX_PARTITIONS[format_id]} "
            f"partitions (got {n})")

    if format_id == FORMAT_AHDI:
        if n <= MAX_PRIMARY_PARTITIONS[FORMAT_AHDI]:
            return {"primary_count": n, "has_extended": False,
                    "logical_count": 0}
        # Use (cap - 1) primaries + AHDI slot 3 as the XGM chain head.
        primary = MAX_PRIMARY_PARTITIONS[FORMAT_AHDI] - 1
        return {"primary_count": primary, "has_extended": True,
                "logical_count": n - primary}

    if format_id == FORMAT_PPDRIVER:
        if n <= MAX_PRIMARY_PARTITIONS[FORMAT_PPDRIVER]:
            # All primary; no extended container needed.
            return {"primary_count": n, "has_extended": False,
                    "logical_count": 0}
        # Use (cap - 1) primaries + 1 MBR extended container. With the 4-slot
        # MBR that's 3 primary + 1 extended, holding N-3 logicals.
        primary = MAX_PRIMARY_PARTITIONS[FORMAT_PPDRIVER] - 1
        return {"primary_count": primary, "has_extended": True,
                "logical_count": n - primary}

    if format_id == FORMAT_HDDRIVER:
        primary = MAX_PRIMARY_PARTITIONS[FORMAT_HDDRIVER]   # always 1
        if n == primary:
            return {"primary_count": primary, "has_extended": False,
                    "logical_count": 0}
        return {"primary_count": primary, "has_extended": True,
                "logical_count": n - primary}

    raise ValueError(f"unknown format {format_id!r}")


def predict_mkfs_spfat(partition_sectors_512: int, ratio: int) -> Optional[int]:
    """Predict the `spfat` value mkfs.vfat will pick for a FAT16 partition
    created with our fixed parameters (-F 16 -S 512 -s 2*ratio -R ratio+1).

    Mirrors dosfstools' inner fixed-point iteration: spfat depends on
    cluster count, which depends on spfat. The numbers below are small
    (the loop converges in 2-3 iterations in practice), so the bound is a
    safety net.

    Returns None when the partition is too small to host a valid FAT16."""
    spc = 2 * ratio
    res = ratio + 1
    root_sectors = 32  # 512 root entries * 32 bytes / 512-byte sector
    spfat = 1
    for _ in range(64):
        data = partition_sectors_512 - res - root_sectors - 2 * spfat
        if data <= 0:
            return None
        clusters = data // spc
        # dosfstools sizes the FAT for (clusters + 2) entries -- the "+2"
        # reserves FAT[0] and FAT[1] for the media descriptor and
        # end-of-chain markers. Without it the predictor lands one sector
        # short of mkfs.vfat's output at boundary sizes.
        fat_bytes = (clusters + 2) * 2
        spfat_needed = (fat_bytes + 511) // 512   # ceil to 512-byte sectors
        if spfat_needed == spfat:
            return spfat
        spfat = spfat_needed
    return None


def align_partition_for_hybrid(partition_sectors_512: int,
                               ratio: int) -> Optional[int]:
    """Round `partition_sectors_512` DOWN to the largest value <= the
    requested size where mkfs.vfat's predicted DOS `spfat` is an exact
    multiple of `ratio`. That's required so the synthesized TOS BPB has an
    integer `spfat = DOS_spfat / ratio` and both views see the same FAT
    table on disk.

    Returns None when no valid aligned size exists (shouldn't happen for
    the partition sizes we allow).
    Ratio == 1 returns the input unchanged (no TOS companion to keep in
    lockstep; AHDI path and tiny hybrid partitions both hit this)."""
    if ratio <= 1:
        return partition_sectors_512
    cluster_sectors = 2 * ratio           # 1 cluster, in 512-byte units
    current = (partition_sectors_512 // cluster_sectors) * cluster_sectors
    min_viable = cluster_sectors * 64     # ~32 KB floor; well below anything
                                          # the prompt accepts
    # Linear search in cluster-sized steps. spfat shifts by ~1 per 256-
    # cluster step, so worst-case ~256 iterations before we land on a
    # multiple of ratio. Guard with a hard cap just in case.
    for _ in range(4096):
        spfat = predict_mkfs_spfat(current, ratio)
        if spfat is not None and spfat % ratio == 0:
            return current
        current -= cluster_sectors
        if current < min_viable:
            return None
    return None


def compute_partition_geometry(format_id: str, size_sectors_512: int) -> dict:
    """For a given on-disk partition size (in physical 512-byte sectors),
    return the mkfs.vfat parameters and, for hybrid formats, the TOS
    companion's target bytesPerSec.

    AHDI: single BPB, Hatari-style logical sector (512..16384).

    PPDRIVER / HDDRIVER: DOS BPB with bps=512 so macOS msdosfs can mount,
    plus a TOS companion BPB with bps=tos_bps and matching physical
    FAT/root/data LBAs. The dual-BPB math requires DOS_res = ratio+1 and
    DOS_spc = 2*ratio, where ratio = tos_bps/512."""
    if format_id == FORMAT_AHDI:
        bps = choose_logical_sector_size(size_sectors_512)
        return {
            "dos_bps": bps,
            "dos_spc": SECTORS_PER_CLUSTER,
            "dos_res": 1,          # mkfs forces a reasonable value
            "tos_bps": 0,           # no TOS companion
            "disable_align": False,
        }

    # Hybrid: choose the TOS-side bps with the Hatari rule, then derive the
    # DOS-side parameters from it.
    tos_bps = choose_logical_sector_size(size_sectors_512)
    if tos_bps == SECTOR_SIZE:
        # For small partitions the Hatari rule keeps bps=512. The dual-BPB
        # trick collapses to a trivial case: DOS_res=2, TOS_res=1, ratio=1.
        return {
            "dos_bps": SECTOR_SIZE,
            "dos_spc": SECTORS_PER_CLUSTER,
            "dos_res": 2,
            "tos_bps": SECTOR_SIZE,
            "disable_align": True,
        }

    ratio = tos_bps // SECTOR_SIZE
    return {
        "dos_bps": SECTOR_SIZE,
        "dos_spc": 2 * ratio,       # cluster_bytes = 512*2*ratio = tos_bps*2
        "dos_res": ratio + 1,       # so (DOS_res - 1) / ratio == 1 == TOS_res
        "tos_bps": tos_bps,
        "disable_align": True,      # mkfs.vfat -a lets -R be honored literally
    }


def plan_image(format_id: str, image_path: str, image_mb: int,
               partitions: List[Partition],
               strict_tos: bool = False) -> ImagePlan:
    if not partitions:
        raise ValueError("at least one partition is required")

    plan = ImagePlan(
        format_id=format_id,
        image_path=image_path,
        image_mb=image_mb,
        partitions=partitions,
        strict_tos=strict_tos,
    )
    plan.image_sectors = mb_to_sectors_512(image_mb)

    # AHDI-only: enforce the TOS per-slot caps. Slot 0 is the boot (GEM)
    # partition with a tighter 16/32 MB cap; slots 2..N follow the BGM
    # 256/512 MB cap. Hybrid formats have no per-slot distinction.
    if format_id == FORMAT_AHDI:
        mode_label = "TOS < 1.04" if strict_tos else "TOS 1.04+"
        for i, part in enumerate(partitions):
            cap_mb = partition_cap_mb(format_id, strict_tos, i)
            if part.size_mb > cap_mb:
                kind = "boot (GEM)" if i == 0 else "BGM"
                raise ValueError(
                    f"partition {part.name!r} is {part.size_mb} MB but "
                    f"the {mode_label} {kind} cap at slot {i + 1} is "
                    f"{cap_mb} MB; lower the size or disable strict mode")

    # Decide the MBR primary vs. extended-chain split. Partitions with index
    # < primary_count land directly in MBR slots; partitions at index >=
    # primary_count become logicals in an MBR extended container and each
    # gets a 1-sector EBR sector placed immediately in front of it.
    layout = partition_layout(format_id, len(partitions))
    plan.primary_count = layout["primary_count"]
    plan.has_extended = layout["has_extended"]

    next_lba = first_partition_start_lba(format_id)
    for i, part in enumerate(partitions):
        is_logical = i >= plan.primary_count
        if is_logical:
            part.ebr_lba = next_lba
            next_lba += 1
        else:
            part.ebr_lba = 0
        part.size_sectors = mb_to_sectors_512(part.size_mb)
        geom = compute_partition_geometry(format_id, part.size_sectors)

        # When ratio > 1 (hybrid TOS&DOS), the DOS spfat picked by
        # mkfs.vfat must be divisible by ratio so the synthesized TOS BPB
        # has an integer spfat. Round the partition size down to the
        # nearest cluster-aligned value that satisfies this.
        ratio = geom["tos_bps"] // SECTOR_SIZE if geom["tos_bps"] else 1
        if format_id in (FORMAT_PPDRIVER, FORMAT_HDDRIVER) and ratio > 1:
            aligned = align_partition_for_hybrid(part.size_sectors, ratio)
            if aligned is None:
                raise ValueError(
                    f"partition {part.name!r} ({part.size_mb} MB) cannot be "
                    f"aligned for ratio={ratio}; try a different size")
            if aligned != part.size_sectors:
                # Update reported size to what will actually be created.
                part.size_sectors = aligned
                part.size_mb = (aligned * SECTOR_SIZE) // MIB

        part.dos_bps = geom["dos_bps"]
        part.dos_spc = geom["dos_spc"]
        part.dos_res = geom["dos_res"]
        part.tos_bps = geom["tos_bps"]
        part.start_lba = next_lba
        next_lba = part.start_lba + part.size_sectors

    # Silently bump the image size if rounding left us short by a few
    # sectors; the overhead is well under 1 KB so it doesn't surprise users.
    tail_needed = next_lba
    if tail_needed > plan.image_sectors:
        plan.image_sectors = tail_needed

    return plan


def ahdi_partition_id(partition_mb: int, strict_tos: bool = False,
                      is_first: bool = False) -> bytes:
    """Pick the AHDI partition-id bytes for a partition of `partition_mb` MB.

    The first AHDI partition is the TOS boot partition and must always be
    GEM (TOS boots only from GEM entries). `is_first=True` forces GEM
    regardless of size. Later partitions use the usual threshold: GEM when
    the partition is <= the TOS-version GEM cap, BGM otherwise.

    The GEM/BGM threshold differs between TOS versions: original TOS
    (< 1.04) accepts GEM only up to 16 MB, while TOS 1.04+ extends that to
    32 MB. `strict_tos=True` requests the conservative pre-1.04 rule."""
    if is_first:
        return b"GEM"
    threshold = AHDI_GEM_MAX_MB_STRICT if strict_tos else AHDI_GEM_MAX_MB
    return b"GEM" if partition_mb <= threshold else b"BGM"


def build_image(plan: ImagePlan, mkfs_path: str) -> None:
    # Step 1: allocate the image file (sparse where possible).
    image_bytes = plan.image_sectors * SECTOR_SIZE
    try:
        with open(plan.image_path, "wb") as f:
            f.truncate(image_bytes)
    except OSError as e:
        raise RuntimeError(f"cannot create {plan.image_path}: {e}") from e

    # Step 2: for each partition, run mkfs.vfat into a temp file and splice
    # the bytes into the main image at the partition's physical offset.
    for part in plan.partitions:
        tmp_fd, tmp_path = tempfile.mkstemp(prefix="atari_hd_",
                                            suffix=".fatpart")
        os.close(tmp_fd)
        try:
            run_mkfs(
                mkfs_path=mkfs_path,
                out_path=tmp_path,
                partition_sectors_512=part.size_sectors,
                sector_size=part.dos_bps,
                sectors_per_cluster=part.dos_spc,
                reserved_sectors=part.dos_res,
                label=part.name,
                disable_fat_align=(plan.format_id != FORMAT_AHDI),
            )
            partition_offset_bytes = part.start_lba * SECTOR_SIZE
            copy_file_into_image(tmp_path, plan.image_path,
                                 partition_offset_bytes)
        finally:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass

    # Step 3: for hybrid formats, synthesize a TOS BPB at each partition's
    # firstLBA+1 (and stamp PPDRIVER's OEM on both BPBs when applicable).
    # Sector size math is handled entirely by synthesize_tos_bpb_from_dos512;
    # the DOS BPB mkfs just wrote has bps=512, spc=2*ratio, res=ratio+1 so
    # the TOS companion's FAT/root/data LBAs align physically.
    if plan.format_id in (FORMAT_PPDRIVER, FORMAT_HDDRIVER):
        oem = b"PPGDODBC" if plan.format_id == FORMAT_PPDRIVER else None
        for part in plan.partitions:
            dos_bpb = read_sector(plan.image_path, part.start_lba)
            if oem is not None:
                # PPDRIVER stamps its OEM on BOTH the DOS and TOS BPBs.
                dos_bpb = overwrite_oem(dos_bpb, oem)
                write_sector(plan.image_path, part.start_lba, dos_bpb)
            tos_bpb = synthesize_tos_bpb_from_dos512(
                dos_bpb, tos_bps=part.tos_bps, oem=oem)
            write_sector(plan.image_path, part.start_lba + 1, tos_bpb)

    # Step 4: write the root sector (sector 0) describing all partitions.
    if plan.format_id == FORMAT_AHDI:
        root = build_root_sector_ahdi(plan)
    elif plan.format_id == FORMAT_PPDRIVER:
        root = build_root_sector_ppdriver(plan)
    elif plan.format_id == FORMAT_HDDRIVER:
        root = build_root_sector_hddriver(plan)
    else:
        raise ValueError(f"unknown format {plan.format_id!r}")
    write_sector(plan.image_path, 0, root)

    # Step 5: write the extended-chain descriptors for logical partitions.
    # PPERA / HDDRIVE use MBR EBR chains (type 0x0F); AHDI uses its native
    # AHDI XGM chain. Each descriptor's first slot references the logical
    # partition right behind it (relative to the descriptor's own LBA);
    # the next-link slot references the next descriptor (relative to the
    # chain's base = the first descriptor's LBA).
    if plan.has_extended:
        logicals = plan.partitions[plan.primary_count:]
        chain_base = logicals[0].ebr_lba
        for i, part in enumerate(logicals):
            if i + 1 < len(logicals):
                next_part = logicals[i + 1]
                next_desc_abs = next_part.ebr_lba
                # "count" of the next-link entry: the size of the next
                # sub-region (next descriptor + its partition). Works the
                # same for MBR EBR and AHDI XGM conventions.
                next_chain_size = next_part.size_sectors + 1
            else:
                next_desc_abs = None
                next_chain_size = 0

            if plan.format_id == FORMAT_AHDI:
                # Logicals in the XGM chain are never the boot partition.
                desc = build_xgm_descriptor_sector(
                    logical_abs_start=part.start_lba,
                    logical_size=part.size_sectors,
                    logical_ident=ahdi_partition_id(part.size_mb,
                                                    plan.strict_tos,
                                                    is_first=False),
                    desc_abs_lba=part.ebr_lba,
                    xgm_base_abs_lba=chain_base,
                    next_desc_abs_lba=next_desc_abs,
                    next_chain_size=next_chain_size,
                )
            else:
                desc = build_ebr_sector(
                    logical_abs_start=part.start_lba,
                    logical_size=part.size_sectors,
                    ebr_abs_lba=part.ebr_lba,
                    ext_base_abs_lba=chain_base,
                    next_ebr_abs_lba=next_desc_abs,
                    next_chain_size=next_chain_size,
                )
            write_sector(plan.image_path, part.ebr_lba, desc)


# --------------------------------------------------------------------------
# Interactive prompts
# --------------------------------------------------------------------------

def ask(prompt_text: str, default: Optional[str] = None,
        allow_blank: bool = False) -> str:
    while True:
        hint = f" [{default}]" if default else ""
        raw = input(f"{prompt_text}{hint}: ").strip()
        if not raw:
            if default is not None:
                return default
            if allow_blank:
                return ""
            print("  please enter a value.")
            continue
        return raw


def ask_int(prompt_text: str, default: Optional[int] = None,
            minimum: Optional[int] = None,
            maximum: Optional[int] = None) -> int:
    while True:
        raw = ask(prompt_text, default=str(default) if default is not None
                  else None)
        try:
            value = int(raw)
        except ValueError:
            print("  not a number.")
            continue
        if minimum is not None and value < minimum:
            print(f"  must be >= {minimum}.")
            continue
        if maximum is not None and value > maximum:
            print(f"  must be <= {maximum}.")
            continue
        return value


def ask_yes_no(prompt_text: str, default: bool = False) -> bool:
    hint = "[Y/n]" if default else "[y/N]"
    while True:
        raw = input(f"{prompt_text} {hint}: ").strip().lower()
        if not raw:
            return default
        if raw in ("y", "yes"):
            return True
        if raw in ("n", "no"):
            return False
        print("  please answer y or n.")


def ask_explicit_yes(prompt_text: str) -> bool:
    """Require the literal string 'YES' to continue. Used for the 16384
    sector-size warning."""
    raw = input(f"{prompt_text}: ").strip()
    return raw == "YES"


def ask_tos_compat() -> bool:
    """Ask whether to enforce TOS < 1.04 strict AHDI limits. Defaults to
    No (= target TOS 1.04 or later)."""
    print()
    print("TOS < 1.04 was the original TOS shipped with early machines")
    print("(520ST, 1040ST, Mega ST). It caps AHDI partitions tighter:")
    print("   - GEM <= 16 MB   (vs 32 MB on TOS 1.04+)")
    print("   - BGM <= 256 MB  (vs 512 MB on TOS 1.04+)")
    return ask_yes_no(
        "Force compatibility with TOS < 1.04 "
        "(original 520ST / 1040ST / Mega ST)?",
        default=False)


def ask_format() -> str:
    print()
    print("Select the on-disk layout:")
    print("  1) AHDI     - native Atari (no MBR); AHDI partition table.")
    print("  2) PPDRIVER - PPera TOS&DOS hybrid (MBR + dual BPB).")
    print("  3) HDDRIVER - HDDRIVER TOS&DOS hybrid (MBR + AHDI overlap).")
    while True:
        raw = ask("Choice (1/2/3)", default="1")
        if raw == "1":
            return FORMAT_AHDI
        if raw == "2":
            return FORMAT_PPDRIVER
        if raw == "3":
            return FORMAT_HDDRIVER
        print("  enter 1, 2, or 3.")


def ask_filename() -> str:
    while True:
        path = ask("Output image filename",
                   default="atari_hd.img")
        if os.path.exists(path):
            if ask_yes_no(f"'{path}' already exists. Overwrite?",
                          default=False):
                try:
                    os.unlink(path)
                except OSError as e:
                    print(f"  cannot remove existing file: {e}")
                    continue
                return path
            continue
        return path


def ask_partition_name(default: str = "ATARI") -> str:
    while True:
        raw = ask("Partition / volume label (<=11 ASCII chars)",
                  default=default)
        name = raw.upper()
        if len(name) > 11:
            print("  too long (max 11 characters).")
            continue
        if not all(32 <= ord(c) < 127 for c in name):
            print("  printable ASCII only.")
            continue
        return name


# --------------------------------------------------------------------------
# Pretty printers
# --------------------------------------------------------------------------

def format_partition_table_label(format_id: str) -> str:
    return {
        FORMAT_AHDI: "AHDI (native Atari, no MBR)",
        FORMAT_PPDRIVER: "MBR + PPera TOS&DOS dual BPB",
        FORMAT_HDDRIVER: "MBR + HDDRIVER TOS&DOS dual BPB (AHDI overlap @ 0x1DE)",
    }[format_id]


def print_summary(plan: ImagePlan) -> None:
    print()
    print("=" * 72)
    print("IMAGE PLAN")
    print("=" * 72)
    print(f"  File       : {plan.image_path}")
    print(f"  Layout     : {format_partition_table_label(plan.format_id)}")
    if plan.format_id == FORMAT_AHDI:
        mode = "TOS < 1.04 (strict)" if plan.strict_tos else "TOS 1.04+"
        print(f"  TOS compat : {mode}")
    print(f"  Image size : {plan.image_mb} MB "
          f"({plan.image_sectors} x 512-byte sectors)")
    print(f"  Partitions : {len(plan.partitions)}")
    print()
    header = (f"   {'#':>2}  {'name':<11}  {'size MB':>8}  "
              f"{'sectors':>10}  {'startLBA':>10}  {'DOS bps':>8}  "
              f"{'TOS bps':>8}  AHDI")
    print(header)
    print("   " + "-" * (len(header) - 3))
    for i, part in enumerate(plan.partitions, start=1):
        is_first = (i == 1) and (plan.format_id == FORMAT_AHDI)
        ahdi_tag = ahdi_partition_id(part.size_mb, plan.strict_tos,
                                     is_first=is_first).decode() \
            if plan.format_id in (FORMAT_AHDI, FORMAT_HDDRIVER) else "-"
        tos_tag = str(part.tos_bps) if part.tos_bps else "-"
        print(f"   {i:>2}  {part.name:<11}  {part.size_mb:>8}  "
              f"{part.size_sectors:>10}  {part.start_lba:>10}  "
              f"{part.dos_bps:>8}  {tos_tag:>8}  {ahdi_tag}")
    print("=" * 72)


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def prompt_partitions(format_id: str, image_mb: int,
                      strict_tos: bool = False) -> List[Partition]:
    """Interactively collect one or more Partition entries. Honors the
    per-format cap, the remaining-space budget, and the AHDI TOS-compat
    per-partition cap. The 16384-byte-sector warning fires per partition."""
    max_count = MAX_PARTITIONS[format_id]
    header_overhead_mb = 1   # conservative: 1 MB reserved for header/table
    available_mb = max(image_mb - header_overhead_mb, 0)
    partitions: List[Partition] = []

    while len(partitions) < max_count and available_mb >= MIN_PARTITION_MB:
        idx = len(partitions) + 1
        slot = len(partitions)    # 0-based index of the partition we're adding
        # Slot 0 on AHDI is the boot (GEM) partition with a tighter cap.
        slot_cap_mb = partition_cap_mb(format_id, strict_tos, slot)
        print()
        print(f"-- Partition {idx} / {max_count} "
              f"(remaining image space: {available_mb} MB) --")
        if format_id == FORMAT_AHDI and slot == 0:
            print(f"   (AHDI boot partition -- must be GEM, "
                  f"max {slot_cap_mb} MB)")
        # Upper bound is the lesser of remaining-space and the per-slot cap.
        partition_max_mb = min(available_mb, slot_cap_mb)
        default_size = min(128, partition_max_mb)
        size_mb = ask_int(
            f"Size (MB) [{MIN_PARTITION_MB}..{partition_max_mb}]",
            default=default_size,
            minimum=MIN_PARTITION_MB, maximum=partition_max_mb)

        size_sectors = mb_to_sectors_512(size_mb)
        atari_bps = choose_logical_sector_size(size_sectors)
        if atari_bps > SECTOR_SIZE_WARN_THRESHOLD:
            print()
            print("!! WARNING -- 16384-byte Atari-side logical sectors !!")
            print(
                "This partition will be readable on real Atari hardware with a\n"
                "driver that supports 16 KB sectors (e.g. HDDRIVER), but the\n"
                "SidecarTridge emulator has a 34 KB BCB rebind pool and CANNOT\n"
                "back 16 KB sectors. Read attempts past the first sector\n"
                "corrupt memory and typically produce 4 bus-error bombs.\n"
                "\n"
                "Type YES (capitals) to accept, anything else re-prompts.")
            if not ask_explicit_yes("Accept 16384-byte sectors for this partition"):
                print("  skipping; pick a smaller size.")
                continue
        elif atari_bps > 512 and format_id == FORMAT_AHDI:
            # AHDI is single-BPB, so the Atari-facing logical sector size is
            # ALSO the FAT filesystem's sector size -- and macOS msdosfs only
            # accepts 512-byte sectors. Flag it. (Hybrid formats dodge this
            # because their DOS view stays at 512 bytes even when the TOS
            # companion uses larger sectors.)
            print(f"  note: AHDI uses {atari_bps}-byte logical sectors here")
            print(
                "        (Hatari convention). This is required for Atari\n"
                "        compatibility above ~16 MB, but macOS cannot mount\n"
                "        such partitions -- msdosfs only supports 512-byte\n"
                "        logical sectors. Use Linux, mtools, or the emulator\n"
                "        itself to put files in. (PPDRIVER/HDDRIVER hybrid\n"
                "        layouts do NOT have this limitation.)")

        default_name = f"ATARI{idx}" if len(partitions) > 0 else "ATARI"
        name = ask_partition_name(default=default_name)

        partitions.append(Partition(name=name, size_mb=size_mb))
        available_mb -= size_mb

        if len(partitions) >= max_count:
            print(f"  partition cap for {format_id} reached ({max_count}).")
            break
        if available_mb < MIN_PARTITION_MB:
            print("  not enough space left for another partition.")
            break
        if not ask_yes_no(f"Add partition {len(partitions) + 1}?",
                          default=False):
            break

    if not partitions:
        raise RuntimeError("no partitions were defined — nothing to build")
    return partitions


def main() -> int:
    abort_if_windows()
    mkfs_path = find_mkfs_tool()

    print("SidecarTridge Atari HD image builder")
    print(f"  using: {mkfs_path}")

    image_path = ask_filename()
    format_id = ask_format()

    # TOS compatibility only matters for AHDI (the hybrid formats route
    # through the DOS view which doesn't care about TOS BGM limits).
    strict_tos = ask_tos_compat() if format_id == FORMAT_AHDI else False

    image_mb_cap = format_max_partition_mb(format_id, strict_tos) * \
        MAX_PARTITIONS[format_id]
    image_mb = ask_int(
        "Total image size (MB)", default=DEFAULT_IMAGE_MB[format_id],
        minimum=MIN_IMAGE_MB, maximum=image_mb_cap)

    partitions = prompt_partitions(format_id, image_mb, strict_tos)

    plan = plan_image(format_id, image_path, image_mb, partitions,
                      strict_tos=strict_tos)
    print_summary(plan)
    if not ask_yes_no("Proceed with image creation?", default=True):
        print("Aborted.")
        return 1

    try:
        build_image(plan, mkfs_path)
    except Exception as e:
        sys.stderr.write(f"ERROR: {e}\n")
        if os.path.exists(plan.image_path):
            try:
                os.unlink(plan.image_path)
            except OSError:
                pass
        return 2

    print()
    print(f"Done. Wrote {plan.image_path} "
          f"({plan.image_sectors * SECTOR_SIZE // MIB} MB, "
          f"{len(plan.partitions)} partition(s)).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
