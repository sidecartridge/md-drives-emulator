"""Pure rendering: state -> frame string with embedded ANSI escapes.

Each public function in this module takes a State and returns a string
that, when written to stdout, produces a complete fresh frame. No
incremental diffing; we redraw the whole screen on every dirty flag.
At our screen sizes (80x24+) this is not measurable.

Story 001 only renders the PLACEHOLDER screen. Stories 002-008 add
the real screens; each gets its own _render_<name> private function
and is dispatched by render() based on state.screen.
"""

from .state import Screen, State


# ANSI escape sequences shared across screens.
CLEAR_SCREEN = "\x1b[2J"
CURSOR_HOME = "\x1b[H"


def render(state: State) -> str:
    """Return a complete frame string for the current screen."""
    if state.screen == Screen.PLACEHOLDER:
        return _render_placeholder(state)
    raise ValueError(f"unknown screen: {state.screen!r}")


def _render_placeholder(state: State) -> str:
    return (
        CLEAR_SCREEN
        + CURSOR_HOME
        + "SidecarTridge atari-hd image creator (TUI)\r\n"
        + "\r\n"
        + "  Skeleton -- epic-003 / story 001\r\n"
        + "\r\n"
        + "  Real screens land in stories 002-008.\r\n"
        + "\r\n"
        + "  Press q, Esc, or Ctrl-C to quit.\r\n"
    )
