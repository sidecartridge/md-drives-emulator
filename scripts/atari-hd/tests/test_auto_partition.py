"""Unit tests for atari_hd.auto_partition_layout (epic-005 / story 001)."""

import unittest

from _support import atari_hd

AUTO_DEFAULT = atari_hd.AUTO_MODE_DEFAULT
AUTO_MAX = atari_hd.AUTO_MODE_MAX


class TestAutoPartitionDefault(unittest.TestCase):
    """mode='default': boot at format min, data slots at per-slot max."""

    def test_ahdi_bootable_256mb(self):
        # 256 MB image, AHDI bootable: 15 MB GEM boot + ceil(241/511)=1
        # 241 MB BGM data slot.
        parts = atari_hd.auto_partition_layout(
            "AHDI", 256, AUTO_DEFAULT, bootable=True)
        self.assertEqual(len(parts), 2)
        self.assertEqual(parts[0].name, "BOOT")
        self.assertEqual(parts[0].size_mb, 15)
        self.assertFalse(parts[0].is_extended)
        self.assertEqual(parts[1].name, "DATA1")
        self.assertEqual(parts[1].size_mb, 241)
        self.assertFalse(parts[1].is_extended)

    def test_ahdi_bootable_4096mb(self):
        # 4096 MB image: 15 MB boot + 4081 MB / 511 = 8 data slots
        # at 511 MB each, last one truncated to 481 MB.
        parts = atari_hd.auto_partition_layout(
            "AHDI", 4096, AUTO_DEFAULT, bootable=True)
        self.assertEqual(parts[0].size_mb, 15)
        # 4081 / 511 = 7.99 -> 8 ceil; first 7 at 511, last at 4081-7*511 = 504.
        self.assertEqual(len(parts) - 1, 8)
        self.assertEqual(sum(p.size_mb for p in parts[1:]), 4081)
        for d in parts[1:-1]:
            self.assertEqual(d.size_mb, 511)
        self.assertEqual(parts[-1].size_mb, 4081 - 7 * 511)

    def test_ahdi_strict_uses_256mb_cap(self):
        # AHDI strict-TOS data cap drops to 256 MB.
        parts = atari_hd.auto_partition_layout(
            "AHDI", 1024, AUTO_DEFAULT, strict_tos=True, bootable=True)
        # 15 boot + 1009 data; ceil(1009/256) = 4 slots at 256 MB
        # except last is 1009 - 3*256 = 241 MB.
        self.assertEqual(parts[0].size_mb, 15)
        self.assertEqual(len(parts) - 1, 4)
        self.assertEqual(parts[1].size_mb, 256)
        self.assertEqual(parts[-1].size_mb, 1009 - 3 * 256)

    def test_ahdi_non_bootable_uses_min_boot(self):
        parts = atari_hd.auto_partition_layout(
            "AHDI", 64, AUTO_DEFAULT, bootable=False)
        # 2 MB boot + 62 MB data (single slot at 62 MB, under cap).
        self.assertEqual(parts[0].size_mb, 2)
        self.assertEqual(len(parts), 2)
        self.assertEqual(parts[1].size_mb, 62)

    def test_ppdriver_default(self):
        # 1024 MB PPDRIVER default: 32 MB boot + ceil(992/511)=2 slots
        # at 511 MB / 481 MB. Slots 1+ extended.
        parts = atari_hd.auto_partition_layout(
            "PPDRIVER", 1024, AUTO_DEFAULT)
        self.assertEqual(parts[0].size_mb, 32)
        self.assertFalse(parts[0].is_extended)
        self.assertEqual(len(parts) - 1, 2)
        self.assertEqual(parts[1].size_mb, 511)
        self.assertTrue(parts[1].is_extended)
        self.assertEqual(parts[2].size_mb, 992 - 511)
        self.assertTrue(parts[2].is_extended)

    def test_hddriver_default(self):
        # HDDRIVER same shape as PPDRIVER; data slots all extended.
        parts = atari_hd.auto_partition_layout(
            "HDDRIVER", 512, AUTO_DEFAULT)
        self.assertEqual(parts[0].size_mb, 32)
        self.assertTrue(all(p.is_extended for p in parts[1:]))

    def test_image_too_small_raises(self):
        with self.assertRaises(ValueError):
            atari_hd.auto_partition_layout(
                "AHDI", 14, AUTO_DEFAULT, bootable=True)

    def test_only_boot_when_image_equals_boot(self):
        # 15 MB image, AHDI bootable -> exactly the boot, no data.
        parts = atari_hd.auto_partition_layout(
            "AHDI", 15, AUTO_DEFAULT, bootable=True)
        self.assertEqual(len(parts), 1)
        self.assertEqual(parts[0].size_mb, 15)


