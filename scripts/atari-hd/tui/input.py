"""Single-key input.

read_key() blocks until a key is available and returns either a
sentinel from `Key` (for special keys like ESC, ENTER, CTRL_C) or a
single-character string for printable input.

Story 001 only needs ESC / CTRL_C / printable to drive the placeholder
screen's quit handling. Arrow keys etc. arrive as ESC-sequences (Unix
+ Windows VT mode) or as 2-byte prefixed sequences (Windows pre-VT);
this module *consumes* those sequences and returns Key.UNKNOWN so
they do not trigger spurious exits, but does not decode them. Real
arrow-key decoding is a story-002 concern.
"""

import os
import sys
from enum import Enum


class Key(Enum):
    """Special-key sentinels."""
    ESC = "ESC"
    CTRL_C = "CTRL_C"
    ENTER = "ENTER"
    UNKNOWN = "UNKNOWN"


if os.name == "posix":
    import select

    def _has_pending(timeout: float = 0.0) -> bool:
        r, _, _ = select.select([sys.stdin], [], [], timeout)
        return bool(r)

    def _drain():
        while _has_pending(0.0):
            os.read(sys.stdin.fileno(), 1)

    def read_key():
        b = os.read(sys.stdin.fileno(), 1)
        if not b:
            return Key.UNKNOWN
        if b == b"\x1b":
            # Could be a real ESC keypress or the start of an escape
            # sequence (arrow keys, F-keys, etc.). Peek with a short
            # timeout to disambiguate; if more bytes follow within
            # 50 ms it's a sequence, drain and return UNKNOWN.
            if _has_pending(0.05):
                _drain()
                return Key.UNKNOWN
            return Key.ESC
        if b == b"\x03":
            return Key.CTRL_C
        if b in (b"\r", b"\n"):
            return Key.ENTER
        try:
            return b.decode("utf-8", errors="replace")
        except Exception:
            return Key.UNKNOWN

elif os.name == "nt":
    import msvcrt

    def read_key():
        b = msvcrt.getch()
        # Pre-VT-style special keys arrive as a 2-byte prefix
        # (\x00 or \xe0) followed by a key code. Drain the second
        # byte; story 002 will decode them.
        if b in (b"\x00", b"\xe0"):
            msvcrt.getch()
            return Key.UNKNOWN
        if b == b"\x1b":
            # In VT-input mode arrows arrive as ESC sequences. Peek
            # for more bytes; if any are queued, drain and return
            # UNKNOWN. If nothing follows it's a real ESC.
            if msvcrt.kbhit():
                while msvcrt.kbhit():
                    msvcrt.getch()
                return Key.UNKNOWN
            return Key.ESC
        if b == b"\x03":
            return Key.CTRL_C
        if b in (b"\r", b"\n"):
            return Key.ENTER
        try:
            return b.decode("utf-8", errors="replace")
        except Exception:
            return Key.UNKNOWN

else:
    raise RuntimeError(f"unsupported platform: os.name={os.name!r}")
