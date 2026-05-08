"""Event loop and key dispatch.

A single boring loop: read a key, mutate state, redraw on the dirty
flag, repeat until exit_requested. No threads, no async, no signal
handlers in this module (terminal.py owns SIGWINCH). See DECISIONS.md
sections 3 and 4.

Story 004 introduces the modal Add/Edit dialog and the inline Delete
confirm prompt; the main-screen handler dispatches A / D / E / T to
those, and W remains a story-006 stub.
"""

import os
import shutil
import sys
import tempfile

import atari_hd

from .input import Key, read_key
from .render import (MIN_COLS, MIN_ROWS,
                      is_legal_label_char, validate_edit_dialog)
from .state import (EditDialogState, EditField, EditMode, PromptMode,
                    State)
from .terminal import terminal_session


# Cap defined by atari_hd.MAX_PARTITIONS for any format; all three
# share the same TOS drive-letter ceiling (14).
MAX_PARTITIONS = 14


def _primary_count_for_state(state, *, count_dialog_add=False) -> int:
    """Compute primary_count for partition_layout given the current
    state. PPDRIVER allows up to 4 primaries; HDDRIVER caps at 1; AHDI
    uses up to 4 AHDI-table slots.

    `count_dialog_add` is True when an ADD dialog is open and we want
    the layout the writer would produce *after* the user commits --
    important for the dialog's live cap label so the user sees the
    right primary/logical cap at the slot they're about to fill.
    """
    n = sum(1 for p in state.partitions if p is not None)
    if count_dialog_add and state.edit_dialog and \
            state.edit_dialog.mode == EditMode.ADD:
        n += 1
    if n < 1:
        n = 1
    if n > MAX_PARTITIONS:
        n = MAX_PARTITIONS
    return atari_hd.partition_layout(state.format_id, n)["primary_count"]


def handle_key(state: State, key) -> State:
    """Dispatch one keypress into a state mutation. Returns the same
    state (mutated in place) for symmetry with future immutable-state
    refactors."""
    # RESIZE always just triggers a redraw; nothing else.
    if key == Key.RESIZE:
        state.dirty = True
        return state

    # Help overlay (story 008): when open, swallow input until the
    # user closes it via Esc or `?` again. Layered above every other
    # handler so any underlying screen / dialog / prompt is preserved
    # exactly as it was when the user opened help.
    if state.show_help:
        return _handle_help_open(state, key)
    # `?` opens help from any screen, including inside dialogs and
    # prompts. We accept it as a global shortcut even if it would
    # otherwise be a printable char in a text-entry prompt; users
    # rarely need a literal `?` in image filenames, and the spec
    # requires a single consistent help key.
    if isinstance(key, str) and key == "?":
        state.show_help = True
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
    if state.prompt_mode == PromptMode.ASK_FORMAT:
        return _handle_ask_format(state, key)
    if state.prompt_mode == PromptMode.ASK_STRICT_TOS:
        return _handle_ask_strict_tos(state, key)
    if state.prompt_mode == PromptMode.CONFIRM_DROP_PARTITIONS:
        return _handle_drop_confirm(state, key)
    if state.prompt_mode == PromptMode.CONFIRM_OVERWRITE_WRITE:
        return _handle_write_overwrite_confirm(state, key)
    if state.prompt_mode == PromptMode.CONFIRM_DISCARD_UNSAVED:
        return _handle_discard_unsaved_confirm(state, key)
    if state.prompt_mode == PromptMode.CONFIRM_DISCARD_BEFORE_LOAD:
        return _handle_discard_before_load_confirm(state, key)
    if state.prompt_mode == PromptMode.ASK_AHDI_DRIVER_PATH:
        return _handle_ask_ahdi_driver_path(state, key)
    return state


# -------------------------------------------------------------------
# Help overlay (story 008)
# -------------------------------------------------------------------

