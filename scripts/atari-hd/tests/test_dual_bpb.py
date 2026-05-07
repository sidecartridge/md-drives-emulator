"""Dual-BPB invariant tests for PPDRIVER / HDDRIVER hybrid images.

Both hybrid layouts carry two BPBs at the start of every partition:
  - DOS view: bytes_per_sector=512, so macOS msdosfs / Linux vfat /
    Windows can mount the volume.
  - TOS view: bytes_per_sector = ratio * 512 (>=1024), with a one-LBA
    offset so its FAT/root/data physical bytes coincide with DOS's.

Six invariants must hold or one of the two views breaks. This story
asserts every invariant for every supported (format, ratio) combination.

ratio=1 is intentionally absent: synthesize_tos_bpb_from_dos512 hard-
rejects tos_bps < 1024 because the TOS view would degenerate to the
DOS view and the dual-BPB trick would be meaningless.
"""

import tempfile
import unittest
from pathlib import Path

from _support import atari_hd, parse_bpb


# (ratio, partition_size_mb) pairs chosen to land cleanly in each ratio
# bucket per choose_logical_sector_size() and to require no
# align_partition_for_hybrid() trim (predict_mkfs_spfat % ratio == 0).
RATIO_FIXTURES = [
    (2,   32),
    (4,   96),
    (8,   128),
    (16,  384),
]

HYBRID_FORMATS = (atari_hd.FORMAT_PPDRIVER, atari_hd.FORMAT_HDDRIVER)


def _check_invariant(test, num, condition, dos, tos, ratio, fmt):
    """Assert one of the six invariants. Failure message names the
    invariant number, the format, the ratio, and the relevant DOS/TOS
    field values per spec criterion 2."""
    test.assertTrue(condition,
                    f"\n  invariant #{num} broken (format={fmt}, "
                    f"ratio={ratio})\n"
                    f"  DOS bps={dos['bytes_per_sector']} "
                    f"spc={dos['sectors_per_cluster']} "
                    f"res={dos['reserved_sectors']} "
                    f"spfat={dos['sectors_per_fat_16']}\n"
                    f"  TOS bps={tos['bytes_per_sector']} "
                    f"spc={tos['sectors_per_cluster']} "
                    f"res={tos['reserved_sectors']} "
                    f"spfat={tos['sectors_per_fat_16']}")


@unittest.skipUnless(hasattr(atari_hd, "format_fat16"),
                     "atari_hd.format_fat16 not present "
                     "(epic-001 / story 001 must land first)")
