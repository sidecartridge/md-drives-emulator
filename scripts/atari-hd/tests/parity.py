#!/usr/bin/env python3
"""parity.py - byte-level diff between format_fat16() and mkfs.vfat.

Developer aid only. Runs on Linux / macOS hosts that have mkfs.vfat (or
mkdosfs) on PATH; skips elsewhere with a clear message. Not part of CI
-- the host-portable test suite (epic-002) is what gates merge.

For each fixture, this harness:
  1. Generates image A by invoking mkfs.vfat with the fixture parameters.
  2. Generates image B by calling format_fat16() with the same parameters.
  3. Diffs A vs B in 4 MiB chunks, masking only fields documented as
     non-deterministic between the two writers (volume serial, boot-code
     stub, root-dir entry timestamps, OEM).
  4. Reports the first 5 mismatches per fixture with hex context.

Exit code: 0 on full parity, 1 on any mismatch, 2 if skipped.

Run from the repo root:
    python scripts/atari-hd/tests/parity.py
"""

import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import atari_hd  # noqa: E402


CHUNK_SIZE = 4 * 1024 * 1024
MAX_MISMATCHES_PER_FIXTURE = 5

# Mask intervals (low inclusive, high exclusive) within the boot sector.
# These are regions where byte-equality is *not* expected:
#   - OEM: format_fat16 writes "MSWIN4.1"; dosfstools writes "mkfs.fat".
#          The OEM is overwritten downstream by partition-table writers
#          anyway (PPGDODBC, GENESIS, etc.), so the test ignores it.
#   - CHS geometry: dosfstools picks num_heads (and sometimes
#          sectors_per_track) from a geometry table indexed by image
#          size; format_fat16 hard-codes 32/64. These fields are
#          vestigial for raw images that never sit on a CHS disk.
#   - Volume serial: dosfstools seeds from the wall clock; we seed from a
#          deterministic hash of inputs.
#   - Boot code: dosfstools ships an x86 stub; we fill HLT (0xF4).
BOOT_MASK = [
    (0x03, 0x0B),
    (0x18, 0x1C),
    (0x27, 0x2B),
    (0x3E, 0x1FE),
]

# Mask offsets within the 32-byte volume-label root-dir entry. dosfstools
# populates timestamps from the wall clock; format_fat16 leaves them at 0.
LABEL_ENTRY_MASK = [
    (0x0D, 0x14),    # creation time/date + last access date
    (0x16, 0x1A),    # write time/date
]


# Fixtures. Each entry is the kwargs format_fat16() accepts plus an `id`.
# Sizes were chosen to exercise:
#   - small partitions near FAT16's lower cluster boundary
#   - the dual-BPB DOS view (bps=512, spc=2*ratio, res=ratio+1)
#   - native single-BPB layouts at every supported sector size > 512
FIXTURES = [
    dict(id="dos_view_8M",
         partition_sectors_512=16384, sector_size=512,
         sectors_per_cluster=2, reserved_sectors=2, label="ATARI",
         disable_fat_align=True),
    dict(id="dos_view_128M_ratio4",
         partition_sectors_512=262144, sector_size=512,
         sectors_per_cluster=8, reserved_sectors=5, label="DOS",
         disable_fat_align=True),
    # 1 GB single-BPB. Dual-BPB at this size would need ratio=16 (TOS bps=8192,
    # spc=32, res=17), which is at the very edge of what FAT16 supports; the
    # plain single-BPB layout is more representative of typical AHDI use.
    dict(id="single_bpb_1G",
         partition_sectors_512=2097152, sector_size=512,
         sectors_per_cluster=32, reserved_sectors=2, label="LARGE",
         disable_fat_align=True),
    dict(id="native_1k_16M",
         partition_sectors_512=32768, sector_size=1024,
         sectors_per_cluster=2, reserved_sectors=1, label="TOS1K",
         disable_fat_align=True),
    dict(id="native_2k_32M",
         partition_sectors_512=65536, sector_size=2048,
         sectors_per_cluster=2, reserved_sectors=1, label="TOS2K",
         disable_fat_align=True),
    dict(id="native_4k_128M",
         partition_sectors_512=262144, sector_size=4096,
         sectors_per_cluster=2, reserved_sectors=1, label="TOS4K",
         disable_fat_align=True),
]


def find_mkfs_vfat():
    """Return path to mkfs.vfat / mkdosfs, or None if neither is found."""
    extras = ["/sbin", "/usr/sbin",
              "/usr/local/sbin", "/usr/local/bin",
              "/opt/homebrew/sbin", "/opt/homebrew/bin"]
    for name in ("mkfs.vfat", "mkdosfs"):
        path = shutil.which(name)
        if path:
            return path
        for d in extras:
            cand = os.path.join(d, name)
            if os.path.isfile(cand) and os.access(cand, os.X_OK):
                return cand
    return None


def run_mkfs_vfat(mkfs_path, out_path, partition_sectors_512, sector_size,
                  sectors_per_cluster, reserved_sectors, label,
                  disable_fat_align):
    """Invoke mkfs.vfat to produce a reference FAT16 image."""
    total_bytes = partition_sectors_512 * 512
    blocks_1k = total_bytes // 1024
    cmd = [mkfs_path, "-F", "16",
           "-S", str(sector_size),
           "-s", str(sectors_per_cluster),
           "-R", str(reserved_sectors),
           "-n", label]
    if disable_fat_align:
        cmd.append("-a")
    cmd.extend(["-C", str(out_path), str(blocks_1k)])
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"mkfs.vfat exit {result.returncode}\n"
            f"  cmd: {' '.join(cmd)}\n"
            f"  out: {result.stdout.strip()}\n"
            f"  err: {result.stderr.strip()}\n")