def _handle_help_open(state: State, key) -> State:
    """While the help overlay is up: Esc / `?` close it, Ctrl-C still
    exits (consistent with everywhere), all other keys are ignored.
    Underlying state (edit_dialog / prompt_mode / partition selection)
    is never touched, so closing the overlay returns the user exactly
    where they were."""
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    if key == Key.ESC or (isinstance(key, str) and key == "?"):
        state.show_help = False
        state.dirty = True
        return state
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
        return _request_exit(state)

    # File-management actions (N, L) are always available so the user
    # can load a different image mid-session. L routes through the
    # unsaved-changes guard when there's an in-memory plan that would
    # otherwise be silently discarded.
    if k == "n":
        state.prompt_mode = PromptMode.ASK_NEW_PATH
        state.prompt_buffer = ""
        state.status_message = None
        state.dirty = True
        return state
    if k == "l":
        return _start_load(state)

    # Partition operations only when an image is set.
    if state.image_path is None:
        return state
    return _handle_partition_action(state, k)


def _start_load(state: State) -> State:
    """Open ASK_LOAD_PATH, but if there are unsaved in-memory changes
    first route through CONFIRM_DISCARD_BEFORE_LOAD so the user
    doesn't silently lose them. Mirrors the Q exit-guard pattern."""
    if state.unsaved_changes:
        state.prompt_mode = PromptMode.CONFIRM_DISCARD_BEFORE_LOAD
        state.dirty = True
        return state
    state.prompt_mode = PromptMode.ASK_LOAD_PATH
    state.prompt_buffer = ""
    state.status_message = None
    state.dirty = True
    return state


def _handle_discard_before_load_confirm(state: State, key) -> State:
    if isinstance(key, str) and key.lower() == "y":
        state.prompt_mode = PromptMode.ASK_LOAD_PATH
        state.prompt_buffer = ""
        state.dirty = True
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    state.prompt_mode = PromptMode.OFF
    state.status_message = "load cancelled"
    state.dirty = True
    return state


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
    if k == "f":
        return _open_format_chooser(state)
    if k == "w":
        return _start_write(state)
    if k == "b":
        return _handle_bootable_toggle(state)
    return state


def _handle_bootable_toggle(state: State) -> State:
    """B-key handler. Behavior is per-format:
      - AHDI: opens the ASK_AHDI_DRIVER_PATH prompt when bootable is
        off (we need the user-supplied ICDBOOT.PRG); when on,
        clears bootable + path.
      - PPDRIVER: single-key toggle (uses the bundled boot blob;
        no path needed).
      - HDDRIVER: surfaces the manual-install pointer; never
        flips bootable on (story 006 -- not self-bootable).
    """
    if state.format_id == "AHDI":
        if state.bootable:
            state.bootable = False
            state.ahdi_driver_path = None
            state.status_message = "AHDI bootable mode disabled"
            state.unsaved_changes = True
            state.dirty = True
            return state
        # Pre-check: ICD's continuation IPL fails on disks with boot
        # partition > AHDI_BOOTABLE_BOOT_MAX_MB. Surface that BEFORE
        # we prompt for a driver path so the user can shrink slot 0
        # first instead of typing a path and then hitting the cap.
        slot0 = state.partitions[0] if state.partitions else None
        if (slot0 is not None
                and slot0.size_mb > atari_hd.AHDI_BOOTABLE_BOOT_MAX_MB):
            state.status_message = (
                f"slot 0 is {slot0.size_mb} MB; AHDI bootable mode "
                f"requires it <= {atari_hd.AHDI_BOOTABLE_BOOT_MAX_MB} "
                f"MB (ICD IPL constraint). Edit slot 0 to shrink it, "
                f"then press B again.")
            state.dirty = True
            return state
        state.prompt_mode = PromptMode.ASK_AHDI_DRIVER_PATH
        state.prompt_buffer = ""
        state.status_message = None
        state.dirty = True
        return state
    if state.format_id == "PPDRIVER":
        state.bootable = not state.bootable
        state.ahdi_driver_path = None
        state.status_message = (
            "PPDRIVER bootable mode enabled (bundled blob)"
            if state.bootable
            else "PPDRIVER bootable mode disabled")
        state.unsaved_changes = True
        state.dirty = True
        return state
    # HDDRIVER -- not self-bootable; point at the docs.
    state.status_message = (
        "HDDRIVER images aren't self-bootable from this tool. "
        "Run HDDRUTIL.APP after building -- see BOOTABLE.md.")
    state.dirty = True
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
    # Default Kind: slot 0 -> primary (mandatory). HDDRIVER slot >= 1 ->
    # extended (mandatory). PPDRIVER slot >= 1 -> default extended,
    # since the typical "second partition is the bulk DATA" workflow
    # wants 511 MB headroom; the user can flip back to primary via
    # the Kind toggle if they want a small primary instead.
    if is_first:
        kind_choice = "primary"
    elif state.format_id == "HDDRIVER":
        kind_choice = "extended"
    else:
        kind_choice = "extended"
    state.edit_dialog = EditDialogState(
        mode=EditMode.ADD, slot=slot, field=EditField.SIZE,
        size_buffer="", type_choice=type_choice,
        kind_choice=kind_choice, label_buffer="")
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
    # Kind is locked for slot 0 / HDDRIVER; for PPDRIVER slot >= 1 we
    # carry over the partition's existing is_extended.
    if slot == 0:
        kind_choice = "primary"
    elif state.format_id == "HDDRIVER":
        kind_choice = "extended"
    else:
        kind_choice = ("extended"
                        if getattr(part, "is_extended", False)
                        else "primary")
    state.edit_dialog = EditDialogState(
        mode=EditMode.EDIT, slot=slot, field=EditField.SIZE,
        size_buffer=str(part.size_mb),
        type_choice=ident,
        kind_choice=kind_choice,
        label_buffer=part.name)
    state.status_message = None
    state.dirty = True
    return state


