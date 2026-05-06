"""Event loop and key dispatch.

A single boring loop: read a key, mutate state, redraw on the dirty
flag, repeat until exit_requested. No threads, no async, no signal
handlers in this module (terminal.py owns SIGWINCH). See DECISIONS.md
sections 3 and 4.

Story 004 introduces the modal Add/Edit dialog and the inline Delete
confirm prompt; the main-screen handler dispatches A / D / E / T to
those, and W remains a story-006 stub.
"""

import sys

import atari_hd

from .input import Key, read_key
from .render import is_legal_label_char, validate_edit_dialog
from .state import (EditDialogState, EditField, EditMode, PromptMode,
                    State)
from .terminal import terminal_session


# Cap defined by atari_hd.MAX_PARTITIONS for any format; all three
# share the same TOS drive-letter ceiling (14).
MAX_PARTITIONS = 14


def handle_key(state: State, key) -> State:
    """Dispatch one keypress into a state mutation. Returns the same
    state (mutated in place) for symmetry with future immutable-state
    refactors."""
    # RESIZE always just triggers a redraw; nothing else.
    if key == Key.RESIZE:
        state.dirty = True
        return state

    # Modal edit dialog takes precedence over every other handler.
    if state.edit_dialog is not None:
        return _handle_edit_dialog(state, key)

    if state.prompt_mode == PromptMode.OFF:
        return _handle_main(state, key)
    if state.prompt_mode in (PromptMode.ASK_NEW_PATH,
                             PromptMode.ASK_LOAD_PATH):
        return _handle_text_prompt(state, key)
    if state.prompt_mode == PromptMode.CONFIRM_OVERWRITE:
        return _handle_overwrite_confirm(state, key)
    if state.prompt_mode == PromptMode.CONFIRM_DELETE:
        return _handle_delete_confirm(state, key)
    return state


# -------------------------------------------------------------------
# Main-screen handler
# -------------------------------------------------------------------

def _handle_main(state: State, key) -> State:
    if key in (Key.ESC, Key.CTRL_C):
        # ESC at the main screen quits; story 006 will route through
        # an unsaved-changes guard.
        state.exit_requested = True
        return state

    # Navigation in the partition list. Hard-stop at top/bottom.
    if key in (Key.UP, Key.DOWN) or (isinstance(key, str)
                                     and key in ("k", "j")):
        return _handle_navigation(state, key)

    if not isinstance(key, str):
        return state
    k = key.lower()
    if k == "q":
        state.exit_requested = True
        return state

    # File-management actions are only available before an image is
    # loaded; once the user has an image, the keybindings switch to
    # partition operations.
    if state.image_path is None:
        if k == "n":
            state.prompt_mode = PromptMode.ASK_NEW_PATH
            state.prompt_buffer = ""
            state.status_message = None
            state.dirty = True
        elif k == "l":
            state.prompt_mode = PromptMode.ASK_LOAD_PATH
            state.prompt_buffer = ""
            state.status_message = None
            state.dirty = True
        return state

    return _handle_partition_action(state, k)


def _handle_navigation(state: State, key) -> State:
    """Move selected_slot up/down. Hard-stop at bounds; ignored when
    the list is empty."""
    if not state.partitions:
        return state
    if key == Key.UP or key == "k":
        if state.selected_slot > 0:
            state.selected_slot -= 1
            state.dirty = True
    elif key == Key.DOWN or key == "j":
        if state.selected_slot < len(state.partitions) - 1:
            state.selected_slot += 1
            state.dirty = True
    return state


def _has_real_partitions(state: State) -> bool:
    return any(p is not None for p in state.partitions)


def _selected_is_real(state: State) -> bool:
    if not state.partitions:
        return False
    if not 0 <= state.selected_slot < len(state.partitions):
        return False
    return state.partitions[state.selected_slot] is not None


def _handle_partition_action(state: State, k: str) -> State:
    if k == "a":
        return _open_add_dialog(state)
    if k == "d":
        return _open_delete_confirm(state)
    if k == "e":
        return _open_edit_dialog(state)
    if k == "t":
        return _toggle_type_inline(state)
    if k == "w":
        # Real handler lands in story 006.
        if _has_real_partitions(state):
            state.status_message = "story 006 implements Write"
            state.dirty = True
        return state
    return state


