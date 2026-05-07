"""Cap enforcement tests for atari_hd.py.

Locks the partition-size and partition-count rules that guard the
boot-from-TOS / drive-letter constraints. Each rule landed via a
separate user correction during the original build-out; these tests
prevent re-discovering them the hard way.

Covered:
  - partition_cap_mb: AHDI per-slot caps, hybrid no-per-slot caps.
  - ahdi_partition_id: first slot always emitted as GEM (defensive
    legacy-driver-compatibility clamp; not a hard TOS rule -- see
    atari_hd.ahdi_partition_id() docstring); threshold pick for
    slots 1+.
  - partition_layout: primary/extended split per format for representative N.
  - Rejection paths: out-of-range N, AHDI strict caps, AHDI permissive caps,
    HDDRIVER primary cap (always 1), first-AHDI-always-GEM invariant.
"""

import unittest

from _support import atari_hd


FORMATS = (atari_hd.FORMAT_AHDI,
           atari_hd.FORMAT_PPDRIVER,
           atari_hd.FORMAT_HDDRIVER)


class TestPartitionCapMb(unittest.TestCase):
    """partition_cap_mb returns the right per-slot ceiling for each
    (format, strict, slot) combination."""

    def test_ahdi_strict_gem_cap(self):
        # Slot 0 is the boot/GEM partition. Strict TOS<1.04 caps GEM at 16 MB.
        self.assertEqual(
            atari_hd.partition_cap_mb(atari_hd.FORMAT_AHDI, True, 0), 16)

    def test_ahdi_permissive_gem_cap(self):
        # 31 MB, not 32: ident=GEM strictly implies bps=512, and
        # choose_logical_sector_size flips to bps=1024 at 32 MB.
        # See AHDI_GEM_MAX_MB rationale in atari_hd.py.
        self.assertEqual(
            atari_hd.partition_cap_mb(atari_hd.FORMAT_AHDI, False, 0), 31)

    def test_ahdi_strict_bgm_cap(self):
        # Slot 1+ is BGM. Strict caps BGM at 256 MB.
        self.assertEqual(
            atari_hd.partition_cap_mb(atari_hd.FORMAT_AHDI, True, 1), 256)

    def test_ahdi_permissive_bgm_cap(self):
        # 511 MB, not 512: the documented "512 MB BGM" is rounded up.
        # Real ceiling is NSECTS=65535 at bps=8192 = 511.99 MB. A 512 MB
        # plan trips the Hatari sector-doubling rule into bps=16384,
        # which TOS 1.04 - 3.x doesn't support.
        self.assertEqual(
            atari_hd.partition_cap_mb(atari_hd.FORMAT_AHDI, False, 1), 511)

    def test_ahdi_strict_bgm_cap_holds_for_higher_slots(self):
        # Slots 2..N share the BGM cap with slot 1.
        for slot in (1, 2, 3, 5, 13):
            with self.subTest(slot=slot):
                self.assertEqual(
                    atari_hd.partition_cap_mb(
                        atari_hd.FORMAT_AHDI, True, slot), 256)

    def test_hybrid_caps_have_no_per_slot_distinction(self):
        # PPDRIVER / HDDRIVER go through the DOS view, which doesn't care
        # about TOS BGM limits. The cap is the hybrid layout ceiling
        # (511 MB; TOS NSECTS 16-bit max at bps=8192) regardless of
        # slot or strict flag. The strict flag is meaningless for
        # hybrids -- they target TOS 1.04+ exclusively (synthesize
        # rejects ratio < 2 / tos_bps < 1024).
        for fmt in (atari_hd.FORMAT_PPDRIVER, atari_hd.FORMAT_HDDRIVER):
            for slot in (0, 1, 5, 13):
                for strict in (True, False):
                    with self.subTest(format=fmt, slot=slot, strict=strict):
                        self.assertEqual(
                            atari_hd.partition_cap_mb(fmt, strict, slot),
                            atari_hd.HYBRID_MAX_PARTITION_MB)