def _auto_ident(state: State, slot: int, size_mb: int) -> str:
    """Initial type pick when the user hasn't chosen one. The AHDI ident
    follows bps strictly (GEM iff bps=512, BGM iff bps>512), so we
    delegate to ahdi_partition_id(). Slot 0 still ends up as GEM
    because partition_cap_mb() caps it at the GEM region; we just
    don't special-case it here."""
    if state.format_id != "AHDI":
        return "GEM"
    return atari_hd.ahdi_partition_id(size_mb).decode()


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
        state.status_message = ("slot 0 is locked to GEM "
                                 "(legacy-driver compatibility)")
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
    state.unsaved_changes = True
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
            state.unsaved_changes = True
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
# F: format selector + AHDI strict-TOS toggle (story 005)
# -------------------------------------------------------------------

def _open_format_chooser(state: State) -> State:
    state.prompt_mode = PromptMode.ASK_FORMAT
    state.status_message = None
    state.dirty = True
    return state


def _handle_ask_format(state: State, key) -> State:
    if key == Key.ESC:
        _reset_format_pending(state)
        state.dirty = True
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    if not isinstance(key, str):
        return state
    chosen = {"a": "AHDI", "p": "PPDRIVER", "h": "HDDRIVER"}.get(key.lower())
    if chosen is None:
        return state
    state.pending_format = chosen
    if chosen == "AHDI":
        # Need the strict-TOS answer before we can revalidate.
        state.prompt_mode = PromptMode.ASK_STRICT_TOS
        state.dirty = True
        return state
    # Hybrid formats don't honor strict_tos.
    state.pending_strict_tos = False
    return _resolve_pending_format(state)


def _handle_ask_strict_tos(state: State, key) -> State:
    if key == Key.ESC:
        _reset_format_pending(state)
        state.dirty = True
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    # y -> strict on; anything else (including N, Enter, others) -> off.
    state.pending_strict_tos = (
        isinstance(key, str) and key.lower() == "y")
    return _resolve_pending_format(state)


def _resolve_pending_format(state: State) -> State:
    """We have pending_format and pending_strict_tos. Compute which
    existing partitions would violate the new caps; if any, ask for
    confirmation before dropping. If none, apply directly."""
    violators = _find_violator_slots(state, state.pending_format,
                                      state.pending_strict_tos)
    if not violators:
        _apply_pending_format(state, drop_slots=())
        return state
    state.pending_drop_slots = violators
    state.prompt_mode = PromptMode.CONFIRM_DROP_PARTITIONS
    state.dirty = True
    return state


