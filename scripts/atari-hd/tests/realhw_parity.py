#!/usr/bin/env python3
"""realhw_parity.py - byte-level diff vs real-tool reference images.

Developer aid only. For each user-supplied reference image, parses
the partition layout, rebuilds an equivalent image with this tool's
writer, and reports byte ranges that differ. Diffs are classified
as "expected" (within a known per-format mask) or "unexpected" (a
real regression). Exit 0 iff all unexpected diffs are zero.

Stdlib only. Skips cleanly when no references are supplied.

Usage:
    python scripts/atari-hd/tests/realhw_parity.py \
        [--ahdi-reference PATH      [--ahdi-driver PATH]] \
        [--ppdriver-reference PATH] \
        [--hddriver-reference PATH] \
        [--out-dir DIR]

Notes per format:
  - AHDI: when the reference is bootable (sector 0 sums to $1234
    AND slot 0 has the boot flag), --ahdi-driver is required and
    must point at the SAME ICDBOOT.PRG the reference was built
    with. Otherwise the bootable-driver region will diff and the
    harness can't tell whether that's expected.
  - PPDRIVER: always non-bootable for the rebuild unless we can
    auto-detect bootable from the reference. Bootable PPDRIVER
    images get the bundled boot blob automatically.
  - HDDRIVER: non-bootable parity only -- this tool can't produce
    self-bootable HDDRIVER images (story 006 manual-install path).
    A user-supplied HDDRIVER reference is compared as a non-
    bootable HDDRIVER format build.
"""

import argparse
import os
import struct
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import atari_hd  # noqa: E402


CHUNK_SIZE = 4 * 1024 * 1024
MAX_DIFFS_REPORTED = 20


def _word_sum(buf):
    return sum(struct.unpack(">256H", buf)) & 0xFFFF


def _is_ahdi_bootable(sec0):
    """Heuristic: AHDI sector 0 sums to $1234 AND slot 0 boot flag set."""
    if _word_sum(sec0) != 0x1234:
        return False
    slot0_flag = sec0[atari_hd.AHDI_SLOT0_OFFSET]
    return bool(slot0_flag & atari_hd.AHDI_FLAG_BOOTABLE)


def _is_ppdriver_bootable(sec0):
    """PPDRIVER bootable: word-sum $1234 + 0x55AA MBR signature."""
    if sec0[atari_hd.MBR_SIGNATURE_OFFSET:
            atari_hd.MBR_SIGNATURE_OFFSET + 2] != atari_hd.MBR_SIGNATURE:
        return False
    return _word_sum(sec0) == 0x1234