class TestCapMbForType(unittest.TestCase):
    """cap_mb_for_type returns the cap for a partition based on the
    user-chosen type ident. Used by interactive callers (TUI edit
    dialog) where slot 1+ might be GEM-typed by the user."""

    def test_ahdi_gem_cap_regardless_of_slot(self):
        # User picks GEM on any slot -> GEM cap (16 strict, 31 perm).
        # Perm cap is 31, not 32: ident=GEM strictly implies bps=512.
        for ident in (b"GEM", "GEM"):
            with self.subTest(ident=ident):
                self.assertEqual(
                    atari_hd.cap_mb_for_type(atari_hd.FORMAT_AHDI, True, ident),
                    16)
                self.assertEqual(
                    atari_hd.cap_mb_for_type(atari_hd.FORMAT_AHDI, False, ident),
                    31)

    def test_ahdi_bgm_cap_regardless_of_slot(self):
        # User picks BGM -> BGM cap (256 strict, 511 perm).
        for ident in (b"BGM", "BGM"):
            with self.subTest(ident=ident):
                self.assertEqual(
                    atari_hd.cap_mb_for_type(atari_hd.FORMAT_AHDI, True, ident),
                    256)
                self.assertEqual(
                    atari_hd.cap_mb_for_type(atari_hd.FORMAT_AHDI, False, ident),
                    511)

    def test_unknown_ahdi_ident_falls_back_to_bgm_cap(self):
        # Defensive: anything other than GEM uses the BGM cap.
        self.assertEqual(
            atari_hd.cap_mb_for_type(atari_hd.FORMAT_AHDI, False, b"XGM"),
            511)
        self.assertEqual(
            atari_hd.cap_mb_for_type(atari_hd.FORMAT_AHDI, False, None),
            511)

    def test_hybrid_ignores_ident(self):
        # PPDRIVER / HDDRIVER use the hybrid-layout ceiling regardless
        # of ident or strict_tos. The cap (511 MB) comes from the TOS
        # NSECTS 16-bit limit at the maximum supported TOS bps of
        # 8192 -- not the FAT16 cluster ceiling.
        for fmt in (atari_hd.FORMAT_PPDRIVER, atari_hd.FORMAT_HDDRIVER):
            for ident in (b"GEM", b"BGM", None, "FAT16"):
                with self.subTest(format=fmt, ident=ident):
                    self.assertEqual(
                        atari_hd.cap_mb_for_type(fmt, False, ident),
                        atari_hd.HYBRID_MAX_PARTITION_MB)
                    self.assertEqual(
                        atari_hd.cap_mb_for_type(fmt, True, ident),
                        atari_hd.HYBRID_MAX_PARTITION_MB)


class TestAhdiPartitionId(unittest.TestCase):
    """ahdi_partition_id picks the ident bytes (b'GEM' / b'BGM')
    strictly from the bps that choose_logical_sector_size picks for
    the partition's 512-byte sector count.

    Per AHDI 3.0 / the Atari Compendium: GEM means bps=512, BGM means
    bps>512. The Hatari doubling rule keeps bps=512 only while
    clusters_at_spc=2 stays <= 32765, i.e. partition_sec_512 <= 65530
    (~31.99 MB). At 32 MB exactly the rule doubles to bps=1024.

    The earlier version of this function accepted an `is_first` flag
    that forced GEM at slot 0 regardless of size; that force was
    removed because it could produce GEM+bps=1024, which the
    Compendium flagged as malformed (legacy drivers may corrupt past
    the first 32 MB of physical sectors). Slot 0 is now kept in the
    GEM region by partition_cap_mb() instead, so the ident landing
    here is always consistent with the BPB."""

    def test_gem_region(self):
        # Sizes that choose_logical_sector_size keeps at bps=512 -> GEM.
        for size in (1, 2, 8, 16, 17, 24, 30, 31):
            with self.subTest(size=size):
                self.assertEqual(
                    atari_hd.ahdi_partition_id(size), b"GEM",
                    f"{size} MB sits at bps=512; ident must be GEM")

    def test_bgm_region(self):
        # 32 MB is the first size that trips bps=1024 -> BGM.
        for size in (32, 33, 64, 128, 256, 511):
            with self.subTest(size=size):
                self.assertEqual(
                    atari_hd.ahdi_partition_id(size), b"BGM",
                    f"{size} MB sits at bps>512; ident must be BGM")


class TestPartitionLayout(unittest.TestCase):
    """partition_layout splits N partitions into primary slots vs. an
    extended chain, per each format's convention."""

    # (primary_count, has_extended, logical_count) per format per N.
    EXPECTED = {
        atari_hd.FORMAT_AHDI: {
            1:  (1, False, 0),
            2:  (2, False, 0),
            3:  (3, False, 0),
            4:  (4, False, 0),
            5:  (3, True, 2),     # slot 3 becomes XGM link, 2 logicals
            10: (3, True, 7),
            14: (3, True, 11),
        },
        atari_hd.FORMAT_PPDRIVER: {
            # PPTOSDOS convention: one primary + extended chain.
            # Multi-primary PPDRIVER fails on real Atari hardware at
            # >256 MB partition sizes (empirically verified), and the
            # single-primary layout is what real PPDRIVER tools emit.
            1:  (1, False, 0),
            2:  (1, True, 1),
            3:  (1, True, 2),
            4:  (1, True, 3),
            5:  (1, True, 4),
            10: (1, True, 9),
            14: (1, True, 13),
        },
        atari_hd.FORMAT_HDDRIVER: {
            1:  (1, False, 0),     # primary cap = 1
            2:  (1, True, 1),
            3:  (1, True, 2),
            4:  (1, True, 3),
            5:  (1, True, 4),
            10: (1, True, 9),
            14: (1, True, 13),
        },
    }

    def test_layout_table(self):
        for fmt, table in self.EXPECTED.items():
            for n, expected in table.items():
                with self.subTest(format=fmt, n=n):
                    layout = atari_hd.partition_layout(fmt, n)
                    actual = (layout["primary_count"],
                              layout["has_extended"],
                              layout["logical_count"])
                    self.assertEqual(
                        actual, expected,
                        f"\n  fmt    : {fmt}\n"
                        f"  n      : {n}\n"
                        f"  expect : (primary={expected[0]}, "
                        f"ext={expected[1]}, logical={expected[2]})\n"
                        f"  actual : (primary={actual[0]}, "
                        f"ext={actual[1]}, logical={actual[2]})")

    def test_hddriver_primary_count_never_exceeds_one(self):
        # The "HDDRIVER > 1 primary" rule is enforced by *capping* primary
        # at 1, not by raising; the test confirms that cap is honored.
        for n in range(1, atari_hd.MAX_PARTITIONS[atari_hd.FORMAT_HDDRIVER] + 1):
            with self.subTest(n=n):
                layout = atari_hd.partition_layout(
                    atari_hd.FORMAT_HDDRIVER, n)
                self.assertEqual(
                    layout["primary_count"], 1,
                    f"HDDRIVER must cap primary at 1; got "
                    f"{layout['primary_count']} for N={n}")