def _handle_drop_confirm(state: State, key) -> State:
    if isinstance(key, str) and key.lower() == "y":
        _apply_pending_format(state, drop_slots=state.pending_drop_slots or ())
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    # Anything else cancels the format change entirely (no partitions
    # dropped; format unchanged).
    fmt = state.pending_format
    _reset_format_pending(state)
    state.status_message = f"format change to {fmt} cancelled"
    state.dirty = True
    return state


def _apply_pending_format(state: State, drop_slots) -> State:
    new_format = state.pending_format
    new_strict = state.pending_strict_tos or False
    actually_changed = (new_format != state.format_id
                        or new_strict != state.strict_tos
                        or bool(drop_slots))
    for slot in drop_slots:
        if 0 <= slot < len(state.partitions):
            state.partitions[slot] = None
    if new_format != state.format_id:
        # Bootable-mode state is per-format; clear it so the next
        # write doesn't try to apply (e.g.) an AHDI driver path on
        # a now-PPDRIVER plan. The user re-enables via B if wanted.
        state.bootable = False
        state.ahdi_driver_path = None
    state.format_id = new_format
    state.strict_tos = new_strict
    if actually_changed:
        state.unsaved_changes = True
    msg = f"format -> {new_format}"
    if new_format == "AHDI":
        msg += f" (TOS<1.04: {'on' if new_strict else 'off'})"
    if drop_slots:
        msg += f"; dropped {len(drop_slots)} partition(s)"
    _reset_format_pending(state)
    state.status_message = msg
    state.dirty = True
    return state


def _reset_format_pending(state: State) -> None:
    state.prompt_mode = PromptMode.OFF
    state.pending_format = None
    state.pending_strict_tos = None
    state.pending_drop_slots = None


def _find_violator_slots(state: State, candidate_format: str,
                         candidate_strict: bool):
    """Return a list of slot indices whose existing partition would
    exceed the candidate format's per-type cap. Empty list means the
    format change is cap-safe."""
    violators = []
    is_hybrid = candidate_format in ("PPDRIVER", "HDDRIVER")
    # AHDI's cap_mb_for_type still consults primary_count for slot 0
    # vs. >=1 ident routing; hybrid uses per-partition is_extended.
    n = sum(1 for p in state.partitions if p is not None) or 1
    if n > MAX_PARTITIONS:
        n = MAX_PARTITIONS
    candidate_primary = atari_hd.partition_layout(candidate_format,
                                                    n)["primary_count"]
    for i, part in enumerate(state.partitions):
        if part is None:
            continue
        if is_hybrid:
            cap = (atari_hd.HYBRID_PRIMARY_MAX_MB
                   if not part.is_extended
                   else atari_hd.HYBRID_MAX_PARTITION_MB)
        else:
            ident = _effective_ident(state, i, part, candidate_format)
            cap = atari_hd.cap_mb_for_type(candidate_format, candidate_strict,
                                            ident, slot_index=i,
                                            primary_count=candidate_primary)
        if part.size_mb > cap:
            violators.append(i)
    return violators


def _effective_ident(state: State, slot: int, part, format_id: str) -> str:
    """The ident the new format would assign this partition. AHDI ident
    follows bps strictly (delegated to ahdi_partition_id); explicit
    ahdi_ident overrides are honored only when consistent with the bps
    derived from size, so we just return the size-derived value."""
    if format_id != "AHDI":
        return "FAT16"
    return atari_hd.ahdi_partition_id(part.size_mb).decode()


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
    # on AHDI slot 0 (clamped to GEM for legacy-driver compatibility,
    # not user-editable in v1).
    if isinstance(key, str) and key == "\t":
        return _cycle_edit_field(state)

    # Per-field key dispatch.
    if d.field == EditField.KIND:
        return _edit_kind_key(state, key)
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
    # KIND is the user's free choice only on PPDRIVER slot >= 1.
    # Slot 0 (always primary) and HDDRIVER slot >= 1 (always extended)
    # have the kind locked and skipped from the Tab order.
    kind_editable = (state.format_id == "PPDRIVER" and d.slot != 0)
    order = []
    if kind_editable:
        order.append(EditField.KIND)
    order.append(EditField.SIZE)
    if type_visible:
        order.append(EditField.TYPE)
    order.append(EditField.LABEL)
    idx = order.index(d.field) if d.field in order else 0
    d.field = order[(idx + 1) % len(order)]
    state.dirty = True
    return state


