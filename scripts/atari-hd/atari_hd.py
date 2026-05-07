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

The FAT16 filesystem is produced by a pure-Python writer (no external
binaries). The script also assembles the partition table, the hybrid
BPBs, and the outer image bytes.

Inspired by Hatari's tools/atari-hd-image.sh but rewritten in Python and
extended with the hybrid layouts used by real Atari hard disk drivers.

Usage: interactive. Run with no arguments.
"""

import hashlib
import os
import struct
import sys
import tempfile
from dataclasses import dataclass, field
from typing import List, Optional

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

SECTOR_SIZE = 512
MIB = 1024 * 1024

# Layout ids
FORMAT_AHDI = "AHDI"
FORMAT_PPDRIVER = "PPDRIVER"
FORMAT_HDDRIVER = "HDDRIVER"

# Limits
MIN_IMAGE_MB = 2              # absolute floor; mkfs.vfat refuses smaller
# FAT16 theoretical ceiling with 32 KB clusters; only reachable on
# Falcon TOS 4.x (which supports bps=16384/32768). On ST/STE/Mega ST/TT
# (TOS 1.04 - 3.x) the practical ceiling is the hybrid cap below.
MAX_PARTITION_MB = 2048
MIN_PARTITION_MB = 2

# AHDI partition-size caps imposed by the TOS kernel's BGM handling. These
# are only enforced on the AHDI path (the hybrid formats use their own
# caps below). "Strict" = TOS < 1.04 (520ST / 1040ST / Mega ST running the
# original TOS). Permissive = TOS 1.04 and later.
#
# The permissive cap was historically quoted as 512 MB (= 65536 logical
# sectors x 8192 bps) but TOS reads NSECTS as 16-bit unsigned, so the
# real ceiling is 65535 sectors -> 511.99 MB rounded down to 511. Real
# AHDI partitions of exactly 512 MB don't build in our pipeline (they
# trip the Hatari sector-doubling rule into bps=16384, which TOS
# 1.04 - 3.x doesn't support). Validated against the Atari Compendium
# notebook; see CLAUDE.md and HYBRID_MAX_PARTITION_MB below.
AHDI_MAX_PARTITION_MB_STRICT = 256   # TOS < 1.04 BGM cap
AHDI_MAX_PARTITION_MB = 511          # TOS 1.04+ BGM cap (was 512; off by one)

# Threshold for AHDI's GEM vs. BGM partition-id choice. <= threshold uses
# "GEM" (small partition); above uses "BGM" (big).
AHDI_GEM_MAX_MB_STRICT = 16          # TOS < 1.04 GEM cap
AHDI_GEM_MAX_MB = 32                 # TOS 1.04+ GEM cap

# Caps for the PPDRIVER / HDDRIVER dual-BPB hybrid layout. Both ends
# come from the TOS-side BPB, NOT the DOS view:
#   - MAX: TOS reads the partition's total-sectors-16 field (NSECTS) as
#     a 16-bit unsigned value (max 65535). At the maximum supported TOS
#     logical sector size of 8192 bytes (TOS 1.04 - 3.x; Falcon TOS 4.x
#     could go to 16384/32768 but isn't a target here), that's
#     65535 x 8192 = 511.99 MB, rounded down to 511 MB.
#   - MIN: the hybrid synthesis requires tos_bps >= 1024 (ratio >= 2).
#     The Hatari sector-doubling rule first reaches ratio=2 at
#     ~32 MB; below that, choose_logical_sector_size leaves bps=512
#     and synthesize_tos_bpb_from_dos512 rejects the plan.
# Validated against the Atari Compendium notebook; see CLAUDE.md.
HYBRID_MAX_PARTITION_MB = 511
HYBRID_MIN_PARTITION_MB = 32

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
# AHDI 3.0 'hd_siz' field: total disk size in 512-byte sectors,
# 4 bytes big-endian. Documented but optional; modern drivers
# (HDDRIVER, ICD Pro) ignore it. We populate it on pure-AHDI images
# for byte-level fidelity with real AHDI tooling. NOT applicable to
# the hybrid layouts (PPDRIVER / HDDRIVER) -- the MBR partition
# table at 0x1BE..0x1FD overlaps these bytes.
AHDI_HD_SIZ_OFFSET = 0x01C2

# MBR partition-table offsets
MBR_P0_OFFSET = 0x01BE
MBR_SIGNATURE_OFFSET = 510
# Disk signature (Windows NT-style 4-byte ID). Real PPDRIVER images
# stamp a random value here; HDDRIVER leaves it at zero. We seed
# PPDRIVER's deterministically from plan inputs (same hash-of-inputs
# approach as the FAT16 volume_id) so output stays reproducible.
MBR_DISK_SIGNATURE_OFFSET = 0x01B8
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

# CHS geometry baked into MBR partition entries. CHS is vestigial on
# AHDI disks (which are LBA-only) and only present in MBR slots for
# PC / DOS compatibility -- real Atari drivers ignore it. The 16x32
# pair is the Hatari atari-hd-image.sh tooling convention, not a TOS
# spec. Validated against the Atari Compendium notebook; see
# CLAUDE.md.
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

# MBR partition type bytes.
#
# FAT16 type: PPDRIVER and HDDRIVER both emit 0x06 (FAT16B / "BIGDOS";
# >= 32 MB) for their primary FAT16 partitions. Sub-32 MB partitions
# would conventionally be 0x04 (FAT16A); we don't generate those
# because the hybrid min cap is 32 MB. AHDI is unaffected (no MBR).
MBR_TYPE_FAT16 = 0x06
#
# Extended-container type: PPDRIVER uses 0x0F (Win95 LBA-addressable
# extended) to force LBA addressing on large disks. HDDRIVER uses
# 0x05 (CHS extended FAT16B). Real PPDRIVER and HDDRIVER images
# byte-mismatch on this single field; we honor each driver's choice.
# Validated against the Atari Compendium notebook; see CLAUDE.md.
MBR_TYPE_EXTENDED_LBA = 0x0F
MBR_TYPE_EXTENDED_CHS = 0x05
# Back-compat alias used by existing callers / tests; defaults to the
# LBA variant which is what build_ebr_sector also defaults to.
MBR_TYPE_EXTENDED = MBR_TYPE_EXTENDED_LBA
# Extended-container types the loader / walker recognises (either LBA
# or CHS) so we can read both PPDRIVER and HDDRIVER images.
MBR_EXTENDED_TYPES = (MBR_TYPE_EXTENDED_LBA, MBR_TYPE_EXTENDED_CHS)


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
                     next_chain_size: int,
                     extended_type: int = MBR_TYPE_EXTENDED_LBA) -> bytes:
    """Build a 512-byte Extended Boot Record.

    EBR layout:
      - slot 0 at 0x1BE: the logical partition living behind THIS EBR.
        Its start-LBA field is RELATIVE to this EBR's own absolute LBA.
      - slot 1 at 0x1CE: pointer to the NEXT EBR in the chain, if any.
        Its start-LBA field is RELATIVE to the extended container's base
        (= the absolute LBA of the first EBR).
      - slots 2/3 left empty.
      - 0x55AA signature at byte 510.

    `extended_type` controls slot 1's type byte for the next-EBR link.
    PPDRIVER uses 0x0F (LBA); HDDRIVER uses 0x05 (CHS). Default 0x0F
    matches PPDRIVER and the test fixtures from epic-002.

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
                        part_type=extended_type,
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

    # AHDI 3.0 'hd_siz' field: total disk sectors in 4 bytes big-endian.
    # Optional / informational; modern drivers ignore it but we write
    # it for byte-level fidelity with real AHDI tooling.
    buf[AHDI_HD_SIZ_OFFSET:AHDI_HD_SIZ_OFFSET + 4] = (
        plan.image_sectors).to_bytes(4, "big")

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
        # PPDRIVER uses LBA-addressable extended (0x0F) to force LBA
        # semantics on large disks; matches what real PPTOSDOS images
        # ship with.
        write_mbr_entry(buf, MBR_P0_OFFSET + plan.primary_count * 16,
                        boot=0x00, part_type=MBR_TYPE_EXTENDED_LBA,
                        start_lba=ext_start, sector_count=ext_size)

    # PPDRIVER stamps a 4-byte disk signature at MBR offset 0x1B8.
    # Real PPDRIVER images use a random value; we seed deterministically
    # from plan inputs so output is byte-stable across runs. HDDRIVER
    # leaves this field at zero (matched by build_root_sector_hddriver).
    buf[MBR_DISK_SIGNATURE_OFFSET:MBR_DISK_SIGNATURE_OFFSET + 4] = (
        _ppdriver_disk_signature(plan))

    buf[MBR_SIGNATURE_OFFSET:MBR_SIGNATURE_OFFSET + 2] = MBR_SIGNATURE
    return bytes(buf)