def compute_root_dir_offset(partition_sectors_512, sector_size,
                            sectors_per_cluster, reserved_sectors):
    """Absolute byte offset of the first root-dir entry, computed from
    the same iteration format_fat16 itself uses."""
    spfat = atari_hd._fat16_compute_spfat(
        partition_sectors_512, sector_size, sectors_per_cluster,
        reserved_sectors)
    return (reserved_sectors + atari_hd.FAT16_NUM_FATS * spfat) * sector_size


def build_ignore_intervals(partition_sectors_512, sector_size,
                           sectors_per_cluster, reserved_sectors):
    """Return absolute-byte-offset ignore intervals for one fixture."""
    intervals = list(BOOT_MASK)
    root_dir_offset = compute_root_dir_offset(
        partition_sectors_512, sector_size, sectors_per_cluster,
        reserved_sectors)
    for lo, hi in LABEL_ENTRY_MASK:
        intervals.append((root_dir_offset + lo, root_dir_offset + hi))
    return intervals


def mask_in_place(buf, base_offset, intervals):
    """Zero bytes in `buf` corresponding to the absolute-offset intervals
    overlapping [base_offset, base_offset+len(buf))."""
    for lo, hi in intervals:
        local_lo = max(lo - base_offset, 0)
        local_hi = min(hi - base_offset, len(buf))
        if local_lo < local_hi:
            buf[local_lo:local_hi] = b"\x00" * (local_hi - local_lo)


def diff_files(path_a, path_b, intervals):
    """Diff two files, ignoring intervals. Returns the first
    MAX_MISMATCHES_PER_FIXTURE mismatches; each is (offset, byte_a, byte_b)."""
    size_a = os.path.getsize(path_a)
    size_b = os.path.getsize(path_b)
    if size_a != size_b:
        raise RuntimeError(
            f"file sizes differ: A={size_a} B={size_b} "
            f"(delta={size_b - size_a})")
    mismatches = []
    pos = 0
    with open(path_a, "rb") as fa, open(path_b, "rb") as fb:
        while True:
            ca = bytearray(fa.read(CHUNK_SIZE))
            cb = bytearray(fb.read(CHUNK_SIZE))
            if not ca and not cb:
                break
            mask_in_place(ca, pos, intervals)
            mask_in_place(cb, pos, intervals)
            if ca == cb:
                pos += len(ca)
                continue
            for i in range(len(ca)):
                if ca[i] == cb[i]:
                    continue
                mismatches.append((pos + i, ca[i], cb[i]))
                if len(mismatches) >= MAX_MISMATCHES_PER_FIXTURE:
                    return mismatches
            pos += len(ca)
    return mismatches


def hex_context(path, offset, span_before=16, span_after=16):
    """Read up to span_before+1+span_after bytes around offset and return a
    hex string with the target byte highlighted in [brackets]."""
    start = max(offset - span_before, 0)
    end = offset + span_after + 1
    with open(path, "rb") as f:
        f.seek(start)
        buf = f.read(end - start)
    target_idx = offset - start
    pieces = []
    for i, b in enumerate(buf):
        s = f"{b:02x}"
        if i == target_idx:
            s = f"[{s}]"
        pieces.append(s)
    return " ".join(pieces)


def run_fixture(mkfs_path, fixture, tmpdir):
    fid = fixture["id"]
    kwargs = {k: v for k, v in fixture.items() if k != "id"}
    path_a = pathlib.Path(tmpdir) / f"{fid}_mkfs.img"
    path_b = pathlib.Path(tmpdir) / f"{fid}_pyfmt.img"

    print(f"  [{fid}] mkfs.vfat ...", flush=True)
    run_mkfs_vfat(mkfs_path, path_a, **kwargs)

    print(f"  [{fid}] format_fat16 ...", flush=True)
    atari_hd.format_fat16(out_path=str(path_b), **kwargs)

    intervals = build_ignore_intervals(
        kwargs["partition_sectors_512"], kwargs["sector_size"],
        kwargs["sectors_per_cluster"], kwargs["reserved_sectors"])

    print(f"  [{fid}] diff ...", flush=True)
    mismatches = diff_files(str(path_a), str(path_b), intervals)
    if not mismatches:
        print(f"  [{fid}] OK")
        return True
    print(f"  [{fid}] {len(mismatches)} mismatch(es) "
          f"(showing up to {MAX_MISMATCHES_PER_FIXTURE}):")
    for off, ba, bb in mismatches:
        print(f"    @0x{off:08x}  A=0x{ba:02x}  B=0x{bb:02x}")
        print(f"      A ctx: {hex_context(str(path_a), off)}")
        print(f"      B ctx: {hex_context(str(path_b), off)}")
    return False


def main():
    mkfs_path = find_mkfs_vfat()
    if not mkfs_path:
        sys.stderr.write(
            "parity.py: mkfs.vfat / mkdosfs not found on PATH; skipping.\n"
            "  This is a developer aid for hosts where dosfstools is\n"
            "  available (Linux / macOS with `brew install dosfstools`).\n")
        return 2

    print(f"parity.py: using {mkfs_path}")
    failed = []
    with tempfile.TemporaryDirectory() as tmpdir:
        for fix in FIXTURES:
            try:
                ok = run_fixture(mkfs_path, fix, tmpdir)
            except Exception as e:
                print(f"  [{fix['id']}] ERROR: {e}")
                failed.append(fix["id"])
                continue
            if not ok:
                failed.append(fix["id"])

    print()
    if failed:
        print(f"FAIL: {len(failed)}/{len(FIXTURES)} fixture(s) had "
              "mismatches:")
        for fid in failed:
            print(f"  - {fid}")
        return 1
    print(f"PASS: all {len(FIXTURES)} fixtures byte-equal "
          "(modulo masked fields).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