def _edit_kind_key(state: State, key) -> State:
    """Toggle Kind (primary <-> extended) on hybrid PPDRIVER slot >= 1."""
    d = state.edit_dialog
    # Locked: nothing to do.
    if state.format_id != "PPDRIVER" or d.slot == 0:
        return state
    if key in (Key.LEFT, Key.RIGHT) or (isinstance(key, str) and key == "t"):
        d.kind_choice = ("extended" if d.kind_choice == "primary"
                          else "primary")
        state.dirty = True
        return state
    if isinstance(key, str) and key.lower() == "s":
        return _commit_edit_dialog(state)
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
    # Force GEM on AHDI slot 0 regardless of d.type_choice (defensive).
    ident = ("GEM" if state.format_id == "AHDI" and d.slot == 0
             else d.type_choice)

    # Resolve the on-disk Kind: slot 0 is always primary; HDDRIVER
    # slot >= 1 is always extended; PPDRIVER slot >= 1 follows
    # d.kind_choice. AHDI partitions don't expose Kind in the dialog
    # (still derived at write time), so we leave is_extended at the
    # dataclass default for AHDI; plan_image will reset it from the
    # AHDI N-driven layout regardless.
    if d.slot == 0:
        is_extended = False
    elif state.format_id == "HDDRIVER":
        is_extended = True
    elif state.format_id == "PPDRIVER":
        is_extended = (d.kind_choice == "extended")
    else:
        is_extended = False  # AHDI; plan_image overrides per N

    # Default label uses the resolved Kind: 'P' prefix for primaries,
    # 'E' for extendeds. AHDI slot 0 stays 'BOOT'.
    label = d.label_buffer or _default_label(state, d.slot, is_extended)

    if d.mode == EditMode.ADD:
        part = atari_hd.Partition(name=label, size_mb=size_mb,
                                    is_extended=is_extended)
    else:
        part = state.partitions[d.slot]
        part.name = label
        part.size_mb = size_mb
        part.is_extended = is_extended
    # ahdi_ident is an AHDI-table concept (GEM / BGM / XGM); it has no
    # meaning on hybrid formats whose MBR-side identity is type 0x06
    # FAT16. Setting it on a PPDRIVER / HDDRIVER partition would
    # confuse _partition_ident's display logic into showing "GEM" /
    # "BGM" in the Type column instead of "FAT16".
    if state.format_id == "AHDI":
        part.ahdi_ident = ident
    else:
        part.ahdi_ident = None

    # Place the partition in its slot. For ADD, slot might be a hole
    # (existing None entry) or len(partitions) (append).
    while len(state.partitions) <= d.slot:
        state.partitions.append(None)
    state.partitions[d.slot] = part
    state.selected_slot = d.slot
    state.edit_dialog = None
    state.unsaved_changes = True
    state.status_message = (f"slot {d.slot}: {label} {size_mb} MB"
                            + (f" {ident}" if state.format_id == "AHDI" else ""))
    state.dirty = True
    return state


def _default_label(state: State, slot: int, is_extended: bool = False) -> str:
    # First slot on AHDI is the boot partition; pick a readable
    # default. Other slots get a prefix that reflects the partition's
    # role -- "P<N+1>" for primaries, "E<N+1>" for extendeds -- so a
    # mixed PPDRIVER layout reads cleanly: BOOT (or P1), E2, E3, ...
    if slot == 0 and state.format_id == "AHDI":
        return "BOOT"
    prefix = "E" if is_extended else "P"
    return f"{prefix}{slot + 1}"


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


