"""Lock-down test for atari_hd.predict_mkfs_spfat().

predict_mkfs_spfat() is the single source of truth for sectors-per-FAT
in the dual-BPB hybrid layout. If the formula ever drifts the hybrid
breaks silently. This test pins it behind a fixture table of known-good
(input -> output) rows.

The expected values were generated against the live function and
spot-checked against mkfs.vfat (dosfstools 4.2) by
scripts/atari-hd/tests/parity.py for the rows that overlap with
parity-test fixtures. Rows marked source="formula" have not yet been
cross-checked against mkfs.vfat directly; that's a soft TODO when
convenient.
"""

import unittest

from _support import atari_hd


# id, partition_sectors_512, ratio, expected_spfat, source, notes
FIXTURES = [
    # Below the smallest valid FAT16: data area would be <= 0 even at
    # spfat=1 -- the function must return None, not iterate forever.
    ("too_small_r1",       36,        1, None,  "formula",
     "below the spfat=1 floor at ratio=1"),
    ("too_small_r4",       39,        4, None,  "formula",
     "below the spfat=1 floor at ratio=4"),

    # Smallest practical FAT16 at ratio=1: cluster_count == 4085 exactly,
    # right at the FAT12/FAT16 boundary.
    ("min_fat16_r1",       8236,      1, 16,    "formula",
     "smallest valid FAT16, clusters=4085 exactly"),

    # 16 / 32 MB at ratio=1
    ("16M_r1",             32768,     1, 64,    "formula", ""),
    ("32M_r1",             65536,     1, 128,   "formula", ""),

    # Largest valid FAT16 at ratio=1: clusters=65523, just below the
    # FAT32 boundary at 65524.
    ("max_fat16_r1",       131592,    1, 256,   "formula",
     "largest valid FAT16, clusters=65523"),

    # ratio=2 sweep
    ("32M_r2",             65536,     2, 64,    "formula", ""),
    ("64M_r2",             131072,    2, 128,   "formula", ""),
    ("128M_r2",            262144,    2, 256,   "formula", ""),

    # ratio=4 sweep -- 128M_r4 lines up with parity.py's
    # dos_view_128M_ratio4 fixture which is byte-checked against mkfs.vfat.
    ("64M_r4",             131072,    4, 64,    "formula", ""),
    ("128M_r4",            262144,    4, 128,   "mkfs_vfat_4.2",
     "matches parity.py dos_view_128M_ratio4"),
    ("256M_r4",            524288,    4, 256,   "formula", ""),

    # ratio=8 sweep
    ("128M_r8",            262144,    8, 64,    "formula", ""),
    ("256M_r8",            524288,    8, 128,   "formula", ""),
    ("512M_r8",            1048576,   8, 256,   "formula", ""),

    # The historical spfat=127 quirk: at ~128 MB with ratio=4 the
    # iteration converges on an odd spfat (not divisible by ratio),
    # which is what tripped the dual-BPB layer in the original PPERA
    # work and prompted align_partition_for_hybrid().
    ("spfat_127_quirk",    259072,    4, 127,   "formula",
     "spfat not divisible by ratio -- triggers align_partition_for_hybrid()"),
]


class TestPredictMkfsSpfat(unittest.TestCase):
    def test_fixture_table(self):
        for row_id, sectors, ratio, expected, source, notes in FIXTURES:
            with self.subTest(id=row_id, sectors=sectors, ratio=ratio,
                              source=source):
                actual = atari_hd.predict_mkfs_spfat(sectors, ratio)
                self.assertEqual(
                    actual, expected,
                    msg=(f"\n  fixture: {row_id}\n"
                         f"  inputs : sectors={sectors}, ratio={ratio}\n"
                         f"  expect : {expected}\n"
                         f"  actual : {actual}\n"
                         f"  source : {source}"
                         + (f"\n  notes  : {notes}" if notes else "")))

    def test_fixture_table_has_required_coverage(self):
        # Spec calls for at least 10 rows. Sanity-check that nobody
        # silently shrinks the table below that floor.
        self.assertGreaterEqual(len(FIXTURES), 10,
                                f"only {len(FIXTURES)} fixtures defined; "
                                "spec requires >= 10")
        ratios = {row[2] for row in FIXTURES}
        self.assertTrue({1, 2, 4, 8}.issubset(ratios),
                        f"ratios covered: {ratios}; spec requires "
                        "1, 2, 4, and 8")


if __name__ == "__main__":
    unittest.main()