# -------------------------------------------------------------------
# A: open add dialog at first hole or append slot
# -------------------------------------------------------------------

def _next_free_slot(state: State):
    for i, p in enumerate(state.partitions):
        if p is None:
            return i
    if len(state.partitions) >= MAX_PARTITIONS:
        return None
    return len(state.partitions)


def _open_add_dialog(state: State) -> State:
    slot = _next_free_slot(state)
    if slot is None:
        state.status_message = (
            f"partition cap reached ({MAX_PARTITIONS} max)")
        state.dirty = True
        return state
    is_first = (slot == 0)
    type_choice = "GEM" if state.format_id != "AHDI" or is_first else "BGM"
    if state.format_id != "AHDI":
        type_choice = "GEM"  # arbitrary; ignored for hybrid formats
    state.edit_dialog = EditDialogState(
        mode=EditMode.ADD, slot=slot, field=EditField.SIZE,
        size_buffer="", type_choice=type_choice, label_buffer="")
    state.status_message = None
    state.dirty = True
    return state


# -------------------------------------------------------------------
# E: open edit dialog on selected partition (must be non-empty)
# -------------------------------------------------------------------

def _open_edit_dialog(state: State) -> State:
    if not _selected_is_real(state):
        return state
    slot = state.selected_slot
    part = state.partitions[slot]
    # Initial type_choice from the existing partition's stored ident
    # (set at creation time below) or fall back to the auto-pick.
    ident = getattr(part, "ahdi_ident", None) or _auto_ident(state, slot,
                                                              part.size_mb)
    state.edit_dialog = EditDialogState(
        mode=EditMode.EDIT, slot=slot, field=EditField.SIZE,
        size_buffer=str(part.size_mb),
        type_choice=ident,
        label_buffer=part.name)
    state.status_message = None
    state.dirty = True
    return state


def _auto_ident(state: State, slot: int, size_mb: int) -> str:
    """Initial type pick when the user hasn't chosen one. AHDI slot 0
    is forced GEM by the boot rule; later slots flip at the GEM
    threshold; hybrid formats don't use the field."""
    if state.format_id != "AHDI":
        return "GEM"
    if slot == 0:
        return "GEM"
    threshold = (atari_hd.AHDI_GEM_MAX_MB_STRICT if state.strict_tos
                 else atari_hd.AHDI_GEM_MAX_MB)
    return "GEM" if size_mb <= threshold else "BGM"


# -------------------------------------------------------------------
# T: cycle type on selected (AHDI non-zero only)
# -------------------------------------------------------------------

def _toggle_type_inline(state: State) -> State:
    if not _selected_is_real(state):
        return state
    if state.format_id != "AHDI":
        state.status_message = "Type toggle only applies to AHDI"
        state.dirty = True
        return state
    if state.selected_slot == 0:
        state.status_message = "slot 0 is locked to GEM (boot rule)"
        state.dirty = True
        return state
    part = state.partitions[state.selected_slot]
    current = getattr(part, "ahdi_ident", None) or _auto_ident(
        state, state.selected_slot, part.size_mb)
    new_ident = "BGM" if current == "GEM" else "GEM"
    # Verify the new pick keeps the partition within its cap.
    cap = atari_hd.cap_mb_for_type(state.format_id, state.strict_tos,
                                    new_ident)
    if part.size_mb > cap:
        state.status_message = (f"cannot switch to {new_ident}: "
                                f"size {part.size_mb} MB exceeds {cap} MB cap")
        state.dirty = True
        return state
    part.ahdi_ident = new_ident
    state.status_message = f"slot {state.selected_slot}: {current} -> {new_ident}"
    state.dirty = True
    return state


# -------------------------------------------------------------------
# D: open inline delete confirm prompt
# -------------------------------------------------------------------

def _open_delete_confirm(state: State) -> State:
    if not _selected_is_real(state):
        return state
    state.prompt_mode = PromptMode.CONFIRM_DELETE
    state.pending_delete_slot = state.selected_slot
    state.status_message = None
    state.dirty = True
    return state


