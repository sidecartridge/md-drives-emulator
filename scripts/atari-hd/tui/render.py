"""Pure rendering: state -> frame string with embedded ANSI escapes.

Each call returns a complete fresh frame; we don't diff. At our screen
sizes (80x24+) writing the whole screen on every dirty flag is not
measurable and the implementation stays simple.

Story 003 fills the body with a partition list (column header +
selectable rows + format hint), and morphs the keybinding row by
state (file actions before an image is loaded; partition actions
afterwards, with D / E / T / W dimmed when the list is empty).
"""

import shutil

from .state import PromptMode, Screen, State


# ANSI escape sequences shared across screens.
CLEAR_SCREEN = "\x1b[2J"
CURSOR_HOME = "\x1b[H"
INVERSE_ON = "\x1b[7m"
INVERSE_OFF = "\x1b[27m"
DIM_ON = "\x1b[2m"
DIM_OFF = "\x1b[22m"


# Hard floor on terminal size. Below either dimension the layout
# breaks; we surface "terminal too small" instead of trying to
# graceful-degrade.
MIN_COLS = 80
MIN_ROWS = 24


# Box-drawing characters. UTF-8 only -- DECISIONS.md accepts this on
# all three target terminals (Terminal.app / iTerm2 / Windows
# Terminal / modern Linux terminals).
HLINE = "─"   # ─