def _ppdriver_disk_signature(plan: "ImagePlan") -> bytes:
    """Deterministic 4-byte disk signature for PPDRIVER images. Same
    rationale as _fat16_seed_volume_id: same inputs -> same bytes,
    no wall-clock seeding, no host randomness. Inputs cover the
    image-shape choices that uniquely identify the plan."""
    seed = (
        os.path.basename(os.fspath(plan.image_path)).encode("utf-8",
                                                              errors="replace")
        + struct.pack("<II", plan.image_sectors & 0xFFFFFFFF,
                       len(plan.partitions))
    )
    digest = hashlib.sha256(seed).digest()
    return digest[:4]


def build_root_sector_hddriver(plan: "ImagePlan") -> bytes:
    """HDDRIVER TOS&DOS root sector.

    HDDRIVER convention: ONE primary (MBR P0) plus, when needed, an MBR
    extended container (next slot). AHDI slot 2 holds the TOS-view overlap
    marker for the primary partition -- one AHDI marker is all the
    emulator's detector needs to classify the image as HDDRIVER.

    The Atari Compendium describes HDDRIVER's hybrid trick as: place AHDI
    partition info into MBR slot 2 with an MBR `part_type` of 0 so PC OSes
    skip it, while the AHDI driver is told to look at 0x1DE specifically.
    We achieve the type-0 byte *implicitly*: the AHDI entry's start-LBA
    is stored big-endian at offset 0x1E2, which is exactly where MBR slot
    2's `part_type` byte lives. For LBAs below 16 M (8 GiB at 512 B per
    sector) the BE high byte is 0, so PC OSes correctly see no partition
    in slot 2. Above that range the implicit type byte starts taking
    non-zero values; not a concern within this tool's <= 2 GB envelope,
    but worth knowing if the plan ever grows."""
    buf = bytearray(SECTOR_SIZE)

    # The one primary partition (always present).
    p0 = plan.partitions[0]
    write_mbr_entry(buf, MBR_P0_OFFSET, boot=0x80, part_type=MBR_TYPE_FAT16,
                    start_lba=p0.start_lba, sector_count=p0.size_sectors)

    if plan.has_extended:
        ext_start, ext_size = extended_container_bounds(plan)
        # HDDRIVER uses CHS-extended FAT16B (0x05) for its container,
        # not the LBA variant -- matches the byte HDDRIVER itself
        # writes. NOTE: per the Atari Compendium, HDDRIVER's TOS&DOS
        # hybrid mode supports only ONE TOS-mountable partition per
        # drive (the primary in MBR P0; the AHDI overlap at 0x1DE
        # points only at it). Logical partitions in this EBR chain
        # are DOS-readable but **not** TOS-mountable; they live for
        # PC-side use of the same image.
        write_mbr_entry(buf, MBR_P0_OFFSET + 16,
                        boot=0x00, part_type=MBR_TYPE_EXTENDED_CHS,
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

    The DOS BPB is produced by format_fat16() with bps=512, spc=2*ratio,
    and res=ratio+1 -- 512-byte logical sectors so macOS msdosfs can mount
    it. The TOS companion uses `tos_bps` (>=1024) with the same physical
    cluster size, same physical FAT size, same root entries, and res=1 --
    so both views land on the same physical FAT/root/data LBAs.

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
# Pure-Python FAT16 formatter
# --------------------------------------------------------------------------
#
# Produces a freshly formatted FAT16 image using only the Python standard
# library, replacing the dependency on `mkfs.vfat` / `mkdosfs`. See
# scripts/atari-hd/epics/epic-001-pure-python-fat16/ for design notes.

# FAT16 cluster-count limits. The FAT12/FAT16/FAT32 boundary is encoded
# entirely in the cluster count (Microsoft FATGEN103.DOC, "Determination
# of FAT type when mounting the volume").
FAT16_MIN_CLUSTERS = 4085
FAT16_MAX_CLUSTERS = 65524

# Logical sector sizes the writer accepts.
FAT16_VALID_SECTOR_SIZES = (512, 1024, 2048, 4096, 8192)

# Fixed structural choices that match mkfs.vfat -F 16 defaults.
FAT16_ROOT_ENTRIES = 512
FAT16_DIR_ENTRY_BYTES = 32
FAT16_NUM_FATS = 2
FAT16_MEDIA_DESCRIPTOR = 0xF8
FAT16_SECTORS_PER_TRACK = 32
FAT16_NUM_HEADS = 64
FAT16_OEM_NAME = b"MSWIN4.1"
FAT16_FS_TYPE = b"FAT16   "
FAT16_BOOT_SIGNATURE = bytes((0x55, 0xAA))
FAT16_EXTENDED_BOOT_SIG = 0x29
FAT16_DRIVE_NUMBER = 0x80
FAT16_VOLUME_LABEL_ATTR = 0x08

# Boot-code stub fill: HLT (0xF4). mkfs.vfat ships its own x86 stub that
# prints "Non-system disk"; we never boot x86 from these images, so a
# one-byte HLT pattern is fine. The byte-parity harness in epic-001 /
# story 003 may mask this region if mkfs.vfat drift is observed.
FAT16_BOOT_CODE_FILL = 0xF4

# Boot-sector field offsets not covered by the BPB_* constants above.
FAT16_BS_MEDIA_OFFSET = 0x15
FAT16_BS_SPT_OFFSET = 0x18
FAT16_BS_HEADS_OFFSET = 0x1A
FAT16_BS_HIDDEN_OFFSET = 0x1C
FAT16_BS_TOTAL32_OFFSET = 0x20
FAT16_BS_DRIVE_OFFSET = 0x24
FAT16_BS_RESERVED1_OFFSET = 0x25
FAT16_BS_BOOTSIG_OFFSET = 0x26
FAT16_BS_VOLID_OFFSET = 0x27
FAT16_BS_LABEL_OFFSET = 0x2B
FAT16_BS_FSTYPE_OFFSET = 0x36
FAT16_BS_BOOTCODE_OFFSET = 0x3E
FAT16_BS_SIGNATURE_OFFSET = 0x1FE


def _fat16_normalize_label(label: str) -> bytes:
    """Encode a volume label per FAT16 short-name rules: ASCII upper-case,
    11 bytes, space-padded. Raises RuntimeError if the input is not
    representable."""
    try:
        upper = label.upper().encode("ascii")
    except UnicodeEncodeError:
        raise RuntimeError(f"volume label is not ASCII: {label!r}")
    if len(upper) > 11:
        raise RuntimeError(f"volume label too long: {label!r} (max 11 chars)")
    forbidden = set(b'*?<>|":+,./;=[]\\\x7f')
    for b in upper:
        if b < 0x20 or b in forbidden:
            raise RuntimeError(
                f"volume label contains illegal byte 0x{b:02X}: {label!r}")
    return upper


def _fat16_seed_volume_id(label: bytes, partition_sectors_512: int,
                          sector_size: int, sectors_per_cluster: int,
                          reserved_sectors: int) -> int:
    """Deterministic 32-bit volume serial seeded from the formatter inputs.
    Same inputs always produce the same output -- no wall-clock reads, no
    OS-dependent randomness. Story 008 of epic-002 is the canary for this
    contract."""
    seed = label.ljust(11, b" ") + struct.pack(
        "<IIII",
        partition_sectors_512 & 0xFFFFFFFF,
        sector_size,
        sectors_per_cluster,
        reserved_sectors,
    )
    digest = hashlib.sha256(seed).digest()
    return struct.unpack_from("<I", digest, 0)[0]


def _fat16_compute_spfat(partition_sectors_512: int, sector_size: int,
                         sectors_per_cluster: int,
                         reserved_sectors: int) -> int:
    """Compute sectors-per-FAT for an arbitrary (sector_size, spc, res)
    tuple, mirroring dosfstools' fixed-point iteration. For sector_size=512
    the output matches predict_mkfs_spfat(); the latter is the source of
    truth for the dual-BPB hybrid layout. Raises RuntimeError if the
    layout cannot be produced."""
    ratio = sector_size // SECTOR_SIZE
    partition_total_logical = partition_sectors_512 // ratio
    root_dir_bytes = FAT16_ROOT_ENTRIES * FAT16_DIR_ENTRY_BYTES
    root_dir_sectors = (root_dir_bytes + sector_size - 1) // sector_size
    spfat = 1
    for _ in range(64):
        overhead = (reserved_sectors
                    + FAT16_NUM_FATS * spfat
                    + root_dir_sectors)
        if overhead >= partition_total_logical:
            raise RuntimeError(
                f"FAT16 layout: reserved+FATs+root ({overhead}) exceeds "
                f"partition ({partition_total_logical} logical sectors)")
        data_logical = partition_total_logical - overhead
        clusters = data_logical // sectors_per_cluster
        # The "+2" reserves FAT[0] (media descriptor) and FAT[1] (EOF);
        # see predict_mkfs_spfat() for the rationale.
        fat_bytes = (clusters + 2) * 2
        spfat_needed = (fat_bytes + sector_size - 1) // sector_size
        if spfat_needed == spfat:
            return spfat
        spfat = spfat_needed
    raise RuntimeError("FAT16 spfat iteration failed to converge")


def _fat16_validate_and_compute_spfat(partition_sectors_512: int,
                                      sector_size: int,
                                      sectors_per_cluster: int,
                                      reserved_sectors: int) -> int:
    """Validate every FAT16 invariant the writer needs; return spfat."""
    if sector_size not in FAT16_VALID_SECTOR_SIZES:
        raise RuntimeError(
            f"sector_size {sector_size} not in {FAT16_VALID_SECTOR_SIZES}")
    if (sectors_per_cluster < 1 or sectors_per_cluster > 128
            or (sectors_per_cluster & (sectors_per_cluster - 1)) != 0):
        raise RuntimeError(
            f"sectors_per_cluster {sectors_per_cluster} must be a power of "
            "two between 1 and 128")
    if reserved_sectors < 1:
        raise RuntimeError(
            f"reserved_sectors {reserved_sectors} must be >= 1")
    if partition_sectors_512 < 1:
        raise RuntimeError(
            f"partition_sectors_512 {partition_sectors_512} must be > 0")
    ratio = sector_size // SECTOR_SIZE
    if partition_sectors_512 % ratio != 0:
        raise RuntimeError(
            f"partition_sectors_512 {partition_sectors_512} not divisible "
            f"by ratio {ratio} (sector_size={sector_size})")

    spfat = _fat16_compute_spfat(partition_sectors_512, sector_size,
                                 sectors_per_cluster, reserved_sectors)

    # Cluster-count check confirms we're in the FAT16 range. Has to come
    # after the iteration so it sees the final spfat.
    partition_total_logical = partition_sectors_512 // ratio
    root_dir_bytes = FAT16_ROOT_ENTRIES * FAT16_DIR_ENTRY_BYTES
    root_dir_sectors = (root_dir_bytes + sector_size - 1) // sector_size
    overhead = reserved_sectors + FAT16_NUM_FATS * spfat + root_dir_sectors
    data_logical = partition_total_logical - overhead
    clusters = data_logical // sectors_per_cluster
    if clusters < FAT16_MIN_CLUSTERS:
        raise RuntimeError(
            f"FAT16 requires >= {FAT16_MIN_CLUSTERS} clusters; got "
            f"{clusters} -- partition too small or sectors_per_cluster "
            "too large")
    if clusters > FAT16_MAX_CLUSTERS:
        raise RuntimeError(
            f"FAT16 supports at most {FAT16_MAX_CLUSTERS} clusters; got "
            f"{clusters} -- increase sectors_per_cluster")
    return spfat


def _fat16_build_boot_sector(sector_size: int, sectors_per_cluster: int,
                             reserved_sectors: int, spfat: int,
                             total_sectors_logical: int, label: bytes,
                             volume_id: int) -> bytes:
    """Assemble the 512-byte FAT16 boot sector. When sector_size > 512 the
    rest of logical sector 0 is zero-filled separately by the caller."""
    bs = bytearray(SECTOR_SIZE)
    bs[0:3] = b"\xEB\x3C\x90"  # jmp 0x3E ; nop
    bs[BPB_OEM_OFFSET:BPB_OEM_OFFSET + BPB_OEM_LENGTH] = FAT16_OEM_NAME

    struct.pack_into("<H", bs, BPB_BYTES_PER_SEC_OFFSET, sector_size)
    bs[BPB_SEC_PER_CLUS_OFFSET] = sectors_per_cluster
    struct.pack_into("<H", bs, BPB_RESERVED_OFFSET, reserved_sectors)
    bs[BPB_FAT_COUNT_OFFSET] = FAT16_NUM_FATS
    struct.pack_into("<H", bs, BPB_ROOT_ENTRIES_OFFSET, FAT16_ROOT_ENTRIES)
    if total_sectors_logical < 0x10000:
        struct.pack_into("<H", bs, BPB_TOTAL_SEC16_OFFSET,
                         total_sectors_logical)
        # FAT16_BS_TOTAL32_OFFSET stays at 0
    else:
        struct.pack_into("<H", bs, BPB_TOTAL_SEC16_OFFSET, 0)
        struct.pack_into("<I", bs, FAT16_BS_TOTAL32_OFFSET,
                         total_sectors_logical)
    bs[FAT16_BS_MEDIA_OFFSET] = FAT16_MEDIA_DESCRIPTOR
    struct.pack_into("<H", bs, BPB_SEC_PER_FAT_OFFSET, spfat)
    struct.pack_into("<H", bs, FAT16_BS_SPT_OFFSET, FAT16_SECTORS_PER_TRACK)
    struct.pack_into("<H", bs, FAT16_BS_HEADS_OFFSET, FAT16_NUM_HEADS)
    # FAT16_BS_HIDDEN_OFFSET stays at 0; partition offset lives in the
    # caller's partition table, not in this BPB.

    bs[FAT16_BS_DRIVE_OFFSET] = FAT16_DRIVE_NUMBER
    bs[FAT16_BS_RESERVED1_OFFSET] = 0
    bs[FAT16_BS_BOOTSIG_OFFSET] = FAT16_EXTENDED_BOOT_SIG
    struct.pack_into("<I", bs, FAT16_BS_VOLID_OFFSET,
                     volume_id & 0xFFFFFFFF)
    bs[FAT16_BS_LABEL_OFFSET:FAT16_BS_LABEL_OFFSET + 11] = \
        label.ljust(11, b" ")
    bs[FAT16_BS_FSTYPE_OFFSET:FAT16_BS_FSTYPE_OFFSET + 8] = FAT16_FS_TYPE

    bs[FAT16_BS_BOOTCODE_OFFSET:FAT16_BS_SIGNATURE_OFFSET] = \
        bytes((FAT16_BOOT_CODE_FILL,)) * \
        (FAT16_BS_SIGNATURE_OFFSET - FAT16_BS_BOOTCODE_OFFSET)
    bs[FAT16_BS_SIGNATURE_OFFSET:FAT16_BS_SIGNATURE_OFFSET + 2] = \
        FAT16_BOOT_SIGNATURE
    return bytes(bs)


def _fat16_volume_label_dir_entry(label: bytes) -> bytes:
    """The single 32-byte root-dir entry for the volume label. Date/time
    fields are zeroed for reproducibility (mkfs.vfat populates them from
    the wall clock; the byte-parity harness in story 003 will mask them)."""
    entry = bytearray(FAT16_DIR_ENTRY_BYTES)
    entry[0:11] = label.ljust(11, b" ")
    entry[11] = FAT16_VOLUME_LABEL_ATTR
    return bytes(entry)


def _fat16_write_zeros(f, n_bytes: int) -> None:
    """Stream n_bytes of zeros to f in 1 MiB chunks."""
    if n_bytes <= 0:
        return
    chunk_size = 1024 * 1024
    chunk = b"\x00" * min(n_bytes, chunk_size)
    while n_bytes >= len(chunk):
        f.write(chunk)
        n_bytes -= len(chunk)
    if n_bytes > 0:
        f.write(b"\x00" * n_bytes)


def format_fat16(out_path: str,
                 partition_sectors_512: int,
                 sector_size: int,
                 sectors_per_cluster: int,
                 reserved_sectors: int,
                 label: str,
                 disable_fat_align: bool) -> None:
    """Write a freshly formatted, empty FAT16 image to `out_path`.

    Reproduces the on-disk layout that mkfs.vfat -F 16 produces for the
    same inputs (modulo volume serial and creation timestamp; both are
    seeded deterministically from inputs to keep the output byte-stable
    across hosts and runs).

    Total file size is exactly partition_sectors_512 * 512 bytes.

    `disable_fat_align` is currently a no-op: this writer always honors
    `reserved_sectors` literally, which is what the dual-BPB hybrid layout
    requires (DOS_res = ratio + 1). The parameter is kept for forward
    compatibility -- a future single-BPB image type may want a different
    alignment policy; today, both branches behave identically.
    """
    _ = disable_fat_align  # see docstring; both branches behave identically

    label_bytes = _fat16_normalize_label(label)
    spfat = _fat16_validate_and_compute_spfat(
        partition_sectors_512, sector_size,
        sectors_per_cluster, reserved_sectors)

    ratio = sector_size // SECTOR_SIZE
    total_sectors_logical = partition_sectors_512 // ratio
    root_dir_bytes = FAT16_ROOT_ENTRIES * FAT16_DIR_ENTRY_BYTES
    root_dir_sectors = (root_dir_bytes + sector_size - 1) // sector_size
    volume_id = _fat16_seed_volume_id(
        label_bytes, partition_sectors_512, sector_size,
        sectors_per_cluster, reserved_sectors)

    boot_sector = _fat16_build_boot_sector(
        sector_size=sector_size,
        sectors_per_cluster=sectors_per_cluster,
        reserved_sectors=reserved_sectors,
        spfat=spfat,
        total_sectors_logical=total_sectors_logical,
        label=label_bytes,
        volume_id=volume_id,
    )

    # First sector of each FAT: F8 FF FF FF (FAT[0]=media descriptor,
    # FAT[1]=end-of-chain), remainder zero. Both FAT copies are identical.
    fat_first_buf = bytearray(sector_size)
    fat_first_buf[0:4] = bytes((FAT16_MEDIA_DESCRIPTOR, 0xFF, 0xFF, 0xFF))
    fat_first_sector = bytes(fat_first_buf)

    # First sector of the root directory: just the volume-label entry,
    # rest zero. Subsequent root-dir sectors are all zero.
    root_first_buf = bytearray(sector_size)
    root_first_buf[0:FAT16_DIR_ENTRY_BYTES] = \
        _fat16_volume_label_dir_entry(label_bytes)
    root_first_sector = bytes(root_first_buf)

    out_path_str = os.fspath(out_path)
    with open(out_path_str, "wb") as f:
        # Sector 0: 512-byte boot sector. If the logical sector is larger
        # than 512 bytes, zero-pad the rest of logical sector 0.
        f.write(boot_sector)
        if sector_size > SECTOR_SIZE:
            _fat16_write_zeros(f, sector_size - SECTOR_SIZE)

        # Reserved area: zero-fill sectors 1 .. reserved_sectors - 1.
        _fat16_write_zeros(f, (reserved_sectors - 1) * sector_size)

        # FATs: spfat sectors each, two byte-identical copies.
        for _ in range(FAT16_NUM_FATS):
            f.write(fat_first_sector)
            _fat16_write_zeros(f, (spfat - 1) * sector_size)

        # Root directory: root_dir_sectors sectors total.
        f.write(root_first_sector)
        _fat16_write_zeros(f, (root_dir_sectors - 1) * sector_size)

        # Data area: zero-fill the remainder of the partition.
        overhead_logical = (
            1                            # sector 0 (boot + reserved start)
            + (reserved_sectors - 1)     # rest of reserved
            + FAT16_NUM_FATS * spfat
            + root_dir_sectors)
        data_logical = total_sectors_logical - overhead_logical
        if data_logical < 0:
            raise RuntimeError(
                f"FAT16 layout overflow: overhead {overhead_logical} "
                f"exceeds total {total_sectors_logical}")
        _fat16_write_zeros(f, data_logical * sector_size)


def run_mkfs(out_path: str,
             partition_sectors_512: int, sector_size: int,
             sectors_per_cluster: int, reserved_sectors: int,
             label: str, disable_fat_align: bool) -> None:
    """Thin wrapper around format_fat16(). Kept as a separate entry point
    so future callers can interpose logging / retries / metrics without
    touching every call site."""
    format_fat16(
        out_path=out_path,
        partition_sectors_512=partition_sectors_512,
        sector_size=sector_size,
        sectors_per_cluster=sectors_per_cluster,
        reserved_sectors=reserved_sectors,
        label=label,
        disable_fat_align=disable_fat_align,
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

    # FAT16 BPB parameters for the DOS-side filesystem:
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
    mode, across ALL slots.

    AHDI honors the TOS BGM cap (256/512). PPDRIVER / HDDRIVER use the
    hybrid-layout cap (511 MB) -- bounded by the TOS-side BPB's 16-bit
    NSECTS field at the max supported TOS bps (8192 on TOS 1.04 - 3.x).
    The Falcon TOS 4.x ceiling (2 GB) isn't a target here. See the
    HYBRID_MAX_PARTITION_MB constant for the rationale."""
    if format_id == FORMAT_AHDI:
        return (AHDI_MAX_PARTITION_MB_STRICT if strict_tos
                else AHDI_MAX_PARTITION_MB)
    return HYBRID_MAX_PARTITION_MB


def format_min_partition_mb(format_id: str) -> int:
    """Minimum partition size (MB) for a given format.

    AHDI single-BPB images can host any partition that produces >= 4085
    FAT16 clusters (around 4-5 MB at native bps); the v1 doesn't enforce
    a hard min on AHDI -- format_fat16 rejects too-small inputs at
    write time and the user fixes the size in the dialog.

    Hybrid formats (PPDRIVER / HDDRIVER) require ratio >= 2 in the
    synthesize step, which the Hatari sector-doubling rule first
    reaches at ~32 MB. Below that, build_image fails with a cryptic
    'tos_bps must be a power of two >= 1024'. We catch it earlier."""
    if format_id == FORMAT_AHDI:
        return MIN_PARTITION_MB  # 2 MB; format_fat16 rejects below ~4-5 MB
    return HYBRID_MIN_PARTITION_MB


def partition_cap_mb(format_id: str, strict_tos: bool,
                     partition_index: int) -> int:
    """Per-slot partition-size cap.

    On AHDI we conservatively treat the first partition as the boot
    partition and clamp it to the GEM cap (16 MB strict / 32 MB
    permissive) for legacy-driver compatibility. This is *not* a TOS
    rule per se -- TOS itself only checks the boot flag, and modern
    drivers boot from BGM up to 511 MB -- but the original AHDI and
    early SCSI Tools required the GEM clamp, so we keep it. See
    ahdi_partition_id() for the full rationale. Slots 2..N can be GEM
    (within the same threshold) or BGM (up to 256/511). Hybrid formats
    have no per-slot distinction."""
    if format_id == FORMAT_AHDI and partition_index == 0:
        return AHDI_GEM_MAX_MB_STRICT if strict_tos else AHDI_GEM_MAX_MB
    return format_max_partition_mb(format_id, strict_tos)


# --------------------------------------------------------------------------
# Partition-table parsers (used by the TUI's load action and by the test
# suite). Pure struct.unpack_from; no I/O. The walker + load_image()
# below add the I/O layer.
# --------------------------------------------------------------------------

# Tuple form for iteration; the scalar AHDI_SLOT*_OFFSET / MBR_P0_OFFSET
# constants are still defined above for individual writes.
AHDI_SLOT_OFFSETS = (AHDI_SLOT0_OFFSET, AHDI_SLOT1_OFFSET,
                     AHDI_SLOT2_OFFSET, AHDI_SLOT3_OFFSET)
MBR_SLOT_OFFSETS = tuple(MBR_P0_OFFSET + i * 16 for i in range(4))


def parse_ahdi_entry(buf, offset):
    """Parse a 12-byte AHDI partition entry at `offset` in `buf`.

    Layout (big-endian for the two 32-bit fields):
        +0   flag        (1 byte; bit 0 = exists, bit 7 = bootable)
        +1   ident       (3 bytes ASCII; e.g. b"GEM" / b"BGM" / b"XGM")
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
    """Parse the four AHDI slots from a root sector buffer. Empty slots
    come back with flag=0 / ident=b'\\x00\\x00\\x00'."""
    return [parse_ahdi_entry(buf, off) for off in AHDI_SLOT_OFFSETS]


def parse_mbr_entry(buf, offset):
    """Parse a 16-byte MBR partition entry at `offset` in `buf`.

    Layout (little-endian for the two 32-bit fields):
        +0   boot          (1 byte; 0x80 marks active)
        +1   chs_first     (3 bytes; pack_chs format)
        +4   part_type     (1 byte; 0x06 = FAT16, 0x0F = extended-LBA)
        +5   chs_last      (3 bytes)
        +8   rel_start_lba (4 bytes, little-endian; for EBRs this is
                            relative to the EBR sector or chain base)
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
    {'slots': [<4 dicts>], 'signature': bytes(2)}."""
    return {
        "slots":     [parse_mbr_entry(buf, off) for off in MBR_SLOT_OFFSETS],
        "signature": bytes(buf[510:512]),
    }


def parse_ebr(buf):
    """An EBR has the same on-disk layout as an MBR root sector (4 slot
    positions + signature); only slots 0 and 1 are populated in
    practice."""
    return parse_mbr_root(buf)


def parse_xgm_descriptor(buf):
    """Parse an AHDI XGM sub-descriptor: slot 0 = logical partition,
    slot 1 = next-descriptor link (or empty when chain ends)."""
    return {
        "logical": parse_ahdi_entry(buf, AHDI_SLOT_OFFSETS[0]),
        "link":    parse_ahdi_entry(buf, AHDI_SLOT_OFFSETS[1]),
    }


def parse_bpb(buf, offset):
    """Parse the BPB / extended boot record at `offset` in a FAT16
    boot-sector buffer. Returns every documented field as a plain int
    or bytes value. Multi-byte fields are little-endian."""
    base = offset
    return {
        "jump":                    bytes(buf[base + 0x00:base + 0x03]),
        "oem":                     bytes(buf[base + 0x03:base + 0x0B]),
        "bytes_per_sector":        struct.unpack_from(
                                       "<H", buf, base + 0x0B)[0],
        "sectors_per_cluster":     buf[base + 0x0D],
        "reserved_sectors":        struct.unpack_from(
                                       "<H", buf, base + 0x0E)[0],
        "num_fats":                buf[base + 0x10],
        "root_entries":            struct.unpack_from(
                                       "<H", buf, base + 0x11)[0],
        "total_sectors_16":        struct.unpack_from(
                                       "<H", buf, base + 0x13)[0],
        "media_descriptor":        buf[base + 0x15],
        "sectors_per_fat_16":      struct.unpack_from(
                                       "<H", buf, base + 0x16)[0],
        "sectors_per_track":       struct.unpack_from(
                                       "<H", buf, base + 0x18)[0],
        "num_heads":               struct.unpack_from(
                                       "<H", buf, base + 0x1A)[0],
        "hidden_sectors":          struct.unpack_from(
                                       "<I", buf, base + 0x1C)[0],
        "total_sectors_32":        struct.unpack_from(
                                       "<I", buf, base + 0x20)[0],
        "drive_number":            buf[base + 0x24],
        "extended_boot_signature": buf[base + 0x26],
        "volume_id":               struct.unpack_from(
                                       "<I", buf, base + 0x27)[0],
        "volume_label":            bytes(buf[base + 0x2B:base + 0x36]),
        "fs_type":                 bytes(buf[base + 0x36:base + 0x3E]),
        "signature":               bytes(buf[base + 0x1FE:base + 0x200]),
    }


def cap_mb_for_type(format_id: str, strict_tos: bool,
                    ident) -> int:
    """Cap (MB) for a partition given its on-disk type ident.

    Used by interactive callers (the TUI dialog) where the user picks
    the type explicitly and we need the cap for *that* type, not the
    auto-pick that partition_cap_mb() implies from the slot index.

    AHDI: ident b"GEM" / "GEM" -> GEM cap; b"BGM" / "BGM" -> BGM cap;
          unknown -> BGM cap (the more permissive default).
    Hybrid formats (PPDRIVER / HDDRIVER): ident is ignored, returns
          the FAT16 ceiling. The DOS view doesn't honor the TOS BGM
          rules.
    """
    if format_id == FORMAT_AHDI:
        if ident in (b"GEM", "GEM"):
            return AHDI_GEM_MAX_MB_STRICT if strict_tos else AHDI_GEM_MAX_MB
        return AHDI_MAX_PARTITION_MB_STRICT if strict_tos else AHDI_MAX_PARTITION_MB
    return HYBRID_MAX_PARTITION_MB


# --------------------------------------------------------------------------
# Image loader: detect format, walk the partition table, recover labels
# from each partition's FAT16 BPB. Used by the TUI's L (load) action.
# --------------------------------------------------------------------------

class ImageLoadError(RuntimeError):
    """Raised when an image can't be parsed (file too small, no
    recognisable root sector, corrupt chain, etc.). The TUI surfaces
    the message verbatim in the status bar."""


# PPDRIVER (PPTOSDOS) marker stamped on every partition's BPB by
# build_image step 3. Per the Atari Compendium it is the canonical
# detection mark for "this is a PPDRIVER-managed image"; PPDRIVER
# itself uses it as a distinguishing marker so it can apply the dual-
# BPB redirection logic.
PPDRIVER_OEM = b"PPGDODBC"


def detect_format(image_path: str):
    """Return one of "AHDI" / "PPDRIVER" / "HDDRIVER" or None.

    Heuristic (validated against the Atari Compendium notebook):
      - 0x55AA at byte 510 -> MBR-style root sector. Then:
          * AHDI overlap at 0x1DE (flag bit 0 set, ident GEM/BGM)
            -> HDDRIVER's overlap trick.
          * Else: read the first FAT16 primary's BPB and check for
            OEM "PPGDODBC" -> PPDRIVER. Without the marker we
            return None rather than falsely tagging vanilla DOS
            MBR images as PPDRIVER.
      - No 0x55AA + at least one valid AHDI slot (GEM/BGM/XGM)
        -> pure-AHDI image.
      - Anything else -> None (caller surfaces "format unknown").
    """
    with open(image_path, "rb") as f:
        sec0 = f.read(SECTOR_SIZE)
    if len(sec0) < SECTOR_SIZE:
        return None
    has_mbr_sig = bytes(sec0[510:512]) == MBR_SIGNATURE
    if has_mbr_sig:
        # Inspect the bytes at AHDI slot 2 (= MBR slot 2 area). If they
        # parse as an AHDI partition entry with flag-exists set and a
        # known ident, this is the HDDRIVER overlap.
        ahdi_at_slot2 = parse_ahdi_entry(sec0, AHDI_SLOT2_OFFSET)
        if (ahdi_at_slot2["flag"] & AHDI_FLAG_EXISTENT) and (
                ahdi_at_slot2["ident"] in (b"GEM", b"BGM")):
            return FORMAT_HDDRIVER
        # PPDRIVER: walk MBR slots to find the first FAT16 primary,
        # then check its BPB OEM. We stamp PPGDODBC on every
        # partition's BPB at build time, so checking the first one
        # is sufficient.
        if _has_ppdriver_oem_marker(image_path, sec0):
            return FORMAT_PPDRIVER
        # MBR-signed image without the HDDRIVER overlap and without
        # the PPDRIVER OEM: not a recognised hybrid (vanilla DOS,
        # foreign tooling, corrupt root, ...). Return None and let
        # the caller surface "format unknown".
        return None
    # No MBR signature -> pure AHDI if any slot has a valid ident.
    valid = (b"GEM", b"BGM", b"XGM")
    for slot in parse_ahdi_root(sec0):
        if (slot["flag"] & AHDI_FLAG_EXISTENT) and slot["ident"] in valid:
            return FORMAT_AHDI
    return None


def _has_ppdriver_oem_marker(image_path: str, sec0: bytes) -> bool:
    """Return True iff the first FAT16 MBR primary's BPB carries the
    PPDRIVER OEM marker. Conservative on errors (returns False) so a
    bad read never produces a false-positive classification."""
    try:
        mbr = parse_mbr_root(sec0)
        for slot in mbr["slots"]:
            if slot["part_type"] != MBR_TYPE_FAT16:
                continue
            start_lba = slot["rel_start_lba"]
            with open(image_path, "rb") as f:
                f.seek(start_lba * SECTOR_SIZE)
                bpb_sec = f.read(SECTOR_SIZE)
            if len(bpb_sec) != SECTOR_SIZE:
                return False
            bpb = parse_bpb(bpb_sec, 0)
            return bpb["oem"] == PPDRIVER_OEM
        return False
    except (OSError, struct.error):
        return False


def _read_sector_at(image_path: str, lba: int, image_size: int) -> bytes:
    """Read one 512-byte sector at `lba`. Raises ImageLoadError when the
    LBA falls outside the file (defensive against corrupt chain
    pointers; we never read past the file)."""
    if lba < 0 or (lba + 1) * SECTOR_SIZE > image_size:
        raise ImageLoadError(
            f"chain pointer LBA {lba} is outside the image "
            f"({image_size} bytes)")
    with open(image_path, "rb") as f:
        f.seek(lba * SECTOR_SIZE)
        buf = f.read(SECTOR_SIZE)
    if len(buf) != SECTOR_SIZE:
        raise ImageLoadError(f"short read at LBA {lba}")
    return buf


def _label_from_bpb(image_path: str, start_lba: int,
                    image_size: int, default: str) -> str:
    """Read the FAT16 BPB at `start_lba` and return the volume label
    (uppercased, trimmed). Falls back to `default` if the BPB looks
    invalid -- we don't fail the whole load for one missing label."""
    try:
        sec = _read_sector_at(image_path, start_lba, image_size)
        bpb = parse_bpb(sec, 0)
    except (ImageLoadError, struct.error):
        return default
    if bpb["signature"] != MBR_SIGNATURE:
        # Not a recognisable BPB; use default.
        return default
    label = bpb["volume_label"].rstrip(b" \x00")
    try:
        decoded = label.decode("ascii", errors="replace").strip()
    except Exception:
        return default
    return decoded or default


def _load_ahdi(image_path: str, sec0: bytes, image_size: int):
    """Walk the AHDI root sector and any XGM chain. Returns a list of
    Partition objects. Raises ImageLoadError on chain corruption."""
    partitions = []
    chain_base = None
    chain_link = None

    for slot_idx, slot in enumerate(parse_ahdi_root(sec0)):
        if not (slot["flag"] & AHDI_FLAG_EXISTENT):
            continue
        if slot["ident"] == b"XGM":
            chain_base = slot["start_lba"]
            chain_link = slot["start_lba"]
            continue
        if slot["ident"] not in (b"GEM", b"BGM"):
            continue
        partitions.append(_partition_from_entry(
            image_path, image_size, slot, slot_index=len(partitions)))

    # Walk the XGM chain.
    visited = set()
    while chain_link is not None:
        if chain_link in visited:
            raise ImageLoadError("XGM chain has a cycle")
        visited.add(chain_link)
        desc_buf = _read_sector_at(image_path, chain_link, image_size)
        desc = parse_xgm_descriptor(desc_buf)
        logical = desc["logical"]
        if not (logical["flag"] & AHDI_FLAG_EXISTENT):
            break
        # Logical partition: start is RELATIVE to the descriptor's LBA.
        abs_start = chain_link + logical["start_lba"]
        partitions.append(_partition_from_entry(
            image_path, image_size,
            {"flag": logical["flag"], "ident": logical["ident"],
             "start_lba": abs_start,
             "size_sectors": logical["size_sectors"]},
            slot_index=len(partitions)))
        link = desc["link"]
        if not (link["flag"] & AHDI_FLAG_EXISTENT):
            break
        chain_link = chain_base + link["start_lba"]

    return partitions


def _load_mbr(image_path: str, sec0: bytes, image_size: int,
              skip_slot2: bool):
    """Walk an MBR root sector and any EBR chain. `skip_slot2` skips
    the slot-2 entry (used for HDDRIVER, where 0x1DE holds an AHDI
    overlap rather than a real MBR partition)."""
    partitions = []
    ext_base = None
    ext_link = None

    mbr = parse_mbr_root(sec0)
    for i, slot in enumerate(mbr["slots"]):
        if skip_slot2 and i == 2:
            continue
        ptype = slot["part_type"]
        if ptype == 0:
            continue
        if ptype in MBR_EXTENDED_TYPES:
            # Either 0x0F (PPDRIVER LBA) or 0x05 (HDDRIVER CHS) is a
            # valid extended-container marker -- accept both.
            ext_base = slot["rel_start_lba"]
            ext_link = slot["rel_start_lba"]
            continue
        if ptype != MBR_TYPE_FAT16:
            # Unknown partition type; skip.
            continue
        partitions.append(_partition_from_mbr(
            image_path, image_size,
            start_lba=slot["rel_start_lba"],
            size_sectors=slot["sector_count"],
            slot_index=len(partitions)))

    # Walk EBR chain.
    visited = set()
    while ext_link is not None:
        if ext_link in visited:
            raise ImageLoadError("EBR chain has a cycle")
        visited.add(ext_link)
        ebr_buf = _read_sector_at(image_path, ext_link, image_size)
        ebr = parse_ebr(ebr_buf)
        slot0 = ebr["slots"][0]
        if slot0["part_type"] == 0:
            break
        abs_start = ext_link + slot0["rel_start_lba"]
        partitions.append(_partition_from_mbr(
            image_path, image_size,
            start_lba=abs_start,
            size_sectors=slot0["sector_count"],
            slot_index=len(partitions)))
        slot1 = ebr["slots"][1]
        if slot1["part_type"] not in MBR_EXTENDED_TYPES:
            break
        ext_link = ext_base + slot1["rel_start_lba"]

    return partitions


def _partition_from_entry(image_path: str, image_size: int, entry: dict,
                          slot_index: int) -> "Partition":
    """Build a Partition from an AHDI-table entry, recovering the label
    from the FAT16 BPB at start_lba. AHDI partitions live at bps =
    Hatari-doubling-rule(size_sectors); we use that convention to
    locate the BPB."""
    size_sec = entry["size_sectors"]
    size_mb = (size_sec * SECTOR_SIZE) // MIB
    default = f"P{slot_index + 1}"
    name = _label_from_bpb(image_path, entry["start_lba"], image_size,
                            default=default)
    p = Partition(name=name, size_mb=size_mb,
                   size_sectors=size_sec,
                   start_lba=entry["start_lba"])
    ident = entry["ident"]
    p.ahdi_ident = (ident.decode("ascii", errors="replace")
                    if isinstance(ident, (bytes, bytearray)) else ident)
    return p


def _partition_from_mbr(image_path: str, image_size: int,
                        start_lba: int, size_sectors: int,
                        slot_index: int) -> "Partition":
    """Build a Partition from an MBR/EBR FAT16 entry. The DOS BPB lives
    at start_lba (bps=512 for the dual-BPB DOS view)."""
    size_mb = (size_sectors * SECTOR_SIZE) // MIB
    default = f"P{slot_index + 1}"
    name = _label_from_bpb(image_path, start_lba, image_size,
                            default=default)
    p = Partition(name=name, size_mb=size_mb,
                   size_sectors=size_sectors, start_lba=start_lba)
    return p


def _infer_strict_tos(partitions) -> bool:
    """Best-effort guess at strict_tos from partition sizes. Strict if
    slot 0 <= 16 MB and all later partitions <= 256 MB. Only meaningful
    for AHDI; the caller should still set strict_tos=False on hybrid
    formats."""
    for i, p in enumerate(partitions):
        cap = AHDI_GEM_MAX_MB_STRICT if i == 0 else AHDI_MAX_PARTITION_MB_STRICT
        if p.size_mb > cap:
            return False
    return True


def load_image(image_path: str) -> dict:
    """Parse an existing image and return a planning summary:
        {
          "format_id": "AHDI" / "PPDRIVER" / "HDDRIVER",
          "strict_tos": bool,         # AHDI-only inference
          "partitions": list[Partition],
          "strict_tos_inferred": bool, # True iff we guessed
        }
    Raises ImageLoadError on unrecognised or corrupt images."""
    try:
        image_size = os.path.getsize(image_path)
    except OSError as e:
        raise ImageLoadError(f"cannot stat image: {e}") from e
    if image_size < SECTOR_SIZE:
        raise ImageLoadError(
            f"image too small ({image_size} bytes); need at least 512")

    fmt = detect_format(image_path)
    if fmt is None:
        raise ImageLoadError(
            "no recognisable AHDI or MBR partition table at sector 0")

    sec0 = _read_sector_at(image_path, 0, image_size)
    if fmt == FORMAT_AHDI:
        partitions = _load_ahdi(image_path, sec0, image_size)
    elif fmt == FORMAT_HDDRIVER:
        partitions = _load_mbr(image_path, sec0, image_size,
                                skip_slot2=True)
    else:
        partitions = _load_mbr(image_path, sec0, image_size,
                                skip_slot2=False)

    if not partitions:
        raise ImageLoadError(
            f"recognised {fmt} root sector but no partitions found")

    strict_inferred = (fmt == FORMAT_AHDI)
    strict_tos = _infer_strict_tos(partitions) if strict_inferred else False

    return {
        "format_id": fmt,
        "strict_tos": strict_tos,
        "strict_tos_inferred": strict_inferred,
        "partitions": partitions,
    }


def first_partition_start_lba(format_id: str) -> int:
    """AHDI: physical LBA 1 is reserved for the Bad Sector List (BSL).
    The root sector carries `bst_st` / `bst_cnt` pointers to it; we
    don't emit a BSL on generated images (those fields stay at 0 = no
    BSL), but we still leave LBA 1 vacant so a real driver writing a
    BSL later doesn't have to relocate partition 0. The first
    partition therefore starts at LBA 2.

    Hybrid layouts (PPDRIVER / HDDRIVER): no AHDI BSL is involved; the
    DOS BPB sits at LBA 1 directly. Validated against the Atari
    Compendium notebook; see CLAUDE.md."""
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

    # AHDI-only: enforce per-slot caps. Slot 0 is conservatively
    # treated as the boot partition and clamped to the GEM cap (16/32
    # MB) for legacy-driver compatibility -- not a hard TOS rule but a
    # defensive default; see ahdi_partition_id() for the rationale.
    # Slots 2..N follow the BGM 256/511 MB cap. Hybrid formats have no
    # per-slot distinction.
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
    else:
        # Hybrid formats (PPDRIVER / HDDRIVER) have a hard min size:
        # the synthesize step requires tos_bps >= 1024, which the
        # Hatari sector-doubling rule first reaches at ~32 MB. Catch
        # it here with a clear message instead of letting the user
        # hit the cryptic 'tos_bps must be a power of two >= 1024'
        # later. The hybrid max (511 MB) is enforced via
        # cap_mb_for_type() at the TUI layer; we don't repeat it
        # here because the planner accepts any size up to the FAT16
        # cluster cap and the synthesize layer rejects whatever is
        # actually unbuildable.
        for i, part in enumerate(partitions):
            if part.size_mb < HYBRID_MIN_PARTITION_MB:
                raise ValueError(
                    f"partition {part.name!r} is {part.size_mb} MB but "
                    f"{format_id} requires >= {HYBRID_MIN_PARTITION_MB} "
                    f"MB per partition (hybrid layout requires logical "
                    f"sector size >= 1024)")
            if part.size_mb > HYBRID_MAX_PARTITION_MB:
                raise ValueError(
                    f"partition {part.name!r} is {part.size_mb} MB but "
                    f"{format_id} caps at {HYBRID_MAX_PARTITION_MB} MB "
                    f"per partition (TOS NSECTS 16-bit limit at "
                    f"bps=8192)")

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

    `is_first=True` forces b"GEM" regardless of size. This is a
    *defensive compatibility* choice rather than a hard TOS requirement:
    TOS itself only checks the boot flag (bit 7 of the partition-entry
    flag byte), not the ident; modern drivers (HDDRIVER, PPDRIVER, ICD
    Pro) happily boot from BGM up to 511 MB. But the original AHDI and
    early SCSI Tools required ident == GEM and size <= 16 MB on the
    boot slot, so emitting GEM at slot 0 is the broadest-compatibility
    pick. Removing this clamp would let the user set up a 511 MB BGM
    boot partition that works on modern drivers but fails on legacy
    ones; revisit if a workflow actually wants that.

    Later partitions use the usual threshold: GEM when partition_mb is
    <= the TOS-version GEM cap, BGM otherwise. Note that the underlying
    distinction is sector-size driven (GEM = bps 512, BGM = bps > 512);
    the size threshold is what causes choose_logical_sector_size() to
    flip bps, so size and ident move together.

    The GEM/BGM threshold differs between TOS versions: original TOS
    (< 1.04) accepts GEM only up to 16 MB, while TOS 1.04+ extends that
    to 32 MB. `strict_tos=True` requests the conservative pre-1.04
    rule."""
    if is_first:
        return b"GEM"
    threshold = AHDI_GEM_MAX_MB_STRICT if strict_tos else AHDI_GEM_MAX_MB
    return b"GEM" if partition_mb <= threshold else b"BGM"


def build_image(plan: ImagePlan, progress=None) -> None:
    """Materialise `plan` to disk at `plan.image_path`.

    `progress`, when not None, is invoked with a single string argument
    at each major step of the pipeline (allocation, per-partition
    format, BPB synthesis, root-sector write, extended-chain write).
    Useful for surfacing progress in interactive callers (TUI). The
    callback runs in the same thread as the writer; raising from it
    aborts the build.
    """
    def _emit(msg):
        if progress is not None:
            progress(msg)

    # Step 1: allocate the image file (sparse where possible).
    image_bytes = plan.image_sectors * SECTOR_SIZE
    _emit(f"Allocating {image_bytes // MIB} MB image...")
    try:
        with open(plan.image_path, "wb") as f:
            f.truncate(image_bytes)
    except OSError as e:
        raise RuntimeError(f"cannot create {plan.image_path}: {e}") from e

    # Step 2: for each partition, format a temp file as FAT16 and splice
    # the bytes into the main image at the partition's physical offset.
    n = len(plan.partitions)
    for i, part in enumerate(plan.partitions):
        _emit(f"Writing partition {i + 1}/{n}: {part.name} "
              f"({part.size_mb} MB)...")
        tmp_fd, tmp_path = tempfile.mkstemp(prefix="atari_hd_",
                                            suffix=".fatpart")
        os.close(tmp_fd)
        try:
            run_mkfs(
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
        _emit("Stamping TOS BPBs...")
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
    _emit("Writing partition table...")
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
        _emit("Writing extended-chain descriptors...")
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
                # PPDRIVER uses 0x0F (LBA), HDDRIVER uses 0x05 (CHS)
                # for the next-EBR-link type byte. Match the
                # container type emitted in the root sector.
                ext_type = (MBR_TYPE_EXTENDED_CHS
                            if plan.format_id == FORMAT_HDDRIVER
                            else MBR_TYPE_EXTENDED_LBA)
                desc = build_ebr_sector(
                    logical_abs_start=part.start_lba,
                    logical_size=part.size_sectors,
                    ebr_abs_lba=part.ebr_lba,
                    ext_base_abs_lba=chain_base,
                    next_ebr_abs_lba=next_desc_abs,
                    next_chain_size=next_chain_size,
                    extended_type=ext_type,
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
    print("   - BGM <= 256 MB  (vs 511 MB on TOS 1.04+)")
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
    print("SidecarTridge Atari HD image builder")

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
        build_image(plan)
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