class TestDualBpbInvariants(unittest.TestCase):
    def test_invariants_across_format_and_ratio(self):
        with tempfile.TemporaryDirectory() as tmp:
            for fmt in HYBRID_FORMATS:
                for ratio, size_mb in RATIO_FIXTURES:
                    with self.subTest(format=fmt, ratio=ratio,
                                      size_mb=size_mb):
                        self._check_one(tmp, fmt, ratio, size_mb)

    def _check_one(self, tmp, fmt, ratio, size_mb):
        # The hybrid PRIMARY cap is HYBRID_PRIMARY_MAX_MB (255 MB):
        # real PPDRIVER / HDDRIVER reject primaries with bps>4096.
        # When the test fixture wants a partition larger than that,
        # we need to place it in the extended chain. PPDRIVER allows
        # up to 4 primaries (with the per-primary 255 MB cap) and
        # only spills to the extended chain at N >= 5; HDDRIVER caps
        # primaries at 1 and spills at N >= 2. To exercise the
        # extended chain on either format the test pads with the
        # right number of small dummy primaries first, then places
        # the test partition at slot=primary_count.
        # Per-partition is_extended: the test partition must be
        # extended (is_extended=True) when its size exceeds the
        # hybrid primary cap, so it lands at bps=8192 in the chain.
        # Pad with a single small primary at slot 0 to satisfy the
        # "first slot must be primary" rule -- one is enough now
        # that PPDRIVER allows the second slot onwards to be
        # extended directly without having to fill all 4 primary
        # slots first.
        if size_mb > atari_hd.HYBRID_PRIMARY_MAX_MB:
            min_p = atari_hd.HYBRID_MIN_PARTITION_MB
            partitions = [
                atari_hd.Partition(name="BOOT", size_mb=min_p),
                atari_hd.Partition(name="P", size_mb=size_mb,
                                    is_extended=True),
            ]
            test_slot = 1
            image_mb = size_mb + min_p + 4
        else:
            partitions = [atari_hd.Partition(name="P", size_mb=size_mb)]
            test_slot = 0
            image_mb = size_mb + 4
        plan = atari_hd.plan_image(
            fmt, image_path="<placeholder>",
            image_mb=image_mb, partitions=partitions, strict_tos=False)

        # Verify the geometry chose the ratio we expected. If not, the
        # rest of the test is meaningless.
        actual_ratio = plan.partitions[test_slot].tos_bps // 512
        self.assertEqual(
            actual_ratio, ratio,
            f"format={fmt} size={size_mb} MB: planner chose "
            f"ratio={actual_ratio}, fixture expected {ratio}")

        path = Path(tmp) / f"{fmt}_r{ratio}.img"
        plan.image_path = str(path)
        atari_hd.build_image(plan)

        # Read DOS BPB at start_lba and TOS BPB at start_lba + 1.
        # Both BPBs are 512-byte structures regardless of the declared
        # bytes_per_sector; parse_bpb is sector-size-agnostic.
        first_lba = plan.partitions[test_slot].start_lba
        with open(path, "rb") as f:
            f.seek(first_lba * 512)
            dos_buf = f.read(512)
            tos_buf = f.read(512)

        dos = parse_bpb(dos_buf, 0)
        tos = parse_bpb(tos_buf, 0)

        # Sanity: the BPBs we just read must declare the bps we expect.
        # If they don't, every other assertion is checking the wrong
        # bytes; surface that loudly before running the invariants.
        self.assertEqual(dos["bytes_per_sector"], 512,
                         f"DOS BPB at LBA {first_lba} declares "
                         f"bps={dos['bytes_per_sector']}, expected 512")
        self.assertEqual(tos["bytes_per_sector"], ratio * 512,
                         f"TOS BPB at LBA {first_lba + 1} declares "
                         f"bps={tos['bytes_per_sector']}, expected "
                         f"{ratio * 512}")

        # Six invariants ---------------------------------------------
        # #1: cluster_size_bytes match (DOS bps * spc == TOS bps * spc).
        dos_cluster_bytes = (dos["bytes_per_sector"]
                             * dos["sectors_per_cluster"])
        tos_cluster_bytes = (tos["bytes_per_sector"]
                             * tos["sectors_per_cluster"])
        _check_invariant(self, 1,
                         dos_cluster_bytes == tos_cluster_bytes,
                         dos, tos, ratio, fmt)

        # #2: FAT_bytes match (DOS spfat * 512 == TOS spfat * (ratio*512)).
        dos_fat_bytes = (dos["sectors_per_fat_16"]
                         * dos["bytes_per_sector"])
        tos_fat_bytes = (tos["sectors_per_fat_16"]
                         * tos["bytes_per_sector"])
        _check_invariant(self, 2,
                         dos_fat_bytes == tos_fat_bytes,
                         dos, tos, ratio, fmt)

        # #3: DOS_reserved_sectors == ratio + 1.
        _check_invariant(self, 3,
                         dos["reserved_sectors"] == ratio + 1,
                         dos, tos, ratio, fmt)

        # #4: TOS_reserved_sectors == 1.
        _check_invariant(self, 4,
                         tos["reserved_sectors"] == 1,
                         dos, tos, ratio, fmt)

        # #5: DOS_sectors_per_cluster == 2 * ratio.
        _check_invariant(self, 5,
                         dos["sectors_per_cluster"] == 2 * ratio,
                         dos, tos, ratio, fmt)

        # #6: TOS_sectors_per_cluster == 2.
        _check_invariant(self, 6,
                         tos["sectors_per_cluster"] == 2,
                         dos, tos, ratio, fmt)


if __name__ == "__main__":
    unittest.main()
