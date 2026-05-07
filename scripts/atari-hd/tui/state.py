"""TUI state container.

A single dataclass holds everything the event loop and renderer need.
No module globals; no thread-local storage. The event loop owns the
only instance.

Story 002 introduces the main screen (image-file management) and the
inline-prompt sub-mode for asking the user for a filename or an
overwrite confirmation. Stories 003+ extend State with partitions,
format/strict_tos, and unsaved-changes tracking.
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional


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
    CONFIRM_DELETE = "confirm_delete"
    # Format selector (story 005)
    ASK_FORMAT = "ask_format"
    ASK_STRICT_TOS = "ask_strict_tos"
    CONFIRM_DROP_PARTITIONS = "confirm_drop_partitions"
    # Commit / write flow (story 006)
    CONFIRM_OVERWRITE_WRITE = "confirm_overwrite_write"
    CONFIRM_DISCARD_UNSAVED = "confirm_discard_unsaved"
    # Load existing image (story 007)
    CONFIRM_DISCARD_BEFORE_LOAD = "confirm_discard_before_load"


class EditField(Enum):
    """Focused field inside the add/edit dialog."""
    SIZE = "size"
    TYPE = "type"
    LABEL = "label"


class EditMode(Enum):
    ADD = "add"
    EDIT = "edit"


@dataclass
class EditDialogState:
    """In-flight state of the modal partition-edit dialog. Lives on
    State.edit_dialog while the dialog is open; cleared on cancel /
    commit."""
    mode: EditMode
    # For ADD: the slot index the partition will land in (first hole
    # or len(partitions)). For EDIT: the slot being edited.
    slot: int
    field: EditField = EditField.SIZE
    size_buffer: str = ""           # digits only
    type_choice: str = "GEM"        # "GEM" / "BGM"; ignored on hybrid formats
    label_buffer: str = ""          # accumulated uppercase ASCII, <= 11 chars


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

    # Partition plan -----------------------------------------------------
    # Driver format. Values match atari_hd.FORMAT_AHDI / FORMAT_PPDRIVER /
    # FORMAT_HDDRIVER (plain strings). Stored as a string so this module
    # does not need to import atari_hd.
    format_id: str = "AHDI"
    # AHDI-only: TOS<1.04 strict caps. Ignored on the hybrid formats.
    strict_tos: bool = False
    # In-memory partition list. Items are atari_hd.Partition instances
    # *or* None (sparse holes left by Delete; the next Add fills the
    # first hole). render.py only depends on duck-typed attribute access
    # (.name / .size_mb / .start_lba / .size_sectors / .ahdi_ident on
    # set partitions; None entries are rendered as "(empty)"), so this
    # module doesn't import atari_hd.
    partitions: List = field(default_factory=list)
    # Open edit dialog (None when no dialog is up).
    edit_dialog: Optional[EditDialogState] = None
    # Slot index pending deletion -- carried through CONFIRM_DELETE so
    # the confirm dialog knows which partition the y/n applies to.
    pending_delete_slot: Optional[int] = None

    # Format-selector pending state (story 005). pending_format is set
    # when the user picks a format but the change hasn't been applied
    # yet (might still need ASK_STRICT_TOS or CONFIRM_DROP_PARTITIONS
    # before commit). pending_strict_tos likewise. pending_drop_slots
    # is the list of partition indices that would violate the pending
    # format's caps; the user is asked to discard them via
    # CONFIRM_DROP_PARTITIONS.
    pending_format: Optional[str] = None
    pending_strict_tos: Optional[bool] = None
    pending_drop_slots: Optional[List[int]] = None

    # Story 006: True when the in-memory plan has changed since the
    # last successful write. Set by every mutating handler; cleared
    # on successful write and on image_path change (load / new).
    unsaved_changes: bool = False
    # Story 008: help overlay visibility. Toggled by `?` from any
    # screen; Esc / `?` while open closes it. Layers on top of every
    # other UI element (main / dialog / prompt) without disturbing
    # them, so closing returns the user to whatever was underneath.
    show_help: bool = False
    # Index of the highlighted row in the partition list.
    selected_slot: int = 0
    # First visible row when the list is taller than the body. With
    # MAX_PARTITIONS=14 and ~17 visible rows at 80x24 the value stays
    # at 0 in practice; the code path exists for taller plans.
    scroll_top: int = 0
    # -------------------------------------------------------------------

    dirty: bool = True
    exit_requested: bool = False
