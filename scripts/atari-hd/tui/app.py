"""Event loop and key dispatch.

A single boring loop: read a key, mutate state, redraw on the dirty
flag, repeat until exit_requested. No threads, no async, no signal
handlers in this module (terminal.py owns SIGWINCH). See DECISIONS.md
sections 3 and 4.

Story 002 introduces inline-prompt sub-modes routed through their own
dispatchers; the main-screen handler dispatches N / L / Q.
"""

import os
import sys

from .input import Key, read_key
from .render import render
from .state import PromptMode, State
from .terminal import terminal_session


def handle_key(state: State, key) -> State:
    """Dispatch one keypress into a state mutation. Returns the same
    state (mutated in place) for symmetry with future immutable-state
    refactors."""
    # RESIZE always just triggers a redraw; nothing else.
    if key == Key.RESIZE:
        state.dirty = True
        return state

    if state.prompt_mode == PromptMode.OFF:
        return _handle_main(state, key)
    if state.prompt_mode in (PromptMode.ASK_NEW_PATH,
                             PromptMode.ASK_LOAD_PATH):
        return _handle_text_prompt(state, key)
    if state.prompt_mode == PromptMode.CONFIRM_OVERWRITE:
        return _handle_overwrite_confirm(state, key)
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

    # Navigation in the partition list. Hard-stop at top/bottom (no
    # wrap) -- DECISIONS.md records the choice for consistency across
    # every list in this epic.
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
    # partition operations. Partition actions are stubs in this story
    # -- story 004 (A/D/E/T) and story 006 (W) replace them with
    # real handlers.
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

    return _handle_partition_action_stub(state, k)


def _handle_navigation(state: State, key) -> State:
    """Move selected_slot up/down in the partition list. Hard-stop at
    bounds; ignored when the list is empty."""
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


# Stubs surfaced for the partition-action keys until the real handlers
# land (A/D/E/T in story 004, W in story 006). Status_message gives the
# user feedback so we don't leave the screen looking dead.
_STUB_MESSAGES = {
    "a": "story 004 implements Add",
    "d": "story 004 implements Delete",
    "e": "story 004 implements Edit",
    "t": "story 004 implements Type toggle",
    "w": "story 006 implements Write",
}


def _handle_partition_action_stub(state: State, k: str) -> State:
    if k not in _STUB_MESSAGES:
        return state
    # D / E / T / W require a non-empty list; A is always available
    # (until 14-cap, which story 004 enforces). Match the dim/enable
    # rule from the keybinding row.
    requires_partition = k in ("d", "e", "t", "w")
    if requires_partition and not state.partitions:
        return state
    state.status_message = _STUB_MESSAGES[k]
    state.dirty = True
    return state


# -------------------------------------------------------------------
# Text-prompt handlers (ASK_NEW_PATH / ASK_LOAD_PATH)
# -------------------------------------------------------------------

def _handle_text_prompt(state: State, key) -> State:
    if key == Key.ESC:
        # Cancel the prompt; clear buffer.
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
    path = state.prompt_buffer.strip()
    mode = state.prompt_mode
    if not path:
        # Empty input -> just cancel quietly.
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
            # Story 007 will actually parse the file; until then we
            # just remember the path and surface a placeholder note.
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
    # Spec: 'O' (capital) confirms; anything else cancels.
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
    # Anything else cancels -- including ESC, lowercase 'o',
    # printable chars, and Enter.
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
                sys.stdout.write(render(state))
                sys.stdout.flush()
                state.dirty = False
            key = read_key()
            state = handle_key(state, key)
    return 0
