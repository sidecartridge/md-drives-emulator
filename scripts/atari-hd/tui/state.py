"""TUI state container.

A single dataclass holds everything the event loop and renderer need.
No module globals; no thread-local storage. The event loop owns the
only instance.

Story 001 keeps the state minimal -- just enough to drive the
placeholder screen and the exit handling. Stories 002+ add fields as
they introduce new screens and operations.
"""

from dataclasses import dataclass
from enum import Enum


class Screen(Enum):
    """Screen identifiers. Each render path maps one Screen to a frame
    string in render.py."""
    PLACEHOLDER = "placeholder"
    # Story 002: MAIN
    # Story 003: PARTITION_LIST
    # Story 004: EDIT_DIALOG
    # Story 005: FORMAT_SELECTOR
    # Story 006: WRITE_CONFIRM
    # Story 008: HELP


@dataclass
class State:
    """Top-level TUI state.

    `dirty` is True when the next loop iteration should redraw; the
    event loop clears it after writing the frame. `exit_requested` is
    the loop's termination flag.
    """
    screen: Screen = Screen.PLACEHOLDER
    dirty: bool = True
    exit_requested: bool = False