class TestAutoPartitionMax(unittest.TestCase):
    """mode='max': boot at format min, remaining space divided equally."""

    def test_ahdi_bootable_max(self):
        # 4096 MB AHDI bootable max: 15 MB boot + 13 equal slices of
        # 4081/13 = 313 MB (rounded down).
        parts = atari_hd.auto_partition_layout(
            "AHDI", 4096, AUTO_MAX, bootable=True)
        self.assertEqual(len(parts), 14)  # MAX_PARTITIONS=14
        self.assertEqual(parts[0].size_mb, 15)
        for d in parts[1:]:
            self.assertEqual(d.size_mb, 4081 // 13)

    def test_ppdriver_small_image_drops_slot_count(self):
        # 64 MB PPDRIVER max: 32 MB boot + 32 MB remainder. With
        # n=13, slice = 32//13 = 2 < HYBRID_MIN (32). Drop n until
        # slice >= 32: n=1 -> slice=32. Result: 1 boot + 1 data.
        parts = atari_hd.auto_partition_layout(
            "PPDRIVER", 64, AUTO_MAX)
        self.assertEqual(len(parts), 2)
        self.assertEqual(parts[0].size_mb, 32)
        self.assertEqual(parts[1].size_mb, 32)
        self.assertTrue(parts[1].is_extended)

    def test_max_caps_per_slice_at_data_cap(self):
        # 16384 MB AHDI bootable max: 16369 MB / 13 = 1259 MB per slice,
        # but data cap is 511 -> capped at 511. Image won't fully use.
        parts = atari_hd.auto_partition_layout(
            "AHDI", 16384, AUTO_MAX, bootable=True)
        self.assertEqual(len(parts), 14)
        self.assertTrue(all(p.size_mb == 511 for p in parts[1:]))

    def test_max_with_n_limit(self):
        # 1024 MB AHDI bootable max, n_limit=4: 1 boot + 3 equal data
        # slices of 1009/3 = 336 MB.
        parts = atari_hd.auto_partition_layout(
            "AHDI", 1024, AUTO_MAX, n_limit=4, bootable=True)
        self.assertEqual(len(parts), 4)
        self.assertEqual(parts[0].size_mb, 15)
        self.assertTrue(all(p.size_mb == 1009 // 3 for p in parts[1:]))

    def test_max_with_n_limit_1_returns_boot_only(self):
        parts = atari_hd.auto_partition_layout(
            "AHDI", 1024, AUTO_MAX, n_limit=1, bootable=True)
        self.assertEqual(len(parts), 1)
        self.assertEqual(parts[0].size_mb, 15)

    def test_default_with_n_limit_clamps(self):
        # 4096 MB AHDI default would naturally produce 9 slots; n_limit=3
        # caps to 1 boot + 2 data at 511 MB each.
        parts = atari_hd.auto_partition_layout(
            "AHDI", 4096, AUTO_DEFAULT, n_limit=3, bootable=True)
        self.assertEqual(len(parts), 3)
        self.assertEqual(parts[0].size_mb, 15)
        self.assertEqual(parts[1].size_mb, 511)
        self.assertEqual(parts[2].size_mb, 511)


class TestAutoPartitionEdgeCases(unittest.TestCase):
    def test_unknown_format_raises(self):
        with self.assertRaises(ValueError):
            atari_hd.auto_partition_layout(
                "BOGUS", 64, AUTO_DEFAULT)

    def test_unknown_mode_raises(self):
        with self.assertRaises(ValueError):
            atari_hd.auto_partition_layout(
                "AHDI", 64, "weird")

    def test_n_limit_zero_raises(self):
        with self.assertRaises(ValueError):
            atari_hd.auto_partition_layout(
                "AHDI", 64, AUTO_DEFAULT, n_limit=0)

    def test_max_n_capped_to_max_partitions(self):
        # n_limit higher than MAX_PARTITIONS -> implicitly capped.
        parts = atari_hd.auto_partition_layout(
            "AHDI", 4096, AUTO_MAX, n_limit=99, bootable=True)
        self.assertEqual(len(parts), atari_hd.MAX_PARTITIONS["AHDI"])

    def test_default_total_size_does_not_exceed_image(self):
        # Sum of all partitions never exceeds the image size for
        # default mode (last slot truncates).
        parts = atari_hd.auto_partition_layout(
            "AHDI", 1234, AUTO_DEFAULT, bootable=True)
        total = sum(p.size_mb for p in parts)
        self.assertLessEqual(total, 1234)

    def test_plan_image_accepts_default_output(self):
        # Sanity: the layout's output is a valid plan_image input.
        parts = atari_hd.auto_partition_layout(
            "AHDI", 256, AUTO_DEFAULT, bootable=True)
        plan = atari_hd.plan_image(
            format_id="AHDI", image_path="/tmp/auto_test.img",
            image_mb=256, partitions=parts,
            ahdi_driver_path="/Users/diego/mister_wkspc/md-drives-emulator"
                              "/scripts/atari-hd/drivers/icdp655a/ICDBOOT.PRG")
        self.assertEqual(len(plan.partitions), 2)
        self.assertEqual(plan.partitions[0].size_mb, 15)


if __name__ == "__main__":
    unittest.main()
