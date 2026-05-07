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
    BACKSPACE = "BACKSPACE"
    RESIZE = "RESIZE"
    UP = "UP"
    DOWN = "DOWN"
    LEFT = "LEFT"
    RIGHT = "RIGHT"
    UNKNOWN = "UNKNOWN"


# Mapping from VT-style arrow sequences (the byte after `\x1b[`) to
# Key sentinels. Used by both POSIX and the VT path on Windows.
_VT_ARROWS = {
    b"A": Key.UP,
    b"B": Key.DOWN,
    b"C": Key.RIGHT,
    b"D": Key.LEFT,
}


# Set by the SIGWINCH handler installed in terminal.py (POSIX only).
# read_key checks and clears it after an interrupted os.read; if set,
# returns Key.RESIZE so the event loop can refresh the layout.
resize_pending = False


if os.name == "posix":
    import select

    def _has_pending(timeout: float = 0.0) -> bool:
        r, _, _ = select.select([sys.stdin], [], [], timeout)
        return bool(r)

    def _drain():
        while _has_pending(0.0):
            os.read(sys.stdin.fileno(), 1)

    def read_key():
        global resize_pending
        while True:
            try:
                b = os.read(sys.stdin.fileno(), 1)
                break
            except InterruptedError:
                # SIGWINCH fired during the read. If our handler set
                # the resize flag, surface it to the event loop;
                # otherwise retry the read.
                if resize_pending:
                    resize_pending = False
                    return Key.RESIZE
                continue
        if not b:
            return Key.UNKNOWN
        if b == b"\x1b":
            # Could be a real ESC keypress or the start of an escape
            # sequence (arrow keys, F-keys, etc.). Peek with a short
            # timeout to disambiguate.
            if _has_pending(0.05):
                # Read the rest of the sequence. We expect short
                # CSI-style sequences (`[A` etc.) but bound the read
                # to be safe against runaway terminals.
                seq = b""
                for _ in range(16):
                    if not _has_pending(0.05):
                        break
                    seq += os.read(sys.stdin.fileno(), 1)
                # Match arrow keys: `[A` / `[B` / `[C` / `[D`.
                if len(seq) == 2 and seq[:1] == b"[" and seq[1:2] in _VT_ARROWS:
                    return _VT_ARROWS[seq[1:2]]
                return Key.UNKNOWN
            return Key.ESC
        if b == b"\x03":
            return Key.CTRL_C
        if b in (b"\r", b"\n"):
            return Key.ENTER
        if b in (b"\x7f", b"\x08"):
            return Key.BACKSPACE
        try:
            return b.decode("utf-8", errors="replace")
        except Exception:
            return Key.UNKNOWN

elif os.name == "nt":
    import msvcrt

    # Pre-VT special-key codes (Windows console, second byte after the
    # 0x00 / 0xE0 prefix).
    _WIN_PREVT_ARROWS = {
        b"H": Key.UP,
        b"P": Key.DOWN,
        b"M": Key.RIGHT,
        b"K": Key.LEFT,
    }

    def read_key():
        b = msvcrt.getch()
        # Pre-VT-style special keys arrive as a 2-byte prefix
        # (\x00 or \xe0) followed by a key code.
        if b in (b"\x00", b"\xe0"):
            b2 = msvcrt.getch()
            return _WIN_PREVT_ARROWS.get(b2, Key.UNKNOWN)
        if b == b"\x1b":
            # In VT-input mode arrows arrive as ESC sequences.
            if msvcrt.kbhit():
                seq = b""
                while msvcrt.kbhit():
                    seq += msvcrt.getch()
                if len(seq) == 2 and seq[:1] == b"[" and seq[1:2] in _VT_ARROWS:
                    return _VT_ARROWS[seq[1:2]]
                return Key.UNKNOWN
            return Key.ESC
        if b == b"\x03":
            return Key.CTRL_C
        if b in (b"\r", b"\n"):
            return Key.ENTER
        if b in (b"\x7f", b"\x08"):
            return Key.BACKSPACE
        try:
            return b.decode("utf-8", errors="replace")
        except Exception:
            return Key.UNKNOWN

else:
    raise RuntimeError(f"unsupported platform: os.name={os.name!r}")
