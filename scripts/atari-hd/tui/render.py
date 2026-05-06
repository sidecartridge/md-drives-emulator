"""Pure rendering: state -> frame string with embedded ANSI escapes.

Each call returns a complete fresh frame; we don't diff. At our screen
sizes (80x24+) writing the whole screen on every dirty flag is not
measurable and the implementation stays simple.

Story 002 lands the three-region main screen (header / body /
status bar) plus the "terminal too small" fallback. Stories 003+
extend the body and the keybinding list.
"""

import shutil

from .state import PromptMode, Screen, State


# ANSI escape sequences shared across screens.
CLEAR_SCREEN = "\x1b[2J"
CURSOR_HOME = "\x1b[H"


# Hard floor on terminal size. Below either dimension the layout
# breaks; we surface "terminal too small" instead of trying to
# graceful-degrade.
MIN_COLS = 80
MIN_ROWS = 24


# Box-drawing characters. UTF-8 only -- DECISIONS.md accepts this on
# all three target terminals (Terminal.app / iTerm2 / Windows
# Terminal / modern Linux terminals).
HLINE = "─"   # ─


def render(state: State) -> str:
    """Return a complete frame string for the current screen, sized to
    the live terminal."""
    cols, rows = shutil.get_terminal_size((MIN_COLS, MIN_ROWS))
    if cols < MIN_COLS or rows < MIN_ROWS:
        return _render_too_small(cols, rows)
    if state.screen == Screen.MAIN:
        return _render_main(state, cols, rows)
    raise ValueError(f"unknown screen: {state.screen!r}")


def _render_too_small(cols: int, rows: int) -> str:
    msg1 = "Terminal too small."
    msg2 = f"Need at least {MIN_COLS}x{MIN_ROWS}; got {cols}x{rows}."
    msg3 = "Resize, or press q / Ctrl-C to quit."
    return CLEAR_SCREEN + CURSOR_HOME + "\r\n".join((msg1, msg2, "", msg3)) + "\r\n"


def _render_main(state: State, cols: int, rows: int) -> str:
    # Layout (24-row baseline; expands at the body for taller terminals):
    #   row 1:        header line
    #   row 2:        separator
    #   rows 3..N-3:  body (placeholder for now; partition list in 003)
    #   row N-2:      separator
    #   row N-1:      key bindings
    #   row N:        prompt or transient status message
    body_rows = rows - 4  # header + 2 separators + status keys row
    # Status occupies 2 rows (keys + prompt/message); we already
    # reserved one of those by counting "rows - 4" above. Reserve one
    # more by trimming body_rows.
    body_rows -= 1

    parts = [CLEAR_SCREEN, CURSOR_HOME,
             _render_header(state, cols), "\r\n",
             _hline(cols), "\r\n",
             _render_body(state, cols, body_rows),
             _hline(cols), "\r\n",
             _render_status_keys(cols), "\r\n",
             _render_status_message(state, cols)]
    return "".join(parts)


def _hline(cols: int) -> str:
    return HLINE * cols


def _render_header(state: State, cols: int) -> str:
    left = "atari-hd image creator"
    if state.image_path is None:
        right = "(no image)"
    else:
        right = f"image: {state.image_path}"
    # Left + right with a 2-space minimum gap; truncate the right
    # half (which is the path) when there isn't room.
    gap = 2
    available_for_right = cols - len(left) - gap
    if available_for_right < len(right):
        right = _truncate_middle(right, max(available_for_right, 5))
    pad = cols - len(left) - len(right)
    if pad < 0:
        # left too long for cols (shouldn't happen at 80+ cols, but be
        # defensive); just hard-truncate.
        return (left + " " + right)[:cols]
    return left + (" " * pad) + right


def _render_body(state: State, cols: int, height: int) -> str:
    """Render the partition-list area. Story 002 leaves it as a
    centered hint; story 003 fills it with the real list."""
    if state.image_path is None:
        msg = "No image selected. Press N to create one or L to load an existing image."
    else:
        msg = "(partition list -- story 003 fills this in)"
    msg = _truncate_middle(msg, cols - 2)

    lines = []
    # Show the message centered on the middle row of the body. All
    # other body rows are empty (full-width to overwrite anything from
    # a previous larger frame).
    middle = height // 2
    blank = " " * cols
    for i in range(height):
        if i == middle:
            pad_left = max(0, (cols - len(msg)) // 2)
            line = (" " * pad_left) + msg
            line += " " * max(0, cols - len(line))
            lines.append(line[:cols])
        else:
            lines.append(blank)
    return "\r\n".join(lines) + "\r\n"


def _render_status_keys(cols: int) -> str:
    keys = "N=New   L=Load   Q=Quit"
    if len(keys) > cols:
        return keys[:cols]
    return keys + (" " * (cols - len(keys)))


def _render_status_message(state: State, cols: int) -> str:
    """Bottom row: either a prompt (when prompt_mode is active) or a
    transient status message, or blank."""
    line = _format_prompt_or_message(state, cols)
    if len(line) > cols:
        line = line[:cols]
    return line + (" " * (cols - len(line)))


def _format_prompt_or_message(state: State, cols: int) -> str:
    if state.prompt_mode == PromptMode.ASK_NEW_PATH:
        return f"New image filename: {state.prompt_buffer}_"
    if state.prompt_mode == PromptMode.ASK_LOAD_PATH:
        return f"Load image filename: {state.prompt_buffer}_"
    if state.prompt_mode == PromptMode.CONFIRM_OVERWRITE:
        path = state.pending_path or "(unknown)"
        return (f"{path} exists. Press O to overwrite, "
                "anything else to cancel.")
    if state.status_message:
        return state.status_message
    return ""


def _truncate_middle(s: str, max_width: int) -> str:
    """Shorten `s` to fit within `max_width` using a "...".  separator
    in the middle. For very short widths just hard-truncate."""
    if len(s) <= max_width:
        return s
    if max_width < 5:
        return s[:max_width]
    head = (max_width - 3) // 2
    tail = max_width - 3 - head
    return s[:head] + "..." + s[-tail:]
