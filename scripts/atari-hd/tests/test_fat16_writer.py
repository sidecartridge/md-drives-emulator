"""Structural lock-down for atari_hd.format_fat16().

format_fat16() is the keystone the dual-BPB hybrid layout relies on: a
drifted BPB field would break mount silently. This story locks the
writer's on-disk layout behind structural assertions across a fixture
matrix that covers single-BPB and dual-BPB DOS-view shapes plus the
sector_size > 512 native path.

This is NOT a byte-parity check vs. mkfs.vfat -- that's epic-001 /
story 003 (Linux-only). Here we assert structural correctness only,
so the suite stays portable across macOS / Linux / Windows.
"""

import os
import tempfile
import unittest
from pathlib import Path

from _support import atari_hd, parse_bpb


# (id, partition_sectors_512, sector_size, sectors_per_cluster,
#  reserved_sectors, label).
#
# Coverage:
#  - 8 MB single-BPB at AHDI native (bps=512 spc=2).
#  - 128 MB / 256 MB dual-BPB DOS view (bps=512, spc=2*ratio,
#    res=ratio+1) at ratios 4 and 8.
#  - 1 GB single-BPB at the FAT16 upper end (spc=32).
#  - Native bps=1024 / bps=4096 to exercise sector_size > 512.
FIXTURES = [
    ("ahdi_8M_native",         16384,    512,  2,  2, "ATARI"),
    ("ppdriver_dos_128M_r4",   262144,   512,  8,  5, "DOS"),
    ("ppdriver_dos_256M_r8",   524288,   512, 16,  9, "DOS"),
    ("single_bpb_1G",          2097152,  512, 32,  2, "LARGE"),
    ("native_1k_16M",          32768,    1024, 2,  1, "TOS1K"),
    ("native_4k_128M",         262144,   4096, 2,  1, "TOS4K"),
]


@unittest.skipUnless(hasattr(atari_hd, "format_fat16"),
                     "atari_hd.format_fat16 not present "
                     "(epic-001 / story 001 must land first)")