# Partition-list column widths, in order.
COL_SLOT = 4
COL_TYPE = 5
COL_START = 12
COL_SIZE = 10
COL_LABEL = 13
COL_GAP = 2
LIST_LEFT_MARGIN = 2


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
    # Layout (rows-row baseline; expands at the body for taller terminals):
    #   row 1:        header line
    #   row 2:        separator
    #   rows 3..N-3:  body (partition list area)
    #   row N-2:      separator
    #   row N-1:      key bindings
    #   row N:        prompt or transient status message
    body_rows = rows - 4    # header + 2 separators + status keys row
    body_rows -= 1          # plus the final prompt/status row

    parts = [CLEAR_SCREEN, CURSOR_HOME,
             _render_header(state, cols), "\r\n",
             _hline(cols), "\r\n",
             _render_body(state, cols, body_rows),
             _hline(cols), "\r\n",
             _render_status_keys(state, cols), "\r\n",
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
    gap = 2
    available_for_right = cols - len(left) - gap
    if available_for_right < len(right):
        right = _truncate_middle(right, max(available_for_right, 5))
    pad = cols - len(left) - len(right)
    if pad < 0:
        return (left + " " + right)[:cols]
    return left + (" " * pad) + right


def _render_body(state: State, cols: int, height: int) -> str:
    """Render the partition-list area with a format hint at the
    bottom. The body has three shapes:
       - no image: "press N or L" hint, no list, no format row
       - image set, no partitions: "press A to add" centered, format row
       - image set, partitions: column header + rows + format row
    """
    if state.image_path is None:
        return _render_body_no_image(cols, height)
    if not state.partitions:
        return _render_body_empty_partitions(state, cols, height)
    return _render_body_partitions(state, cols, height)


def _render_body_no_image(cols: int, height: int) -> str:
    msg = "No image selected. Press N to create one or L to load an existing image."
    return _centered_in_body(msg, cols, height)


def _render_body_empty_partitions(state: State, cols: int, height: int) -> str:
    """Image set but partition list empty: centered hint, format row at
    the bottom."""
    msg = "No partitions defined -- press A to add"
    msg = _truncate_middle(msg, cols - 2)
    blank = " " * cols
    fmt_line = _render_format_hint(state, cols)
    lines = []
    middle = (height - 1) // 2
    for i in range(height - 1):
        if i == middle:
            pad_left = max(0, (cols - len(msg)) // 2)
            line = (" " * pad_left) + msg
            lines.append(_pad_to(line, cols))
        else:
            lines.append(blank)
    lines.append(fmt_line)
    return "\r\n".join(lines) + "\r\n"


def _render_body_partitions(state: State, cols: int, height: int) -> str:
    """Image set and partitions present: column header + rows +
    format row. Selection is highlighted with reverse video.
    """
    blank = " " * cols
    list_rows_visible = height - 2  # header + format row
    # Adjust scroll_top to keep selected_slot visible.
    scroll = state.scroll_top
    if state.selected_slot < scroll:
        scroll = state.selected_slot
    elif state.selected_slot >= scroll + list_rows_visible:
        scroll = state.selected_slot - list_rows_visible + 1

    lines = [_render_partition_list_header(cols)]
    for i in range(list_rows_visible):
        idx = scroll + i
        if idx < len(state.partitions):
            row = _render_partition_row(state.partitions[idx], idx, cols,
                                        format_id=state.format_id,
                                        selected=(idx == state.selected_slot))
            lines.append(row)
        else:
            lines.append(blank)
    lines.append(_render_format_hint(state, cols))
    return "\r\n".join(lines) + "\r\n"


def _render_partition_list_header(cols: int) -> str:
    """Column header row, padded to `cols`."""
    parts = [
        " " * LIST_LEFT_MARGIN,
        "Slot".rjust(COL_SLOT), " " * COL_GAP,
        "Type".ljust(COL_TYPE), " " * COL_GAP,
        "Start (LBA)".rjust(COL_START), " " * COL_GAP,
        "Size".rjust(COL_SIZE), " " * COL_GAP,
        "Label".ljust(COL_LABEL),
    ]
    return _pad_to("".join(parts), cols)


def _render_partition_row(part, index: int, cols: int,
                          format_id: str, selected: bool) -> str:
    """One partition row. `part` duck-types atari_hd.Partition --
    requires .name, .size_mb, .start_lba, .size_sectors."""
    ident = _partition_ident(part, index, format_id)
    parts = [
        " " * LIST_LEFT_MARGIN,
        f"{index:>{COL_SLOT}}", " " * COL_GAP,
        ident.ljust(COL_TYPE), " " * COL_GAP,
        f"{part.start_lba:>{COL_START},}", " " * COL_GAP,
        _format_size(part.size_mb).rjust(COL_SIZE), " " * COL_GAP,
        _truncate_middle(part.name, COL_LABEL).ljust(COL_LABEL),
    ]
    body = "".join(parts)
    body = _pad_to(body, cols)
    if selected:
        return INVERSE_ON + body + INVERSE_OFF
    return body


def _partition_ident(part, index: int, format_id: str) -> str:
    """Best-effort short ident for the type column. Honors an explicit
    `ahdi_ident` attribute when story 004 starts setting one; otherwise
    derives a v1 value from the format and slot index.

    For AHDI: slot 0 is forced GEM (boot rule); later slots flip to BGM
    above the 32 MB threshold.
    For PPDRIVER / HDDRIVER: every partition is FAT16 in the MBR table
    (the EBR-chain "extended" type is the chain header, not a partition
    the user listed).
    """
    explicit = getattr(part, "ahdi_ident", None)
    if explicit is not None:
        return explicit if isinstance(explicit, str) else explicit.decode(
            "ascii", errors="replace")
    if format_id == "AHDI":
        if index == 0:
            return "GEM"
        return "GEM" if part.size_mb <= 32 else "BGM"
    return "FAT16"


def _format_size(mb: int) -> str:
    """MB up to 999.9; GB beyond. One decimal."""
    if mb < 1000:
        return f"{mb:.1f} MB"
    return f"{mb / 1024:.1f} GB"


def _render_format_hint(state: State, cols: int) -> str:
    fmt = state.format_id
    if state.format_id == "AHDI":
        tos = f"  TOS<1.04: {'on' if state.strict_tos else 'off'}"
    else:
        tos = ""
    line = f"  Format: {fmt}{tos}"
    return _pad_to(line, cols)


def _render_status_keys(state: State, cols: int) -> str:
    """Keybinding row. Morphs by state:
       - no image: file actions (N=New L=Load Q=Quit)
       - image set: partition actions (A/D/E/T/W/Q), with D/E/T/W
         dimmed when the list is empty.
    """
    if state.image_path is None:
        line = "N=New   L=Load   Q=Quit"
        return _pad_to(line, cols)
    has_partitions = bool(state.partitions)
    items = ["A=Add"]
    cond = ["D=Delete", "E=Edit", "T=Type", "W=Write"]
    for k in cond:
        items.append(k if has_partitions else f"{DIM_ON}{k}{DIM_OFF}")
    items.append("Q=Quit")
    line = "  ".join(items)
    # Dimming escapes don't take visible space; right-pad to cols by
    # measuring the *visible* width.
    visible_len = sum(len(k.replace(DIM_ON, "").replace(DIM_OFF, ""))
                      for k in items) + 2 * (len(items) - 1)
    pad = max(0, cols - visible_len)
    return line + (" " * pad)


def _render_status_message(state: State, cols: int) -> str:
    """Bottom row: prompt (when prompt_mode is active), transient
    status message, or blank."""
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


# -------------------------------------------------------------------
# Layout helpers
# -------------------------------------------------------------------

def _centered_in_body(msg: str, cols: int, height: int) -> str:
    msg = _truncate_middle(msg, cols - 2)
    blank = " " * cols
    middle = height // 2
    lines = []
    for i in range(height):
        if i == middle:
            pad_left = max(0, (cols - len(msg)) // 2)
            lines.append(_pad_to(" " * pad_left + msg, cols))
        else:
            lines.append(blank)
    return "\r\n".join(lines) + "\r\n"


def _pad_to(s: str, cols: int) -> str:
    """Right-pad `s` with spaces to exactly `cols` characters
    (visible). Caller is responsible for not embedding wider escapes
    in `s` -- this is a plain len()-based padder; functions that emit
    ANSI styling do their own padding."""
    if len(s) >= cols:
        return s[:cols]
    return s + " " * (cols - len(s))


def _truncate_middle(s: str, max_width: int) -> str:
    """Shorten `s` to fit within `max_width` using a "..." separator
    in the middle. For very short widths just hard-truncate."""
    if len(s) <= max_width:
        return s
    if max_width < 5:
        return s[:max_width]
    head = (max_width - 3) // 2
    tail = max_width - 3 - head
    return s[:head] + "..." + s[-tail:]