def _handle_delete_confirm(state: State, key) -> State:
    if isinstance(key, str) and key.lower() == "y":
        slot = state.pending_delete_slot
        if slot is not None and 0 <= slot < len(state.partitions):
            state.partitions[slot] = None
            state.status_message = f"deleted partition #{slot}"
        state.pending_delete_slot = None
        state.prompt_mode = PromptMode.OFF
        state.dirty = True
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    # Anything else cancels.
    state.pending_delete_slot = None
    state.prompt_mode = PromptMode.OFF
    state.status_message = "delete cancelled"
    state.dirty = True
    return state


# -------------------------------------------------------------------
# Edit dialog key handler
# -------------------------------------------------------------------

def _handle_edit_dialog(state: State, key) -> State:
    d = state.edit_dialog
    if key == Key.ESC:
        state.edit_dialog = None
        state.status_message = "cancelled"
        state.dirty = True
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    if key == Key.ENTER:
        return _commit_edit_dialog(state)

    # Tab cycles fields. Skip TYPE on hybrid formats (it's hidden) and
    # on AHDI slot 0 (locked to GEM, can't be edited).
    if isinstance(key, str) and key == "\t":
        return _cycle_edit_field(state)

    # Per-field key dispatch.
    if d.field == EditField.SIZE:
        return _edit_size_key(state, key)
    if d.field == EditField.TYPE:
        return _edit_type_key(state, key)
    if d.field == EditField.LABEL:
        return _edit_label_key(state, key)
    return state


def _cycle_edit_field(state: State) -> State:
    d = state.edit_dialog
    type_visible = state.format_id == "AHDI" and d.slot != 0
    order = [EditField.SIZE]
    if type_visible:
        order.append(EditField.TYPE)
    order.append(EditField.LABEL)
    idx = order.index(d.field) if d.field in order else 0
    d.field = order[(idx + 1) % len(order)]
    state.dirty = True
    return state


def _edit_size_key(state: State, key) -> State:
    d = state.edit_dialog
    if isinstance(key, str) and key.isdigit():
        if len(d.size_buffer) < 6:  # avoid huge inputs
            d.size_buffer += key
            state.dirty = True
        return state
    if key == Key.BACKSPACE:
        if d.size_buffer:
            d.size_buffer = d.size_buffer[:-1]
            state.dirty = True
        return state
    # Save shortcut: 'S' or 's' anywhere commits when valid.
    if isinstance(key, str) and key.lower() == "s":
        return _commit_edit_dialog(state)
    return state


def _edit_type_key(state: State, key) -> State:
    d = state.edit_dialog
    # Locked on AHDI slot 0; should never reach this in normal flow,
    # but guard for safety.
    if state.format_id == "AHDI" and d.slot == 0:
        return state
    if state.format_id != "AHDI":
        return state
    if key in (Key.LEFT, Key.RIGHT) or (isinstance(key, str) and key == "t"):
        d.type_choice = "BGM" if d.type_choice == "GEM" else "GEM"
        state.dirty = True
        return state
    if isinstance(key, str) and key.lower() == "s":
        return _commit_edit_dialog(state)
    return state


def _edit_label_key(state: State, key) -> State:
    d = state.edit_dialog
    if key == Key.BACKSPACE:
        if d.label_buffer:
            d.label_buffer = d.label_buffer[:-1]
            state.dirty = True
        return state
    if isinstance(key, str) and len(key) == 1:
        # Save shortcut: capital S only here -- a lowercase 's' would
        # be a legitimate label character. Capital S still saves.
        if key == "S" and is_legal_label_char(key):
            # Treat as label char OR save? We pick label-char when the
            # field is LABEL: typing "S" extends the label. The spec
            # offers Enter or 'S' as save shortcuts; in the LABEL
            # field, Enter is the only one to avoid this ambiguity.
            if len(d.label_buffer) < 11:
                d.label_buffer += key.upper()
                state.dirty = True
            return state
        if is_legal_label_char(key):
            if len(d.label_buffer) < 11:
                d.label_buffer += key.upper()
                state.dirty = True
        return state
    return state