class TestRejectionPaths(unittest.TestCase):
    """Out-of-range inputs and cap-violating plans must raise with a
    message that names the rule, so the failure points the user at the
    fix instead of producing a corrupt image silently."""

    def test_zero_partitions_rejected(self):
        for fmt in FORMATS:
            with self.subTest(format=fmt):
                with self.assertRaises(ValueError) as ctx:
                    atari_hd.partition_layout(fmt, 0)
                self.assertIn("at least one partition",
                              str(ctx.exception).lower())

    def test_above_max_partitions_rejected(self):
        for fmt in FORMATS:
            with self.subTest(format=fmt):
                with self.assertRaises(ValueError) as ctx:
                    atari_hd.partition_layout(fmt, 15)
                msg = str(ctx.exception).lower()
                self.assertIn("supports at most", msg,
                              f"{fmt}: {ctx.exception}")
                self.assertIn("14", msg, f"{fmt}: {ctx.exception}")

    def test_ahdi_strict_first_partition_over_gem_cap(self):
        # 17 MB > 16 MB strict GEM cap => plan_image must reject and the
        # message must name the rule.
        partitions = [atari_hd.Partition(name="BOOT", size_mb=17)]
        with self.assertRaises(ValueError) as ctx:
            atari_hd.plan_image(atari_hd.FORMAT_AHDI, "<test>", image_mb=64,
                                partitions=partitions, strict_tos=True)
        msg = str(ctx.exception)
        self.assertIn("TOS < 1.04", msg, msg)
        self.assertIn("boot (GEM)", msg, msg)

    def test_ahdi_strict_bgm_partition_over_cap(self):
        # Slot 0 within cap, slot 1 over the strict 256 MB BGM cap.
        partitions = [
            atari_hd.Partition(name="BOOT", size_mb=16),
            atari_hd.Partition(name="BIG",  size_mb=300),
        ]
        with self.assertRaises(ValueError) as ctx:
            atari_hd.plan_image(atari_hd.FORMAT_AHDI, "<test>", image_mb=512,
                                partitions=partitions, strict_tos=True)
        msg = str(ctx.exception)
        self.assertIn("TOS < 1.04", msg, msg)
        self.assertIn("BGM", msg, msg)

    def test_ahdi_permissive_first_partition_over_gem_cap(self):
        # 32 MB > 31 MB permissive GEM cap => reject under TOS 1.04+ mode.
        partitions = [atari_hd.Partition(name="BOOT", size_mb=32)]
        with self.assertRaises(ValueError) as ctx:
            atari_hd.plan_image(atari_hd.FORMAT_AHDI, "<test>", image_mb=64,
                                partitions=partitions, strict_tos=False)
        msg = str(ctx.exception)
        self.assertIn("TOS 1.04+", msg, msg)
        self.assertIn("boot (GEM)", msg, msg)

    def test_ahdi_permissive_bgm_partition_over_cap(self):
        # 600 MB > 511 MB permissive BGM cap. BOOT is 31 MB (the
        # permissive GEM cap) so plan_image gets past slot 0 and
        # reaches the BGM-cap check on slot 1.
        partitions = [
            atari_hd.Partition(name="BOOT", size_mb=31),
            atari_hd.Partition(name="HUGE", size_mb=600),
        ]
        with self.assertRaises(ValueError) as ctx:
            atari_hd.plan_image(atari_hd.FORMAT_AHDI, "<test>", image_mb=1024,
                                partitions=partitions, strict_tos=False)
        msg = str(ctx.exception)
        self.assertIn("TOS 1.04+", msg, msg)
        self.assertIn("BGM", msg, msg)


if __name__ == "__main__":
    unittest.main()
