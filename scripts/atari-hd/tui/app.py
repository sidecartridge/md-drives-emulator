"""Event loop and key dispatch.

A single boring loop: read a key, mutate state, redraw on the dirty
flag, repeat until exit_requested. No threads, no async, no signal
handlers. See DECISIONS.md sections 3 and 4.
"""

import sys

from .input import Key, read_key
from .render import render
from .state import State
from .terminal import terminal_session


def handle_key(state: State, key) -> State:
    """Dispatch one keypress into a state mutation. Returns the same
    state object (mutated in place) for symmetry with future
    immutable-state refactors -- callers must not assume identity is
    preserved across calls."""
    if key in (Key.ESC, Key.CTRL_C):
        state.exit_requested = True
        return state
    if isinstance(key, str) and key.lower() == "q":
        state.exit_requested = True
        return state
    return state


def main() -> int:
    """Run the TUI. Returns a process exit code."""
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
