"""Cross-platform terminal user interface for atari_hd.py.

Plain ANSI escape sequences only -- no curses, no third-party libs.
See DECISIONS.md for the framework choice and rationale.

Story 001 lands the skeleton: terminal session manager, key reader,
state dataclass, render function, and a placeholder screen. Real
screens (partition list, edit dialog, format selector, commit flow,
load existing image) come in stories 002-008.
"""
