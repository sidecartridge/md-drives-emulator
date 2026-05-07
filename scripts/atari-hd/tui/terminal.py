"""Cross-platform terminal session manager.

Enters raw mode, switches to the alternate screen buffer, and hides
the cursor on `__enter__`; restores everything on `__exit__` --
including on KeyboardInterrupt and unhandled exceptions, which is
the only thing standing between a crash and a broken shell.

Unix path uses termios + tty.
Windows path uses kernel32.SetConsoleMode via ctypes.
"""

import os
import sys
from contextlib import contextmanager


# ANSI escape sequences for terminal-state setup / teardown.
ENTER_ALT_SCREEN = "\x1b[?1049h"
EXIT_ALT_SCREEN = "\x1b[?1049l"
HIDE_CURSOR = "\x1b[?25l"
SHOW_CURSOR = "\x1b[?25h"


def _emit(seq: str) -> None:
    sys.stdout.write(seq)
    sys.stdout.flush()


if os.name == "posix":
    import signal
    import termios
    import tty

    from . import input as _tui_input

    def _on_sigwinch(_signum, _frame):
        # Set a module-level flag that read_key() in input.py drains
        # after an interrupted os.read. The event loop then redraws
        # with the new terminal size on the next iteration.
        _tui_input.resize_pending = True

    @contextmanager
    def terminal_session():
        """Raw mode + alt screen + hidden cursor for the duration of
        the block. Restores everything on any exit path. Installs a
        SIGWINCH handler so terminal resizes wake up read_key() with
        Key.RESIZE; uninstalls on exit."""
        fd = sys.stdin.fileno()
        old_attrs = termios.tcgetattr(fd)
        old_winch = signal.signal(signal.SIGWINCH, _on_sigwinch)
        # siginterrupt(True) makes os.read return EINTR on signal
        # delivery instead of being auto-restarted.
        signal.siginterrupt(signal.SIGWINCH, True)
        try:
            tty.setraw(fd)
            _emit(ENTER_ALT_SCREEN + HIDE_CURSOR)
            yield
        finally:
            _emit(SHOW_CURSOR + EXIT_ALT_SCREEN)
            termios.tcsetattr(fd, termios.TCSADRAIN, old_attrs)
            signal.signal(signal.SIGWINCH, old_winch)

elif os.name == "nt":
    import ctypes
    from ctypes import wintypes

    # Console mode bits we care about.
    _ENABLE_PROCESSED_INPUT = 0x0001
    _ENABLE_LINE_INPUT = 0x0002
    _ENABLE_ECHO_INPUT = 0x0004
    _ENABLE_VIRTUAL_TERMINAL_INPUT = 0x0200
    _ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004

    _STD_INPUT_HANDLE = -10
    _STD_OUTPUT_HANDLE = -11

    _kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _kernel32.GetStdHandle.restype = wintypes.HANDLE
    _kernel32.GetConsoleMode.argtypes = (wintypes.HANDLE,
                                         ctypes.POINTER(wintypes.DWORD))
    _kernel32.SetConsoleMode.argtypes = (wintypes.HANDLE,
                                         wintypes.DWORD)

    @contextmanager
    def terminal_session():
        h_in = _kernel32.GetStdHandle(_STD_INPUT_HANDLE)
        h_out = _kernel32.GetStdHandle(_STD_OUTPUT_HANDLE)
        old_in = wintypes.DWORD()
        old_out = wintypes.DWORD()
        _kernel32.GetConsoleMode(h_in, ctypes.byref(old_in))
        _kernel32.GetConsoleMode(h_out, ctypes.byref(old_out))

        new_in = ((old_in.value
                   & ~_ENABLE_LINE_INPUT
                   & ~_ENABLE_ECHO_INPUT
                   & ~_ENABLE_PROCESSED_INPUT)
                  | _ENABLE_VIRTUAL_TERMINAL_INPUT)
        new_out = old_out.value | _ENABLE_VIRTUAL_TERMINAL_PROCESSING

        try:
            _kernel32.SetConsoleMode(h_in, new_in)
            _kernel32.SetConsoleMode(h_out, new_out)
            _emit(ENTER_ALT_SCREEN + HIDE_CURSOR)
            yield
        finally:
            _emit(SHOW_CURSOR + EXIT_ALT_SCREEN)
            _kernel32.SetConsoleMode(h_in, old_in.value)
            _kernel32.SetConsoleMode(h_out, old_out.value)

else:
    raise RuntimeError(f"unsupported platform: os.name={os.name!r}")