def _commit_edit_dialog(state: State) -> State:
    err = validate_edit_dialog(state)
    if err is not None:
        state.status_message = f"cannot save: {err}"
        state.dirty = True
        return state
    d = state.edit_dialog
    size_mb = int(d.size_buffer)
    label = d.label_buffer or _default_label(state, d.slot)
    # Force GEM on AHDI slot 0 regardless of d.type_choice (defensive).
    ident = ("GEM" if state.format_id == "AHDI" and d.slot == 0
             else d.type_choice)

    if d.mode == EditMode.ADD:
        part = atari_hd.Partition(name=label, size_mb=size_mb)
    else:
        part = state.partitions[d.slot]
        part.name = label
        part.size_mb = size_mb
    part.ahdi_ident = ident

    # Place the partition in its slot. For ADD, slot might be a hole
    # (existing None entry) or len(partitions) (append).
    while len(state.partitions) <= d.slot:
        state.partitions.append(None)
    state.partitions[d.slot] = part
    state.selected_slot = d.slot
    state.edit_dialog = None
    state.status_message = (f"slot {d.slot}: {label} {size_mb} MB"
                            + (f" {ident}" if state.format_id == "AHDI" else ""))
    state.dirty = True
    return state


def _default_label(state: State, slot: int) -> str:
    # First slot is the boot partition by AHDI convention; pick a
    # readable default. Other slots get "P<N+1>" so the user doesn't
    # have to type one.
    if slot == 0 and state.format_id == "AHDI":
        return "BOOT"
    return f"P{slot + 1}"


# -------------------------------------------------------------------
# Text-prompt handlers (ASK_NEW_PATH / ASK_LOAD_PATH)
# -------------------------------------------------------------------

def _handle_text_prompt(state: State, key) -> State:
    if key == Key.ESC:
        state.prompt_mode = PromptMode.OFF
        state.prompt_buffer = ""
        state.dirty = True
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    if key == Key.ENTER:
        return _commit_text_prompt(state)
    if key == Key.BACKSPACE:
        if state.prompt_buffer:
            state.prompt_buffer = state.prompt_buffer[:-1]
            state.dirty = True
        return state
    if isinstance(key, str) and len(key) == 1 and key.isprintable():
        state.prompt_buffer += key
        state.dirty = True
    return state


def _commit_text_prompt(state: State) -> State:
    import os
    path = state.prompt_buffer.strip()
    mode = state.prompt_mode
    if not path:
        state.prompt_mode = PromptMode.OFF
        state.prompt_buffer = ""
        state.dirty = True
        return state
    state.prompt_buffer = ""
    if mode == PromptMode.ASK_NEW_PATH:
        if os.path.exists(path):
            state.prompt_mode = PromptMode.CONFIRM_OVERWRITE
            state.pending_path = path
        else:
            state.image_path = path
            state.prompt_mode = PromptMode.OFF
            state.status_message = None
        state.dirty = True
        return state
    if mode == PromptMode.ASK_LOAD_PATH:
        if not os.path.exists(path):
            state.prompt_mode = PromptMode.OFF
            state.status_message = f"file not found: {path}"
        else:
            state.image_path = path
            state.prompt_mode = PromptMode.OFF
            state.status_message = "load not yet implemented (story 007)"
        state.dirty = True
        return state
    return state


# -------------------------------------------------------------------
# Overwrite-confirm handler
# -------------------------------------------------------------------

def _handle_overwrite_confirm(state: State, key) -> State:
    if isinstance(key, str) and key == "O":
        state.image_path = state.pending_path
        state.pending_path = None
        state.prompt_mode = PromptMode.OFF
        state.status_message = None
        state.dirty = True
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    state.pending_path = None
    state.prompt_mode = PromptMode.OFF
    state.status_message = "overwrite cancelled"
    state.dirty = True
    return state


# -------------------------------------------------------------------
# Main entry point
# -------------------------------------------------------------------

def main() -> int:
    state = State()
    with terminal_session():
        while not state.exit_requested:
            if state.dirty:
                from .render import render
                sys.stdout.write(render(state))
                sys.stdout.flush()
                state.dirty = False
            key = read_key()
            state = handle_key(state, key)
    return 0