class TestFat16Writer(unittest.TestCase):
    def test_fixture_matrix(self):
        with tempfile.TemporaryDirectory() as tmp:
            for fid, p_sec, ssize, spc, res, label in FIXTURES:
                with self.subTest(id=fid, sector_size=ssize, spc=spc,
                                  res=res):
                    self._check_fixture(tmp, fid, p_sec, ssize, spc, res,
                                        label)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _check_fixture(self, tmp, fid, p_sec, ssize, spc, res, label):
        path = Path(tmp) / f"{fid}.img"
        atari_hd.format_fat16(
            out_path=str(path),
            partition_sectors_512=p_sec,
            sector_size=ssize,
            sectors_per_cluster=spc,
            reserved_sectors=res,
            label=label,
            disable_fat_align=True,
        )

        # File size is exact: partition_sectors_512 * 512.
        self.assertEqual(os.path.getsize(path), p_sec * 512,
                         f"{fid}: file size != partition_sectors_512 * 512")

        with open(path, "rb") as f:
            head = f.read(ssize)  # full first logical sector
        bpb = parse_bpb(head, 0)

        # Derived expectations.
        ratio = ssize // 512
        total_logical = p_sec // ratio
        spfat_expected = atari_hd._fat16_compute_spfat(
            p_sec, ssize, spc, res)
        label_bytes = atari_hd._fat16_normalize_label(label)
        vol_id_expected = atari_hd._fat16_seed_volume_id(
            label_bytes, p_sec, ssize, spc, res)

        # BPB field assertions ---------------------------------------
        self.assertEqual(bpb["jump"], b"\xEB\x3C\x90", f"{fid}: jump")
        self.assertEqual(bpb["oem"], b"MSWIN4.1", f"{fid}: OEM")
        self.assertEqual(bpb["bytes_per_sector"], ssize,
                         f"{fid}: bytes_per_sector")
        self.assertEqual(bpb["sectors_per_cluster"], spc,
                         f"{fid}: sectors_per_cluster")
        self.assertEqual(bpb["reserved_sectors"], res,
                         f"{fid}: reserved_sectors")
        self.assertEqual(bpb["num_fats"], 2, f"{fid}: num_fats")
        self.assertEqual(bpb["root_entries"], 512, f"{fid}: root_entries")
        if total_logical < 0x10000:
            self.assertEqual(bpb["total_sectors_16"], total_logical,
                             f"{fid}: total_sectors_16")
            self.assertEqual(bpb["total_sectors_32"], 0,
                             f"{fid}: total_sectors_32 should be 0")
        else:
            self.assertEqual(bpb["total_sectors_16"], 0,
                             f"{fid}: total_sectors_16 should be 0")
            self.assertEqual(bpb["total_sectors_32"], total_logical,
                             f"{fid}: total_sectors_32")
        self.assertEqual(bpb["media_descriptor"], 0xF8,
                         f"{fid}: media_descriptor")
        self.assertEqual(bpb["sectors_per_fat_16"], spfat_expected,
                         f"{fid}: sectors_per_fat_16 (expect "
                         f"_fat16_compute_spfat)")
        self.assertEqual(bpb["sectors_per_track"], 32, f"{fid}: spt")
        self.assertEqual(bpb["num_heads"], 64, f"{fid}: num_heads")
        self.assertEqual(bpb["hidden_sectors"], 0, f"{fid}: hidden_sectors")
        self.assertEqual(bpb["drive_number"], 0x80, f"{fid}: drive_number")
        self.assertEqual(bpb["extended_boot_signature"], 0x29,
                         f"{fid}: extended_boot_signature")
        self.assertEqual(bpb["volume_id"], vol_id_expected,
                         f"{fid}: volume_id (expect deterministic seed)")
        self.assertEqual(bpb["volume_label"], label_bytes.ljust(11, b" "),
                         f"{fid}: volume_label")
        self.assertEqual(bpb["fs_type"], b"FAT16   ", f"{fid}: fs_type")
        self.assertEqual(bpb["signature"], b"\x55\xAA",
                         f"{fid}: 0x55AA boot signature")

        # FAT contents -----------------------------------------------
        # Both FAT copies are byte-identical; FAT[0..3] = F8 FF FF FF
        # and the rest of each FAT is zero (no clusters allocated).
        fat_size_bytes = spfat_expected * ssize
        with open(path, "rb") as f:
            f.seek(res * ssize)
            fat1 = f.read(fat_size_bytes)
            fat2 = f.read(fat_size_bytes)
        self.assertEqual(len(fat1), fat_size_bytes,
                         f"{fid}: FAT1 short read")
        self.assertEqual(fat1[0:4], b"\xF8\xFF\xFF\xFF",
                         f"{fid}: FAT[0..3] head")
        self.assertEqual(fat1[4:], b"\x00" * (fat_size_bytes - 4),
                         f"{fid}: FAT1 tail must be all-zero "
                         "(no clusters allocated yet)")
        self.assertEqual(fat1, fat2,
                         f"{fid}: FAT1 != FAT2 (must be byte-identical)")

        # Root directory ---------------------------------------------
        # Exactly one entry: the volume label. Attribute 0x08, all other
        # fields zero. Remaining root-dir entries are all zero.
        root_dir_bytes = 512 * 32
        root_dir_offset_bytes = (res + 2 * spfat_expected) * ssize
        with open(path, "rb") as f:
            f.seek(root_dir_offset_bytes)
            root = f.read(root_dir_bytes)
        self.assertEqual(len(root), root_dir_bytes,
                         f"{fid}: root dir short read")
        # First 32 bytes: label entry.
        self.assertEqual(root[0:11], label_bytes.ljust(11, b" "),
                         f"{fid}: root entry name field")
        self.assertEqual(root[11], 0x08,
                         f"{fid}: root entry attribute byte (must be 0x08)")
        self.assertEqual(root[12:32], b"\x00" * 20,
                         f"{fid}: root entry tail must be zero "
                         "(timestamps + cluster + size)")
        # Remaining entries all-zero.
        self.assertEqual(root[32:], b"\x00" * (root_dir_bytes - 32),
                         f"{fid}: subsequent root entries must be empty")

        # Data area sample-check -------------------------------------
        # Read 4 KiB at start, mid, and end of the data area; assert
        # all-zero. Sampling avoids reading the full image for large
        # fixtures.
        root_dir_sectors = (root_dir_bytes + ssize - 1) // ssize
        data_start_byte = (
            res + 2 * spfat_expected + root_dir_sectors) * ssize
        data_end_byte = total_logical * ssize
        self.assertGreater(data_end_byte, data_start_byte,
                           f"{fid}: no data area present")
        sample_size = 4096
        positions = [
            data_start_byte,
            (data_start_byte + data_end_byte) // 2,
            data_end_byte - sample_size,
        ]
        zero_block = b"\x00" * sample_size
        with open(path, "rb") as f:
            for pos in positions:
                f.seek(pos)
                self.assertEqual(f.read(sample_size), zero_block,
                                 f"{fid}: data area at byte 0x{pos:X} "
                                 "must be zero")


if __name__ == "__main__":
    unittest.main()
