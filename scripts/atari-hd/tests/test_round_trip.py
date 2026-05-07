"""Round-trip parse-back tests.

Builds a real image via atari_hd.build_image() into a temporary
directory, then parses the partition table (and any extended chain)
back from disk and asserts the recovered list matches the plan.

This is the only honest end-to-end check for the writers' offsets,
chain pointers, and relative-LBA conventions. Each test exercises
both primary slots and an extended chain (XGM for AHDI, EBR for the
hybrid formats).

Sizes are chosen to be the smallest the build pipeline accepts:
  - AHDI partitions can be ~5 MB (single-BPB FAT16 minimum).
  - PPDRIVER / HDDRIVER partitions need >=32 MB so the synthesized
    TOS BPB has tos_bps >= 1024 (synthesize_tos_bpb_from_dos512
    rejects anything smaller).

All artifacts go through tempfile.TemporaryDirectory() so cleanup is
automatic; nothing is committed to git.
"""

import tempfile
import unittest
from pathlib import Path

from _support import atari_hd, walk_image


# MBR partition type expected for FAT16 primaries / logicals.
MBR_TYPE_FAT16 = 0x06


def _build(plan, tmpdir):
    """Run atari_hd.build_image() into `tmpdir`. Returns the image path."""
    plan.image_path = str(Path(tmpdir) / "image.img")
    atari_hd.build_image(plan)
    return plan.image_path


class TestRoundTrip(unittest.TestCase):
    def test_ahdi_xgm_chain(self):
        # 5 partitions => layout splits 3 primary + 2 logicals via XGM
        # chain. Smallest AHDI partition that format_fat16 accepts is
        # ~5 MB; we use 5 MB across the board.
        partitions = [
            atari_hd.Partition(name=f"P{i + 1}", size_mb=5)
            for i in range(5)
        ]
        plan = atari_hd.plan_image(
            atari_hd.FORMAT_AHDI, image_path="<placeholder>",
            image_mb=32, partitions=partitions, strict_tos=False)

        with tempfile.TemporaryDirectory() as tmp:
            image_path = _build(plan, tmp)
            recovered = walk_image(image_path, atari_hd.FORMAT_AHDI)

        self.assertEqual(len(recovered), len(plan.partitions),
                         f"recovered {len(recovered)} partitions, "
                         f"expected {len(plan.partitions)}")

        for i, (got, expected) in enumerate(zip(recovered, plan.partitions)):
            with self.subTest(slot=i, partition=expected.name):
                self.assertEqual(got["start_lba"], expected.start_lba,
                                 f"slot {i} start_lba")
                self.assertEqual(got["size_sectors"], expected.size_sectors,
                                 f"slot {i} size_sectors")
                # All AHDI partitions <=32 MB resolve to GEM (first slot
                # is forced GEM regardless; the rest fall under the 32 MB
                # threshold for permissive mode).
                self.assertEqual(got["ident"], b"GEM",
                                 f"slot {i} ident")

    def test_ppdriver_ebr_chain(self):
        # 5 partitions: 3 primary (slots 0..2) + 2 extended (slots 3..4).
        # The user picks primary vs extended per partition via
        # Partition.is_extended; this fixture mirrors what a typical
        # multi-primary PPDRIVER layout looks like (max 4 primaries on
        # PPDRIVER; here we use 3 + 2 to exercise both halves of the
        # writer). 32 MB is the smallest size that hits tos_bps>=1024.
        partitions = [
            atari_hd.Partition(name=f"P{i + 1}", size_mb=32,
                                is_extended=(i >= 3))
            for i in range(5)
        ]
        plan = atari_hd.plan_image(
            atari_hd.FORMAT_PPDRIVER, image_path="<placeholder>",
            image_mb=32 * 5 + 4, partitions=partitions, strict_tos=False)

        with tempfile.TemporaryDirectory() as tmp:
            image_path = _build(plan, tmp)
            recovered = walk_image(image_path, atari_hd.FORMAT_PPDRIVER)

        self.assertEqual(len(recovered), len(plan.partitions))
        for i, (got, expected) in enumerate(zip(recovered, plan.partitions)):
            with self.subTest(slot=i, partition=expected.name):
                self.assertEqual(got["start_lba"], expected.start_lba,
                                 f"slot {i} start_lba")
                self.assertEqual(got["size_sectors"], expected.size_sectors,
                                 f"slot {i} size_sectors")
                self.assertEqual(got["part_type"], MBR_TYPE_FAT16,
                                 f"slot {i} part_type")

    def test_hddriver_ebr_chain(self):
        # 2 partitions: 1 primary + 1 extended. HDDRIVER's primary cap
        # is 1, so the second partition MUST be is_extended=True.
        # 32 MB partitions for the same hybrid (tos_bps>=1024) reason.
        partitions = [
            atari_hd.Partition(name="BOOT", size_mb=32),
            atari_hd.Partition(name="DATA", size_mb=32, is_extended=True),
        ]
        plan = atari_hd.plan_image(
            atari_hd.FORMAT_HDDRIVER, image_path="<placeholder>",
            image_mb=72, partitions=partitions, strict_tos=False)

        with tempfile.TemporaryDirectory() as tmp:
            image_path = _build(plan, tmp)
            recovered = walk_image(image_path, atari_hd.FORMAT_HDDRIVER)

        self.assertEqual(len(recovered), len(plan.partitions))
        for i, (got, expected) in enumerate(zip(recovered, plan.partitions)):
            with self.subTest(slot=i, partition=expected.name):
                self.assertEqual(got["start_lba"], expected.start_lba,
                                 f"slot {i} start_lba")
                self.assertEqual(got["size_sectors"], expected.size_sectors,
                                 f"slot {i} size_sectors")
                self.assertEqual(got["part_type"], MBR_TYPE_FAT16,
                                 f"slot {i} part_type")


if __name__ == "__main__":
    unittest.main()
