"""TUI state container.

A single dataclass holds everything the event loop and renderer need.
No module globals; no thread-local storage. The event loop owns the
only instance.

Story 002 introduces the main screen (image-file management) and the
inline-prompt sub-mode for asking the user for a filename or an
overwrite confirmation. Stories 003+ extend State with partitions,
format/strict_tos, and unsaved-changes tracking.
"""

from dataclasses import dataclass
from enum import Enum
from typing import Optional


class Screen(Enum):
    """Screen identifiers. Each render path maps one Screen to a frame
    string in render.py."""
    MAIN = "main"
    # Story 003: PARTITION_LIST  (likely folded into MAIN's body)
    # Story 004: EDIT_DIALOG
    # Story 005: FORMAT_SELECTOR
    # Story 006: WRITE_CONFIRM
    # Story 008: HELP


class PromptMode(Enum):
    """Inline-prompt sub-mode. When OFF the status bar shows the key
    bindings; otherwise it shows a question (with optional buffered
    text input) and routes keys through the prompt handler."""
    OFF = "off"
    ASK_NEW_PATH = "ask_new_path"
    ASK_LOAD_PATH = "ask_load_path"
    CONFIRM_OVERWRITE = "confirm_overwrite"


@dataclass
class State:
    """Top-level TUI state.

    `dirty` is True when the next loop iteration should redraw; the
    event loop clears it after writing the frame. `exit_requested` is
    the loop's termination flag.
    """
    screen: Screen = Screen.MAIN
    image_path: Optional[str] = None
    prompt_mode: PromptMode = PromptMode.OFF
    prompt_buffer: str = ""
    # Path the user just typed but that already exists -- carried into
    # CONFIRM_OVERWRITE so the confirm dialog can show what it'd
    # overwrite and apply it on accept.
    pending_path: Optional[str] = None
    # Transient one-line message shown on the bottom row of the status
    # bar (e.g. "file not found", "load not yet implemented"). Cleared
    # on the next state-mutating key.
    status_message: Optional[str] = None
    dirty: bool = True
    exit_requested: bool = False