def _handle_ask_ahdi_driver_path(state: State, key) -> State:
    """Text prompt that takes a path to an AHDI driver (ICDBOOT.PRG
    or equivalent). On Enter, validates the .PRG magic via the
    helper from atari_hd.py; on success enables bootable mode."""
    if key == Key.ESC:
        state.prompt_mode = PromptMode.OFF
        state.prompt_buffer = ""
        state.dirty = True
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    if key == Key.ENTER:
        path = state.prompt_buffer.strip()
        state.prompt_buffer = ""
        if not path:
            state.prompt_mode = PromptMode.OFF
            state.status_message = "no driver path entered; bootable mode unchanged"
            state.dirty = True
            return state
        # Reuse the same validator the CLI flag uses.
        err = atari_hd._validate_ahdi_driver(path)
        if err:
            state.prompt_mode = PromptMode.OFF
            state.status_message = err
            state.dirty = True
            return state
        state.bootable = True
        state.ahdi_driver_path = path
        state.prompt_mode = PromptMode.OFF
        state.unsaved_changes = True
        state.status_message = (
            f"AHDI bootable: {os.path.basename(path)} validated "
            f"(.PRG magic OK)")
        state.dirty = True
        return state
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
            state.dirty = True
            return state
        return _do_load(state, path)
    return state


def _do_load(state: State, path: str) -> State:
    """Parse the image at `path` and replace the in-memory partition
    plan with what the file says. On any parse error the previous
    state is left intact and the error is surfaced in the status
    bar -- never partial state."""
    try:
        loaded = atari_hd.load_image(path)
    except atari_hd.ImageLoadError as e:
        state.prompt_mode = PromptMode.OFF
        state.status_message = f"load failed: {e}"
        state.dirty = True
        return state
    except Exception as e:
        state.prompt_mode = PromptMode.OFF
        state.status_message = f"load failed (unexpected): {e}"
        state.dirty = True
        return state

    state.image_path = path
    state.format_id = loaded["format_id"]
    state.strict_tos = loaded["strict_tos"]
    state.partitions = list(loaded["partitions"])
    state.selected_slot = 0
    state.scroll_top = 0
    state.unsaved_changes = False
    state.prompt_mode = PromptMode.OFF
    state.dirty = True

    n = len(state.partitions)
    msg = f"loaded {state.format_id}: {n} partition(s) from {path}"
    if loaded.get("strict_tos_inferred"):
        msg += (f"  [TOS<1.04: "
                f"{'on' if state.strict_tos else 'off'} -- guessed]")
    # Surface the first compatibility warning (when present) so a user
    # loading a foreign-tool image (mkdosfs, acsi2stm, Falcon-format,
    # ...) sees the salient issue immediately. The full list is on
    # the planning summary if a future story wants a help/details
    # screen.
    warnings = loaded.get("warnings") or []
    if warnings:
        extra = warnings[0]
        if len(warnings) > 1:
            extra = f"{extra} (+{len(warnings) - 1} more)"
        msg += f"  WARNING: {extra}"
    state.status_message = msg
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
# W: commit / write flow (story 006)
# -------------------------------------------------------------------

def _start_write(state: State) -> State:
    """Run the pre-flight checks; if the target file already exists,
    open the overwrite-confirm prompt; otherwise proceed straight to
    the write."""
    err = _preflight_check(state)
    if err is not None:
        state.status_message = f"cannot write: {err}"
        state.dirty = True
        return state
    if os.path.exists(state.image_path):
        state.prompt_mode = PromptMode.CONFIRM_OVERWRITE_WRITE
        state.dirty = True
        return state
    return _do_write(state)