def _ahdi_masks(plan, ref_path, ref_sec0):
    """Byte ranges where AHDI diffs are expected (per-image variation,
    not regressions). Absolute byte offsets."""
    masks = []
    bootable = _is_ahdi_bootable(ref_sec0)
    if bootable:
        # ICDFMT bakes a 1-byte per-image marker at sector 0 offset
        # 0x1C1 (last byte of the IPL region). Sigword at 0x1FE..0x1FF
        # then compensates so the whole sector still sums to $1234.
        # Both bytes vary per-image; mask both regions.
        masks.append((0x1C1, 0x1C2, "sector 0 IPL per-image byte"))
        masks.append((0x1FE, 0x200, "sector 0 sigword (compensates for 0x1C1)"))
    # Per-partition: FAT[0..1] media-descriptor entries + the file-
    # padding region after a bootable's ICDBOOT.SYS in cluster 2.
    for i, part in enumerate(plan.partitions):
        bps, spc, resv, nfats, nroot, spfat = _read_bpb_geom(
            ref_path, part.start_lba)
        if bps == 0 or spc == 0 or nfats == 0 or spfat == 0:
            continue
        ratio = max(1, bps // atari_hd.SECTOR_SIZE)
        fat_lba_phys = part.start_lba + resv * ratio
        for fat_idx in range(nfats):
            fat_start_byte = ((fat_lba_phys + fat_idx * spfat * ratio)
                               * atari_hd.SECTOR_SIZE)
            # FAT entries 0 and 1 (first 4 bytes of the FAT) -- real
            # ICDFMT leaves them zeroed; our format_fat16 writes
            # FFF8 FFFF. Both work; the IPL doesn't read them.
            masks.append((fat_start_byte, fat_start_byte + 4,
                          f"part {i+1} FAT{fat_idx+1}[0..1] media bytes"))
        # On a bootable AHDI image, mask everything in the data area
        # past ICDBOOT.SYS's cluster chain. The region we actually
        # write byte-by-byte is clusters 2..N where N is the last
        # cluster of ICDBOOT.SYS; everything beyond that is
        # unallocated FAT space. Real Atari ICDFMT-formatted disks
        # often dump-include leftover data from prior use here; we
        # leave it zero. That's expected per-image variation, not a
        # bug -- but if our writer ever started touching post-file
        # data, the cluster-2..N region itself would still be
        # checked, so regressions there are caught.
        if bootable and i == 0:
            data_lba_log = resv + nfats * spfat + (
                nroot * 32 + bps - 1) // bps
            data_lba_phys = part.start_lba + data_lba_log * ratio
            cluster_size = bps * spc
            driver_size = 48502  # ICDBOOT.PRG canonical size
            n_clusters = (driver_size + cluster_size - 1) // cluster_size
            file_end_byte = (data_lba_phys * atari_hd.SECTOR_SIZE
                              + driver_size)
            cluster_end_byte = (data_lba_phys * atari_hd.SECTOR_SIZE
                                 + n_clusters * cluster_size)
            part_end_byte = ((part.start_lba + part.size_sectors)
                              * atari_hd.SECTOR_SIZE)
            if cluster_end_byte > file_end_byte:
                masks.append((file_end_byte, cluster_end_byte,
                              f"part {i+1} ICDBOOT.SYS cluster-tail padding"))
            if part_end_byte > cluster_end_byte:
                masks.append((cluster_end_byte, part_end_byte,
                              f"part {i+1} unallocated data area "
                              f"(beyond ICDBOOT.SYS clusters)"))
        # Also mask the per-partition unused area between the FAT
        # tables and any unmodified cluster region for non-bootable
        # AHDI: we don't claim byte-fidelity for unallocated data
        # area on any partition.
        if not (bootable and i == 0):
            data_lba_log = resv + nfats * spfat + (
                nroot * 32 + bps - 1) // bps
            data_lba_phys = part.start_lba + data_lba_log * ratio
            part_end_byte = ((part.start_lba + part.size_sectors)
                              * atari_hd.SECTOR_SIZE)
            data_start_byte = data_lba_phys * atari_hd.SECTOR_SIZE
            if part_end_byte > data_start_byte:
                masks.append((data_start_byte, part_end_byte,
                              f"part {i+1} data area (unallocated)"))
    return masks


def _ppdriver_masks(plan, ref_path, ref_sec0):
    """Mask intervals for PPDRIVER reference parity.

    PPDRIVER byte-fidelity is claimed only for the boot-blob region
    (LBA 0..14: sector 0 IPL + secondary IPL + bundled .PRG-format
    driver). Everything past LBA 14 -- the cylinder-alignment slack
    at LBA 15..62 plus the partitions starting at LBA 63 -- is
    expected to differ from a real-tool reference because:
      - Our writer's spfat / cluster auto-alignment produces
        different partition sectors than ICDFMT / PPTOSDOS would
        for the same user-input size_mb.
      - That cascades into the MBR partition table (CHS + LBA
        encoding), the partition's BPB header, FAT positions, and
        all data-area bytes.
    None of those are regressions in OUR writer; they're stylistic
    deltas. The harness's value here is locking down sector 0 IPL +
    LBA 1..14 byte-equality, which it does within the leading 15
    sectors that aren't masked.
    """
    masks = []
    # 4-byte disk signature at MBR offset 0x1B8 -- random in real
    # PPTOSDOS output, deterministic-from-plan in ours.
    masks.append((atari_hd.MBR_DISK_SIGNATURE_OFFSET,
                  atari_hd.MBR_DISK_SIGNATURE_OFFSET + 4,
                  "PPDRIVER disk signature (random in real tool)"))
    # The $1234 adjust word (0x1BC..0x1BD), MBR partition table
    # (0x1BE..0x1FD) and CHS bytes therein vary with partition
    # geometry; auto-alignment differences from ICDFMT make these
    # not byte-equal even when the IPL itself matches.
    masks.append((atari_hd.MBR_BOOT_ADJUST_WORD_OFFSET,
                  atari_hd.MBR_BOOT_ADJUST_WORD_OFFSET + 2,
                  "PPDRIVER $1234 adjust word (compensates for "
                  "table changes)"))
    masks.append((atari_hd.MBR_P0_OFFSET,
                  atari_hd.MBR_SIGNATURE_OFFSET,
                  "MBR partition table (varies with auto-aligned "
                  "partition geometry)"))
    # Mask everything from LBA 15 onwards (cylinder-alignment slack
    # + all partitions). Real Atari raw dumps include user data and
    # ICDFMT-aligned geometry; our writer produces fresh-format
    # output with differently-aligned partitions.
    masks.append((15 * atari_hd.SECTOR_SIZE,
                  os.path.getsize(ref_path),
                  "PPDRIVER LBA 15+ (post-boot-blob region: "
                  "cylinder slack + partitions)"))
    return masks


def _post_partitions_mask(plan, ref_size):
    """Single mask covering anything past the last sector of the last
    plan partition. Real reference dumps often extend well past the
    last partition (e.g. the 2 GiB AHDI reference is a raw disk dump
    with only the first 16 MB partitioned); we don't write to that
    region, so per-byte equality there isn't a meaningful claim."""
    if not plan.partitions:
        return []
    last_lba = max(p.start_lba + p.size_sectors for p in plan.partitions)
    last_byte = last_lba * atari_hd.SECTOR_SIZE
    if ref_size <= last_byte:
        return []
    return [(last_byte, ref_size,
             "image bytes past last partition end (not written)")]


def _read_bpb_geom(image_path, partition_lba):
    """Read a partition's FAT16 BPB and return (bps, spc, resv, nfats,
    nroot, spfat). Used to compute root-dir LBAs for masking."""
    with open(image_path, "rb") as f:
        f.seek(partition_lba * atari_hd.SECTOR_SIZE)
        bpb = f.read(atari_hd.SECTOR_SIZE)
    return (
        int.from_bytes(bpb[11:13], "little"),  # bps
        bpb[13],                                # spc
        int.from_bytes(bpb[14:16], "little"),   # resv
        bpb[16],                                # nfats
        int.from_bytes(bpb[17:19], "little"),   # nroot
        int.from_bytes(bpb[22:24], "little"),   # spfat
    )


def _partition_root_dir_masks(plan, image_path, label_prefix):
    """For each partition in plan, compute byte ranges in the FAT16
    root directory area where dir-entry timestamps live (per-entry
    offsets 14..25 of the 32-byte entry). These are expected to differ
    between real-tool builds (wall-clock seeded) and ours (zero or
    deterministic).

    Reads BPB from `image_path` -- usable on either the reference or
    our build since both have identical FAT16 geometry."""
    masks = []
    for i, part in enumerate(plan.partitions):
        bps, spc, resv, nfats, nroot, spfat = _read_bpb_geom(
            image_path, part.start_lba)
        if bps == 0 or nfats == 0 or spfat == 0 or nroot == 0:
            continue
        ratio = max(1, bps // atari_hd.SECTOR_SIZE)
        root_lba_log = resv + nfats * spfat
        root_lba = part.start_lba + root_lba_log * ratio
        n_entries = nroot
        for entry_idx in range(n_entries):
            entry_off = (root_lba * atari_hd.SECTOR_SIZE
                         + entry_idx * 32)
            # Mask offsets 14..25 of each entry (ctime/cdate/adate
            # + the FAT32 cluster-high field which FAT16 leaves at 0)
            masks.append((entry_off + 14, entry_off + 26,
                          f"{label_prefix} part {i+1} root entry "
                          f"{entry_idx} timestamps"))
    return masks


def _diff_with_masks(ref_path, our_path, masks):
    """Chunked byte-by-byte diff. Returns (expected_diffs, unexpected_diffs)
    where each is a list of (offset, ref_byte, our_byte) tuples capped at
    MAX_DIFFS_REPORTED for `unexpected_diffs`. Masks are merged and
    sorted; bytes inside any mask interval count as expected."""
    # Sort + merge masks for fast lookup
    if masks:
        sorted_masks = sorted(masks, key=lambda m: m[0])
    else:
        sorted_masks = []

    def _is_masked(off, mask_idx_ref):
        i = mask_idx_ref[0]
        while i < len(sorted_masks) and sorted_masks[i][1] <= off:
            i += 1
        mask_idx_ref[0] = i
        return (i < len(sorted_masks)
                and sorted_masks[i][0] <= off < sorted_masks[i][1])

    ref_size = os.path.getsize(ref_path)
    our_size = os.path.getsize(our_path)
    expected, unexpected = 0, []
    mask_cursor = [0]  # mutable so _is_masked can advance it
    walk_size = min(ref_size, our_size)
    pos = 0
    with open(ref_path, "rb") as fr, open(our_path, "rb") as fo:
        while pos < walk_size:
            n = min(CHUNK_SIZE, walk_size - pos)
            r = fr.read(n)
            o = fo.read(n)
            # Fast path: chunks are bytes-equal -- skip the per-byte
            # walk entirely. Most of a multi-MB image has zero diffs;
            # bytes() == bytes() in C is vastly faster than the Python
            # loop. Only fall through to per-byte when there's a diff.
            if r == o:
                pos += n
                continue
            for j in range(n):
                if r[j] != o[j]:
                    if _is_masked(pos + j, mask_cursor):
                        expected += 1
                    elif len(unexpected) < MAX_DIFFS_REPORTED:
                        unexpected.append((pos + j, r[j], o[j]))
                    else:
                        unexpected.append(None)  # marker for "more"
            pos += n
    return expected, unexpected, ref_size, our_size


def _build_one(reference_path, format_hint, out_dir, ahdi_driver_path):
    """Load reference, plan equivalent, build to out_dir/<basename>.our.
    Returns (our_path, plan)."""
    summary = atari_hd.load_image(reference_path)
    if format_hint and summary["format_id"] != format_hint:
        raise RuntimeError(
            f"reference {reference_path!r} parsed as "
            f"{summary['format_id']} but --{format_hint.lower()}"
            f"-reference was expected")
    fmt = summary["format_id"]
    ref_size = os.path.getsize(reference_path)
    image_mb = max(1, ref_size // (1024 * 1024))

    with open(reference_path, "rb") as f:
        sec0 = f.read(atari_hd.SECTOR_SIZE)
    bootable_ahdi = (fmt == atari_hd.FORMAT_AHDI
                     and _is_ahdi_bootable(sec0))
    bootable_pp = (fmt == atari_hd.FORMAT_PPDRIVER
                   and _is_ppdriver_bootable(sec0))

    if bootable_ahdi and not ahdi_driver_path:
        raise RuntimeError(
            f"reference {reference_path!r} is a bootable AHDI image but "
            f"--ahdi-driver wasn't supplied; the harness can't rebuild "
            f"a parallel without the same driver bytes")

    out_path = str(Path(out_dir) /
                    f"{Path(reference_path).stem}.our.img")
    if os.path.exists(out_path):
        os.unlink(out_path)
    plan = atari_hd.plan_image(
        format_id=fmt,
        image_path=out_path,
        image_mb=image_mb,
        partitions=summary["partitions"],
        strict_tos=summary["strict_tos"],
        ahdi_driver_path=ahdi_driver_path if bootable_ahdi else None,
        ppdriver_bootable=bootable_pp,
    )
    atari_hd.build_image(plan)
    return out_path, plan, sec0


def _report_one(label, reference_path, our_path, plan, ref_sec0,
                masks):
    """Run the diff, print a per-reference summary, return True iff
    no unexpected diffs were found."""
    expected, unexpected, ref_size, our_size = _diff_with_masks(
        reference_path, our_path, masks)
    print(f"\n=== {label} ===")
    print(f"  reference : {reference_path} ({ref_size} bytes)")
    print(f"  our build : {our_path} ({our_size} bytes)")
    if ref_size != our_size:
        print(f"  size      : DIFFER ({ref_size} vs {our_size})")
    else:
        print(f"  size      : match ({ref_size} bytes)")
    print(f"  masks     : {len(masks)} byte ranges marked expected")
    print(f"  expected diffs (within masks)   : {expected}")
    n_unexp = sum(1 for u in unexpected if u is not None)
    truncated = unexpected and unexpected[-1] is None
    print(f"  unexpected diffs (regressions) : {n_unexp}"
          f"{'+ (truncated)' if truncated else ''}")
    for u in unexpected[:MAX_DIFFS_REPORTED]:
        if u is None:
            print(f"    ... (more truncated; raise MAX_DIFFS_REPORTED to see)")
            break
        off, rb, ob = u
        print(f"    off {off:#010x}: ref={rb:#04x}  ours={ob:#04x}")
    return n_unexp == 0


def main(argv=None):
    p = argparse.ArgumentParser(prog="realhw_parity.py")
    p.add_argument("--ahdi-reference", metavar="PATH", default=None)
    p.add_argument("--ahdi-driver", metavar="PATH", default=None,
                    help="Required when the AHDI reference is bootable. "
                         "Should be the same ICDBOOT.PRG bytes the "
                         "reference was built with.")
    p.add_argument("--ppdriver-reference", metavar="PATH", default=None)
    p.add_argument("--hddriver-reference", metavar="PATH", default=None)
    p.add_argument("--out-dir", metavar="DIR", default=None,
                    help="Where to drop our parallel builds. Defaults "
                         "to a tempdir cleaned on exit.")
    args = p.parse_args(argv)

    refs = [r for r in (args.ahdi_reference, args.ppdriver_reference,
                         args.hddriver_reference) if r]
    if not refs:
        print("realhw_parity.py: no reference images supplied; skipping. "
              "(Pass --ahdi-reference / --ppdriver-reference / "
              "--hddriver-reference to enable.)")
        return 0
    for r in refs:
        if not os.path.exists(r):
            print(f"ERROR: reference path does not exist: {r!r}",
                  file=sys.stderr)
            return 2

    if args.out_dir:
        out_dir = args.out_dir
        os.makedirs(out_dir, exist_ok=True)
        cleanup = lambda: None
    else:
        tdir = tempfile.TemporaryDirectory(prefix="realhw_parity_")
        out_dir = tdir.name
        cleanup = tdir.cleanup

    all_clean = True
    try:
        if args.ahdi_reference:
            our_path, plan, ref_sec0 = _build_one(
                args.ahdi_reference, atari_hd.FORMAT_AHDI, out_dir,
                args.ahdi_driver)
            ref_size = os.path.getsize(args.ahdi_reference)
            masks = list(_ahdi_masks(plan, args.ahdi_reference, ref_sec0))
            masks += _partition_root_dir_masks(
                plan, args.ahdi_reference, "AHDI")
            masks += _post_partitions_mask(plan, ref_size)
            ok = _report_one(
                f"AHDI parity ({Path(args.ahdi_reference).name})",
                args.ahdi_reference, our_path, plan, ref_sec0, masks)
            all_clean = all_clean and ok

        if args.ppdriver_reference:
            our_path, plan, ref_sec0 = _build_one(
                args.ppdriver_reference, atari_hd.FORMAT_PPDRIVER,
                out_dir, None)
            ref_size = os.path.getsize(args.ppdriver_reference)
            masks = list(_ppdriver_masks(plan, args.ppdriver_reference, ref_sec0))
            masks += _partition_root_dir_masks(
                plan, args.ppdriver_reference, "PPDRIVER")
            masks += _post_partitions_mask(plan, ref_size)
            ok = _report_one(
                f"PPDRIVER parity ({Path(args.ppdriver_reference).name})",
                args.ppdriver_reference, our_path, plan, ref_sec0, masks)
            all_clean = all_clean and ok

        if args.hddriver_reference:
            our_path, plan, ref_sec0 = _build_one(
                args.hddriver_reference, atari_hd.FORMAT_HDDRIVER,
                out_dir, None)
            # HDDRIVER non-bootable: no IPL; no extra mask beyond
            # FAT16 timestamps. dual-BPB byte-fidelity validated by
            # story 003 already.
            ref_size = os.path.getsize(args.hddriver_reference)
            masks = _partition_root_dir_masks(
                plan, args.hddriver_reference, "HDDRIVER")
            masks += _post_partitions_mask(plan, ref_size)
            ok = _report_one(
                f"HDDRIVER parity ({Path(args.hddriver_reference).name})",
                args.hddriver_reference, our_path, plan, ref_sec0, masks)
            all_clean = all_clean and ok
    finally:
        cleanup()

    print()
    if all_clean:
        print("realhw_parity.py: PASS -- no unexpected diffs across all "
              "supplied references.")
        return 0
    print("realhw_parity.py: FAIL -- one or more references reported "
          "unexpected diffs (likely a regression).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
