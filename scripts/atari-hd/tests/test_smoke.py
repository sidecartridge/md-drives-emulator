"""Smoke test: confirms unittest discovery finds this module and that
the sys.path bootstrap in _support.py exposes scripts/atari-hd/atari_hd.py
as the `atari_hd` module."""

import unittest

from _support import atari_hd


class TestSmoke(unittest.TestCase):
    def test_atari_hd_exposes_format_fat16(self):
        # format_fat16 is the keystone landed by epic-001; if the suite
        # can see it, every story 002+ test will be able to as well.
        self.assertTrue(hasattr(atari_hd, "format_fat16"),
                        "atari_hd.format_fat16 missing")


if __name__ == "__main__":
    unittest.main()