def _preflight_check(state: State):
    """Return None when every pre-flight rule passes, otherwise a
    short reason string. Doesn't mutate state."""
    real = [p for p in state.partitions if p is not None]
    if not real:
        return "no partitions to write"
    min_mb = atari_hd.format_min_partition_mb(state.format_id)
    primary_count = _primary_count_for_state(state)
    is_hybrid = state.format_id in ("PPDRIVER", "HDDRIVER")
    for i, part in enumerate(real):
        if part.size_mb < min_mb:
            return (f"partition {part.name!r} ({part.size_mb} MB) is "
                    f"below the {min_mb} MB minimum for "
                    f"{state.format_id}")
        if is_hybrid:
            # Hybrid caps follow the per-partition is_extended flag,
            # not the slot index. After load_image's auto-migration,
            # an oversize-on-disk primary may have is_extended=True
            # even though it lives at a low slot index; using
            # slot_index/primary_count would mis-cap it as a primary
            # and reject the write.
            cap = (atari_hd.HYBRID_PRIMARY_MAX_MB if not part.is_extended
                   else atari_hd.HYBRID_MAX_PARTITION_MB)
        else:
            ident = _effective_ident(state, i, part, state.format_id)
            cap = atari_hd.cap_mb_for_type(state.format_id, state.strict_tos,
                                            ident, slot_index=i,
                                            primary_count=primary_count)
        if part.size_mb > cap:
            return (f"partition {part.name!r} ({part.size_mb} MB) "
                    f"exceeds {cap} MB cap")
    # AHDI bootable mode pins slot 0 to a tighter cap (15 MB) -- ICD's
    # continuation IPL fails on disks with a larger boot partition.
    # Mirror plan_image's validation here so W shows the error inline
    # instead of making the user wait for the build-time abort.
    if (state.format_id == "AHDI" and state.bootable
            and real and real[0].size_mb > atari_hd.AHDI_BOOTABLE_BOOT_MAX_MB):
        return (f"AHDI bootable boot partition (slot 0, "
                f"{real[0].name!r}) is {real[0].size_mb} MB; cap is "
                f"{atari_hd.AHDI_BOOTABLE_BOOT_MAX_MB} MB. Shrink it "
                f"or press B to disable bootable mode.")
    return None


def _handle_write_overwrite_confirm(state: State, key) -> State:
    if isinstance(key, str) and key.lower() == "y":
        state.prompt_mode = PromptMode.OFF
        return _do_write(state)
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    state.prompt_mode = PromptMode.OFF
    state.status_message = "write cancelled"
    state.dirty = True
    return state


