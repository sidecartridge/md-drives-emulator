"""Reproducibility canary for atari_hd.format_fat16().

The writer commits to a deterministic volume_id seeded from inputs
(label, size, sector size, sectors-per-cluster, reserved-sectors), so
the same arguments produce byte-equal output across hosts and runs.
The contract is quiet -- it would be easy to break and easy to miss.

This module is the alarm that goes off if the seed function ever
starts reading the wall clock, ingesting host randomness, or losing
sensitivity to one of its inputs.

Cross-OS stability follows by construction: if the formula is
pure-Python and reads no clock, the output is bit-identical across
OSes. We do not need to upload fixture images from one OS to another
to verify it.
"""

import tempfile
import unittest
from pathlib import Path

from _support import atari_hd


# Baseline inputs to format_fat16(). Sized at 16 MB so it's small enough
# to format twice per case without slowing the suite, and big enough to
# satisfy the FAT16 minimum cluster count under every variation below.
BASELINE = dict(
    partition_sectors_512=32768,
    sector_size=512,
    sectors_per_cluster=2,
    reserved_sectors=2,
    label="ATARI",
    disable_fat_align=True,
)


# Variations tested for sensitivity. Each entry is (case_id, overrides
# dict). At least one variation per input dimension that participates
# in _fat16_seed_volume_id (label, partition_sectors_512, sector_size,
# sectors_per_cluster, reserved_sectors).
SENSITIVITY_CASES = [
    ("label",         dict(label="DIFFERENT")),
    ("size",          dict(partition_sectors_512=40960)),
    ("sector_size",   dict(sector_size=1024)),
    ("spc",           dict(sectors_per_cluster=4)),
    ("res",           dict(reserved_sectors=3)),
]


# Volume serial lives at offset 0x27 in the boot sector; 4 bytes.
VOL_ID_SLICE = slice(0x27, 0x2B)


def _format(tmp, name, **overrides):
    kwargs = {**BASELINE, **overrides}
    path = Path(tmp) / f"{name}.img"
    atari_hd.format_fat16(out_path=str(path), **kwargs)
    return path.read_bytes()


@unittest.skipUnless(hasattr(atari_hd, "format_fat16"),
                     "atari_hd.format_fat16 not present "
                     "(epic-001 / story 001 must land first)")
class TestReproducibility(unittest.TestCase):
    def test_determinism_byte_equal(self):
        # Same inputs, two runs, byte-equal output.
        with tempfile.TemporaryDirectory() as tmp:
            a = _format(tmp, "a")
            b = _format(tmp, "b")
        self.assertEqual(
            a, b,
            "format_fat16 is not deterministic: identical inputs "
            "produced different bytes")

    def test_sensitivity_volume_id_changes_per_dimension(self):
        # Vary one input at a time; the four volume-serial bytes at
        # offset 0x27 must differ from the baseline. If they don't,
        # the seed function has lost sensitivity to that dimension.
        with tempfile.TemporaryDirectory() as tmp:
            base = _format(tmp, "baseline")
            base_vol_id = base[VOL_ID_SLICE]
            for case_id, overrides in SENSITIVITY_CASES:
                with self.subTest(varied=case_id):
                    varied = _format(tmp, case_id, **overrides)
                    varied_vol_id = varied[VOL_ID_SLICE]
                    self.assertNotEqual(
                        varied_vol_id, base_vol_id,
                        f"varying '{case_id}' did not change volume_id "
                        f"(base={base_vol_id.hex()}, "
                        f"varied={varied_vol_id.hex()}); the seed "
                        "function may have lost sensitivity to that "
                        "input")

    def test_disable_fat_align_is_currently_a_noop(self):
        # Story 001 of epic-001 documents disable_fat_align=False as
        # reserved for future single-BPB variants; today both branches
        # behave identically. Lock that contract here so that the day
        # someone wires up the False branch, this test fails loudly and
        # forces an explicit decision instead of producing silently
        # different bytes.
        with tempfile.TemporaryDirectory() as tmp:
            true_bytes = _format(tmp, "fat_align_true",
                                 disable_fat_align=True)
            false_bytes = _format(tmp, "fat_align_false",
                                  disable_fat_align=False)
        self.assertEqual(
            true_bytes, false_bytes,
            "disable_fat_align is documented as a no-op (epic-001 / "
            "story 001); flipping the flag must not change output. If "
            "this test fails, decide whether the new behavior is "
            "intended and update the docstring + this test together.")


if __name__ == "__main__":
    unittest.main()
