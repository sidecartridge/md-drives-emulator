"""Shared bootstrap and helper stubs for the atari_hd test suite.

Tests import the script under test via:

    from _support import atari_hd

The sys.path manipulation below makes that import resolve to
scripts/atari-hd/atari_hd.py regardless of the caller's cwd.
"""

import sys
from pathlib import Path

# Make scripts/atari-hd/ importable so `import atari_hd` works from any
# cwd. unittest discover roots at scripts/atari-hd/tests/, which puts
# this directory on sys.path; we add the parent so the script under
# test resolves too.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import atari_hd  # noqa: E402


def parse_bpb(buf, offset):
    """Parse a 512-byte FAT16 boot sector / BPB starting at `offset` in
    `buf`. Returns a dict keyed by field name. Real implementation lands
    in epic-002 / story 006."""
    raise NotImplementedError("parse_bpb is not implemented yet")


def parse_ahdi_root(buf):
    """Parse an AHDI root sector and return the list of partition slots
    with start LBA, size, and type. Real implementation lands in
    epic-002 / story 003."""
    raise NotImplementedError("parse_ahdi_root is not implemented yet")