def _do_write(state: State) -> State:
    """Build the plan and call atari_hd.build_image() into a temp
    file in the same directory, then os.replace into the target.
    Atomic so a partial write never overwrites the existing file."""
    target = state.image_path
    target_dir = os.path.dirname(os.path.abspath(target)) or "."
    real_partitions = [p for p in state.partitions if p is not None]

    # Compute a generous initial image size; plan_image will bump it
    # if the partitions need more headroom (root sector + EBR / XGM
    # chain overhead).
    total_partition_mb = sum(p.size_mb for p in real_partitions)
    image_mb = max(total_partition_mb + 1, 2)

    tmp_path = None
    try:
        # tempfile in the same directory so os.replace() is atomic
        # (same filesystem). delete=False -> we own cleanup.
        tmp = tempfile.NamedTemporaryFile(
            prefix="atari_hd_write_", suffix=".img.tmp",
            dir=target_dir, delete=False)
        tmp_path = tmp.name
        tmp.close()

        # Fresh Partition objects so plan_image's mutations
        # (size_sectors / start_lba / ebr_lba) don't leak back into our
        # in-memory list. CRITICAL: is_extended must be carried over --
        # it's the source of truth for the hybrid layout role and
        # plan_image's validation rejects partitions whose size exceeds
        # the wrong cap if we forget it (a >255 MB partition copied
        # without is_extended=True would be planned as a primary and
        # rejected at the 255 MB cap).
        plan_partitions = []
        for src in real_partitions:
            new = atari_hd.Partition(
                name=src.name, size_mb=src.size_mb,
                is_extended=getattr(src, "is_extended", False))
            # Preserve the user's explicit ident so XGM-chain logicals
            # render the right type if the user picked one. (Currently
            # ahdi_partition_id ignores this for XGM logicals; a
            # follow-up could thread it through. v1 is OK.)
            ident = getattr(src, "ahdi_ident", None)
            if ident is not None:
                new.ahdi_ident = ident
            plan_partitions.append(new)

        # Wire through the bootable-mode toggles. plan_image's own
        # validators reject these on the wrong format; we mirror the
        # cleanup in _apply_pending_format so a stale bootable flag
        # never reaches here.
        ahdi_driver = (state.ahdi_driver_path
                       if (state.bootable
                           and state.format_id == "AHDI")
                       else None)
        ppdriver_bootable = (state.bootable
                              and state.format_id == "PPDRIVER")
        plan = atari_hd.plan_image(
            state.format_id, image_path=tmp_path, image_mb=image_mb,
            partitions=plan_partitions, strict_tos=state.strict_tos,
            ahdi_driver_path=ahdi_driver,
            ppdriver_bootable=ppdriver_bootable)

        atari_hd.build_image(plan, progress=_make_progress_callback())

        os.replace(tmp_path, target)
        tmp_path = None  # successfully consumed
    except KeyboardInterrupt:
        # Don't swallow; let the terminal_session restore + propagate.
        if tmp_path is not None:
            try: os.unlink(tmp_path)
            except OSError: pass
        raise
    except Exception as e:
        if tmp_path is not None:
            try: os.unlink(tmp_path)
            except OSError: pass
        state.status_message = f"write failed: {e}"
        state.dirty = True
        return state

    state.status_message = (f"Written {len(real_partitions)} "
                            f"partition(s) to {target}")
    state.unsaved_changes = False
    state.dirty = True
    return state


def _make_progress_callback():
    """Build a callback that overwrites the bottom row in place
    during the write. The callback is invoked synchronously from
    inside build_image while terminal_session holds the alt screen,
    so direct stdout writes are safe."""
    def emit(message):
        size = shutil.get_terminal_size((MIN_COLS, MIN_ROWS))
        cols, rows = size.columns, size.lines
        line = message[:cols].ljust(cols)
        # Move to last row, clear it, write, no newline.
        sys.stdout.write(f"\x1b[{rows};1H\x1b[2K{line}")
        sys.stdout.flush()
    return emit


# -------------------------------------------------------------------
# Q: exit (with unsaved-changes guard, story 006)
# -------------------------------------------------------------------

def _request_exit(state: State) -> State:
    """Q at the main screen: guard against quitting with unsaved
    in-memory changes. ESC follows the same path."""
    if state.unsaved_changes:
        state.prompt_mode = PromptMode.CONFIRM_DISCARD_UNSAVED
        state.dirty = True
        return state
    state.exit_requested = True
    return state


def _handle_discard_unsaved_confirm(state: State, key) -> State:
    if isinstance(key, str) and key.lower() == "y":
        state.exit_requested = True
        return state
    if key == Key.CTRL_C:
        state.exit_requested = True
        return state
    state.prompt_mode = PromptMode.OFF
    state.status_message = "exit cancelled"
    state.dirty = True
    return state


# -------------------------------------------------------------------
# Main entry point
# -------------------------------------------------------------------

def main(ahdi_driver_path=None, ppdriver_bootable: bool = False) -> int:
    state = State()
    # Story 012: pre-fill bootable-mode from CLI flags (the wrapper
    # in atari_hd.py parses --ahdi-driver / --ppdriver-bootable and
    # forwards them here when --tui is also requested).
    if ahdi_driver_path is not None:
        state.format_id = "AHDI"
        state.bootable = True
        state.ahdi_driver_path = ahdi_driver_path
        state.status_message = (
            f"AHDI bootable pre-filled from CLI: "
            f"{os.path.basename(ahdi_driver_path)}")
    elif ppdriver_bootable:
        state.format_id = "PPDRIVER"
        state.bootable = True
        state.status_message = (
            "PPDRIVER bootable pre-filled from CLI (bundled blob)")
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
