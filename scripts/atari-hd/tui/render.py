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

import atari_hd

from .state import EditField, EditMode, PromptMode, Screen, State


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


# Partition-list column widths, in order. COL_TYPE = 6 fits "FAT16+"
# (the trailing "+" marks chain logicals; bare "FAT16" / "GEM" / "BGM"
# / "XGM" mean primary).
COL_SLOT = 4
COL_TYPE = 6
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
        frame = _render_main(state, cols, rows)
        if state.edit_dialog is not None:
            frame += _render_edit_dialog_overlay(state, cols, rows)
        # Help overlay sits on top of everything (story 008): drawn
        # last so it covers the edit dialog and the main body.
        if state.show_help:
            frame += _render_help_overlay(state, cols, rows)
        return frame
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
    # Epic-005 / story 004: copyright line lives next to the program
    # name on the left. The right-side image-path label gets the
    # _truncate_middle treatment first when the terminal is narrow,
    # keeping authorship readable on every screen.
    left = f"atari-hd image creator  {COPYRIGHT_LINE}"
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
    # Mention U=Autopart only when the auto path is actually enabled
    # (state.image_mb is set; otherwise pressing U just prints a
    # "set an image size first" status message).
    if state.image_mb is not None:
        msg = "No partitions defined -- press A to add or U to autopartition"
    else:
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


def _effective_kind(state: State, d) -> str:
    """Resolve the partition kind ("primary" / "extended") that the
    open dialog will commit. Slot 0 is always primary; HDDRIVER slot
    >= 1 is always extended; PPDRIVER slot >= 1 honors the user's
    d.kind_choice. AHDI partitions fall through to "primary" (the AHDI
    side derives primary/extended from N at write time)."""
    if d.slot == 0:
        return "primary"
    if state.format_id == "HDDRIVER":
        return "extended"
    if state.format_id == "PPDRIVER":
        return d.kind_choice
    return "primary"


def _kind_cap_mb(state: State, d, primary_count: int) -> int:
    """Cap (MB) for the partition the dialog is composing, derived from
    the effective kind (primary -> HYBRID_PRIMARY_MAX_MB; extended ->
    HYBRID_MAX_PARTITION_MB) on hybrid formats. AHDI defers to
    cap_mb_for_type's ident-based logic, with one extra rule: when
    AHDI bootable mode is on, slot 0 is capped at
    AHDI_BOOTABLE_BOOT_MAX_MB (15 MB) -- ICD's continuation IPL
    fails to recognize disks with a larger boot partition. That
    cap mirrors the validation in plan_image; surfacing it in the
    dialog lets the user see the constraint upfront instead of at
    write-time as a status-bar error."""
    is_hybrid = state.format_id in ("PPDRIVER", "HDDRIVER")
    if is_hybrid:
        kind = _effective_kind(state, d)
        if kind == "primary":
            return atari_hd.HYBRID_PRIMARY_MAX_MB
        return atari_hd.HYBRID_MAX_PARTITION_MB
    base = atari_hd.cap_mb_for_type(state.format_id, state.strict_tos,
                                     d.type_choice, slot_index=d.slot,
                                     primary_count=primary_count)
    if (state.format_id == "AHDI" and state.bootable
            and d.slot == 0):
        return min(base, atari_hd.AHDI_BOOTABLE_BOOT_MAX_MB)
    return base


def _dialog_primary_count(state: State, d) -> int:
    """primary_count for partition_layout from the perspective of the
    open edit dialog. ADD adds a slot to the count (the partition the
    user is about to commit); EDIT leaves it unchanged. Used by both
    the dialog's live cap label and validate_edit_dialog so the
    primary-vs-logical decision matches what plan_image will produce
    on save."""
    n = sum(1 for p in state.partitions if p is not None)
    if d.mode == EditMode.ADD:
        n += 1
    if n < 1:
        n = 1
    if n > 14:
        n = 14
    return atari_hd.partition_layout(state.format_id, n)["primary_count"]


def _display_start_lbas(state: State) -> list:
    """Return [start_lba_i] for each slot in `state.partitions` for the
    "Start (LBA)" column.

    Two cases:

    1. `Partition.start_lba` is already populated -- e.g. the plan
       came from `load_image()` so each partition carries its real
       on-disk LBA. Use it verbatim. This is the only way to display
       the correct data-start LBA for chain logicals: the cumulative
       sum below would land on the EBR sector itself (start_of_data
       minus 1) because it doesn't account for EBR / XGM chain
       overhead.

    2. `start_lba == 0` -- the partition was added in the dialog and
       hasn't been planned yet. Fall back to a sequential approximation
       so the list reflects something useful while composing. Still
       approximate (no chain overhead), but plan_image rewrites
       start_lba on write, so the discrepancy resolves itself.

    Slots that are None (a hole left by Delete) get 0 and don't
    advance the cursor, matching plan_image which skips them entirely.
    """
    if not state.partitions:
        return []
    next_lba = atari_hd.first_partition_start_lba(state.format_id)
    out = []
    for part in state.partitions:
        if part is None:
            out.append(0)
            continue
        if part.start_lba > 0:
            out.append(part.start_lba)
            next_lba = part.start_lba + (part.size_sectors
                                          or (part.size_mb * 1024 * 1024) // 512)
            continue
        out.append(next_lba)
        size_sectors = (part.size_mb * 1024 * 1024) // 512
        next_lba += size_sectors
    return out


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

    display_lbas = _display_start_lbas(state)
    lines = [_render_partition_list_header(cols)]
    for i in range(list_rows_visible):
        idx = scroll + i
        if idx < len(state.partitions):
            lba = display_lbas[idx] if idx < len(display_lbas) else 0
            row = _render_partition_row(state.partitions[idx], idx, cols,
                                        format_id=state.format_id,
                                        selected=(idx == state.selected_slot),
                                        start_lba_override=lba)
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
                          format_id: str, selected: bool,
                          start_lba_override=None) -> str:
    """One partition row. `part` may be None (a hole left by Delete);
    set partitions duck-type atari_hd.Partition (.name, .size_mb,
    .start_lba, .size_sectors).

    `start_lba_override` lets the caller display a sequentially-derived
    LBA (computed by `_display_start_lbas`) instead of `part.start_lba`,
    which is only populated by `plan_image()` at write time.
    """
    if part is None:
        parts = [
            " " * LIST_LEFT_MARGIN,
            f"{index:>{COL_SLOT}}", " " * COL_GAP,
            "(empty)".ljust(COL_TYPE + COL_GAP + COL_START
                            + COL_GAP + COL_SIZE + COL_GAP + COL_LABEL),
        ]
        body = "".join(parts)
        body = _pad_to(body, cols)
        if selected:
            return INVERSE_ON + body + INVERSE_OFF
        return body
    ident = _partition_ident(part, index, format_id)
    lba = part.start_lba if start_lba_override is None else start_lba_override
    parts = [
        " " * LIST_LEFT_MARGIN,
        f"{index:>{COL_SLOT}}", " " * COL_GAP,
        ident.ljust(COL_TYPE), " " * COL_GAP,
        f"{lba:>{COL_START},}", " " * COL_GAP,
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
    `ahdi_ident` attribute when set; otherwise derives the value the
    writer will emit by mirroring atari_hd.ahdi_partition_id() (ident
    follows bps strictly: GEM iff bps=512, i.e. size <= 31 MB; BGM
    otherwise).

    For PPDRIVER / HDDRIVER: every partition is FAT16 in the MBR table.

    A trailing "+" marks chain logicals -- partitions whose
    is_extended is True (the user-side source of truth). ebr_lba is
    the on-disk projection and is 0 right after load_image's
    auto-migration flips a primary's is_extended to True; keying on
    is_extended keeps the display honest in that intermediate state.
    Bare idents mean the partition occupies a primary slot.
    """
    explicit = getattr(part, "ahdi_ident", None)
    if explicit is not None:
        ident = explicit if isinstance(explicit, str) else explicit.decode(
            "ascii", errors="replace")
    elif format_id == "AHDI":
        ident = "GEM" if part.size_mb <= atari_hd.AHDI_GEM_MAX_MB else "BGM"
    else:
        ident = "FAT16"
    if getattr(part, "is_extended", False) or getattr(part, "ebr_lba", 0):
        ident += "+"
    return ident


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
    boot = _bootable_indicator(state)
    if state.image_mb is not None:
        size = f"  Size: {state.image_mb} MB"
    else:
        size = ""
    line = f"  Format: {fmt}{tos}{size}{boot}"
    return _pad_to(line, cols)


def _bootable_indicator(state) -> str:
    """One-segment string ("  Bootable: …") describing the current
    bootable-mode state per format. HDDRIVER prints a docs pointer
    instead of yes/no since it's not self-bootable from this tool."""
    if state.format_id == "HDDRIVER":
        return "  Bootable: manual (HDDRUTIL.APP)"
    if state.format_id == "AHDI":
        if state.bootable and state.ahdi_driver_path:
            import os as _os
            base = _os.path.basename(state.ahdi_driver_path)
            return f"  Bootable: yes ({base})"
        return "  Bootable: no (B to enable)"
    if state.format_id == "PPDRIVER":
        return ("  Bootable: yes (bundled blob)" if state.bootable
                else "  Bootable: no (B to enable)")
    return ""


def _has_real_partitions(state: State) -> bool:
    """True iff state.partitions has at least one non-None entry.
    Holes (None) left by Delete don't count toward the dim policy."""
    return any(p is not None for p in state.partitions)


def _selected_is_real(state: State) -> bool:
    """True iff selected_slot points at a real (non-None) partition.
    D / E require this; T also (since type only applies to AHDI
    partitions on a real slot)."""
    if not state.partitions:
        return False
    if not 0 <= state.selected_slot < len(state.partitions):
        return False
    return state.partitions[state.selected_slot] is not None


def _render_status_keys(state: State, cols: int) -> str:
    """Keybinding row. Morphs by state:
       - no image: file actions (N=New L=Load Q=Quit)
       - image set: partition actions (A/D/E/T/W/F/Q), with D/E/T
         dimmed when the selected slot is empty (no real partition
         to act on); W dimmed when there are zero real partitions.
         F (format selector) is always enabled when an image is set
         so the user can pick the format before adding partitions.
    """
    if state.image_path is None:
        # ? is universal but easy to miss; advertise it on the
        # landing row where there's still space. The partition-list
        # row is at the 80-col budget and stays as-is.
        line = "N=New   L=Load   Q=Quit   ?=Help"
        return _pad_to(line, cols)
    # File actions stay visible even after an image is loaded so the
    # user can create a fresh plan or load a different file mid-session
    # without exiting first.
    items = ["N=New", "L=Load", "A=Add"]
    selected_real = _selected_is_real(state)
    has_real = _has_real_partitions(state)
    # Story 012: B is enabled for AHDI and PPDRIVER (story 005/011's
    # CLI flags); HDDRIVER dims B because the tool can't produce
    # self-bootable HDDRIVER images (story 006 docs-only).
    boot_enabled = state.format_id in ("AHDI", "PPDRIVER")
    # Epic-005 story 002: U=Auto enabled once the New-flow set an
    # image size (state.image_mb). Always-dim before then so the
    # user can't trigger auto-fill on a half-baked plan.
    auto_enabled = state.image_mb is not None
    cond = [
        ("D=Delete", selected_real),
        ("E=Edit",   selected_real),
        ("T=Type",   selected_real),
        ("W=Write",  has_real),
        ("B=Boot",   boot_enabled),
        ("U=Autopart", auto_enabled),
    ]
    for label, enabled in cond:
        items.append(label if enabled else f"{DIM_ON}{label}{DIM_OFF}")
    items.append("F=Format")
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
    if state.prompt_mode == PromptMode.ASK_AHDI_DRIVER_PATH:
        return (f"AHDI driver path (e.g. ICDBOOT.PRG): "
                f"{state.prompt_buffer}_")
    if state.prompt_mode == PromptMode.ASK_IMAGE_SIZE:
        return ("Image size:  1)16  2)64  3)128  4)256  5)512  "
                "6)1024  7)2048  8)4096   c)Custom   (Esc cancel)")
    if state.prompt_mode == PromptMode.ASK_IMAGE_SIZE_CUSTOM:
        return (f"Custom size (16-8192 MB): "
                f"{state.prompt_buffer}_  (Esc back to presets)")
    if state.prompt_mode == PromptMode.CONFIRM_AUTO_DISCARD:
        n = sum(1 for p in state.partitions if p is not None)
        return f"Discard {n} existing partition(s) and auto-fill? (y/N)"
    if state.prompt_mode == PromptMode.ASK_AUTO_MODE:
        return ("Auto-partition mode:  [D]efault (few large)   "
                "[M]ax (many equal)   (Esc cancel)")
    if state.prompt_mode == PromptMode.ASK_AUTO_N:
        # Compute the natural default and show it as the [hint].
        # Avoid importing app.py from render.py; reproduce the small
        # natural-N math inline.
        try:
            from . import app as _app  # late import to dodge cycles
            natural = _app._auto_natural_n(state)
        except Exception:
            natural = 1
        return (f"Number of partitions [default {natural}]: "
                f"{state.prompt_buffer}_  (Esc cancel)")
    if state.prompt_mode == PromptMode.CONFIRM_OVERWRITE:
        path = state.pending_path or "(unknown)"
        return (f"{path} exists. Press O to overwrite, "
                "anything else to cancel.")
    if state.prompt_mode == PromptMode.CONFIRM_DELETE:
        slot = state.pending_delete_slot
        return f"Delete partition #{slot}? (y/N)"
    if state.prompt_mode == PromptMode.ASK_FORMAT:
        return ("Format: [A]HDI  [P]PDRIVER  [H]DDRIVER (experimental)  "
                "(Esc cancel)")
    if state.prompt_mode == PromptMode.ASK_STRICT_TOS:
        return "Compatibility with TOS < 1.04? (y/N)"
    if state.prompt_mode == PromptMode.CONFIRM_DROP_PARTITIONS:
        n = len(state.pending_drop_slots or [])
        plural = "" if n == 1 else "s"
        verb = "violates" if n == 1 else "violate"
        return (f"{n} partition{plural} {verb} the new format's "
                "rules. Discard? (y/N)")
    if state.prompt_mode == PromptMode.CONFIRM_OVERWRITE_WRITE:
        return f"Overwrite {state.image_path}? (y/N)"
    if state.prompt_mode == PromptMode.CONFIRM_DISCARD_UNSAVED:
        return "Discard unsaved changes? (y/N)"
    if state.prompt_mode == PromptMode.CONFIRM_DISCARD_BEFORE_LOAD:
        return "Discard unsaved changes and load? (y/N)"
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


# -------------------------------------------------------------------
# Edit dialog overlay (story 004)
# -------------------------------------------------------------------

DIALOG_WIDTH = 56
DIALOG_HEIGHT = 14


def _render_edit_dialog_overlay(state: State, cols: int, rows: int) -> str:
    """Centered modal dialog drawn on top of the partition list.
    Uses absolute cursor positioning so we don't have to redraw the
    background; the caller composes this after the main frame."""
    d = state.edit_dialog
    box_top = max(1, (rows - DIALOG_HEIGHT) // 2 + 1)
    box_left = max(1, (cols - DIALOG_WIDTH) // 2 + 1)

    title = "Add Partition" if d.mode == EditMode.ADD else f"Edit Partition #{d.slot}"
    primary_count = _dialog_primary_count(state, d)
    is_hybrid = state.format_id in ("PPDRIVER", "HDDRIVER")

    # Kind row (hybrid only): primary vs extended. Slot 0 is locked
    # primary; HDDRIVER slot >= 1 is locked extended; PPDRIVER slot >= 1
    # is the user's free choice.
    kind_visible = is_hybrid
    kind_locked = is_hybrid and (d.slot == 0
                                  or state.format_id == "HDDRIVER")
    kind_value = ""
    if kind_visible:
        if kind_locked:
            forced = ("primary" if d.slot == 0 else "extended")
            kind_value = f"{forced} (locked)"
        else:
            pri = "[Primary]" if d.kind_choice == "primary" else " Primary "
            ext = "[Extended]" if d.kind_choice == "extended" else " Extended "
            kind_value = f"{pri}  {ext}"
        if d.field == EditField.KIND and not kind_locked:
            kind_value = INVERSE_ON + kind_value + INVERSE_OFF

    cap_mb = _kind_cap_mb(state, d, primary_count)
    min_mb = atari_hd.format_min_partition_mb(state.format_id)
    if min_mb > 1:
        # Hybrid formats have a hard 32 MB floor; surface the range
        # so users don't type a 16 MB hybrid partition and hit the
        # build-time min-size rejection later.
        size_label = f"Size ({min_mb}-{cap_mb} MB):"
    else:
        size_label = f"Size (<= {cap_mb} MB):"
    size_value = d.size_buffer if d.size_buffer else "_"
    if d.field == EditField.SIZE:
        size_value = INVERSE_ON + size_value.ljust(8) + INVERSE_OFF
    else:
        size_value = size_value.ljust(8)

    type_locked = (state.format_id == "AHDI" and d.slot == 0)
    type_visible = state.format_id == "AHDI"
    if type_visible:
        if type_locked:
            type_value = f"GEM (locked)"
        else:
            gem = "[GEM]" if d.type_choice == "GEM" else " GEM "
            bgm = "[BGM]" if d.type_choice == "BGM" else " BGM "
            type_value = f"{gem}  {bgm}"
        if d.field == EditField.TYPE and not type_locked:
            type_value = INVERSE_ON + type_value + INVERSE_OFF

    label_value = d.label_buffer if d.label_buffer else "_"
    label_value = label_value.ljust(11)
    if d.field == EditField.LABEL:
        label_value = INVERSE_ON + label_value + INVERSE_OFF

    error = validate_edit_dialog(state)
    status_line = "OK" if error is None else error

    # Build the lines that go inside the box. The width budget is
    # DIALOG_WIDTH - 4 (border + 1 padding each side).
    inner = DIALOG_WIDTH - 4
    body_lines = [
        title.ljust(inner),
        "",
    ]
    if kind_visible:
        body_lines.append(f"  {'Kind:'.ljust(20)} {kind_value}")
    body_lines.append(f"  {size_label.ljust(20)} {size_value}")
    if type_visible:
        body_lines.append(f"  {'Type:'.ljust(20)} {type_value}")
    body_lines.append(f"  {'Label:'.ljust(20)} {label_value}")
    body_lines.append("")
    body_lines.append(_truncate_middle(f"Status: {status_line}", inner))
    body_lines.append("")
    body_lines.append(_truncate_middle(
        "[Tab] field  [t] cycle  [Enter] save  [Esc] cancel", inner))

    # Pad body to DIALOG_HEIGHT - 2 (top + bottom border rows).
    while len(body_lines) < DIALOG_HEIGHT - 2:
        body_lines.append("")

    # Compose the framed box with absolute positioning.
    out = []
    # Top border
    out.append(_cursor_to(box_top, box_left) + "┌" + "─" * (DIALOG_WIDTH - 2) + "┐")
    # Body
    for i, body in enumerate(body_lines):
        # Strip ANSI for length measurement
        visible = _strip_ansi(body)
        pad = max(0, (DIALOG_WIDTH - 2) - len(visible))
        line = "│ " + body + (" " * (pad - 1 if pad >= 1 else 0)) + "│"
        out.append(_cursor_to(box_top + 1 + i, box_left) + line)
    # Bottom border
    out.append(_cursor_to(box_top + DIALOG_HEIGHT - 1, box_left)
               + "└" + "─" * (DIALOG_WIDTH - 2) + "┘")
    return "".join(out)


def _cursor_to(row: int, col: int) -> str:
    return f"\x1b[{row};{col}H"


def _strip_ansi(s: str) -> str:
    """Remove ANSI escape sequences for visible-length measurement.
    Cheap and good-enough for our usage; we only emit \\x1b[<digits>m
    style sequences from this module."""
    out = []
    i = 0
    while i < len(s):
        if s[i] == "\x1b" and i + 1 < len(s) and s[i + 1] == "[":
            # Skip until letter
            j = i + 2
            while j < len(s) and not s[j].isalpha():
                j += 1
            i = j + 1
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


# -------------------------------------------------------------------
# Validation (story 004)
# -------------------------------------------------------------------

LABEL_FORBIDDEN_CHARS = set('*?<>|":+,./;=[]\\\x7f')


def is_legal_label_char(ch: str) -> bool:
    """True if `ch` can be entered into a FAT16 volume label.
    Mirrors the filter atari_hd._fat16_normalize_label() applies on
    commit; doing it at input time avoids accumulating illegal chars
    that would only fail validation later."""
    if len(ch) != 1:
        return False
    if not (32 <= ord(ch) < 127):
        return False
    return ch.upper() not in LABEL_FORBIDDEN_CHARS


def validate_edit_dialog(state: State):
    """Return None when the dialog state passes every rule, otherwise
    a short message naming the *first* failure. The Save action is
    enabled iff this returns None."""
    d = state.edit_dialog
    if d is None:
        return None

    # Size must be a positive integer.
    if not d.size_buffer:
        return "size required"
    try:
        size_mb = int(d.size_buffer)
    except ValueError:
        return "size must be a number"
    if size_mb < 1:
        return "size must be >= 1 MB"

    # Cap depends on:
    #  - For AHDI: ident (GEM cap vs BGM cap) and strict_tos.
    #  - For hybrid: the resolved Kind (primary -> 255 MB; extended
    #    -> 511 MB), ignoring d.type_choice (AHDI ident doesn't apply
    #    to hybrid). The kind comes from _effective_kind which honours
    #    slot 0 / HDDRIVER locks and the user's d.kind_choice on
    #    PPDRIVER slot >= 1.
    primary_count = _dialog_primary_count(state, d)
    cap = _kind_cap_mb(state, d, primary_count)
    min_mb = atari_hd.format_min_partition_mb(state.format_id)
    if size_mb < min_mb:
        return (f"size below {min_mb} MB minimum for "
                f"{state.format_id} hybrid layout")
    # AHDI ident strictly follows bps: GEM iff bps=512, BGM iff bps>512.
    # bps doubles past 31 MB (clusters_at_spc=2 > 32765), so a user
    # picking BGM with size <= 31 MB would write ident=BGM but bps=512
    # in the BPB -- the malformed combination flagged by AHDI 3.0.
    # Reject explicitly so the user gets a clear message instead of a
    # surprise ident swap at write time.
    if (state.format_id == "AHDI" and d.type_choice == "BGM"
            and size_mb <= atari_hd.AHDI_GEM_MAX_MB):
        return (f"BGM requires size > {atari_hd.AHDI_GEM_MAX_MB} MB "
                "(smaller partitions are GEM); pick GEM or grow the size")
    if size_mb > cap:
        # AHDI bootable mode pins slot 0 to a tighter cap (15 MB)
        # because ICD's continuation IPL can't recognize larger boot
        # partitions. Surface the constraint explicitly so the user
        # knows it's the bootable toggle (B), not the format cap.
        if (state.format_id == "AHDI" and state.bootable
                and d.slot == 0):
            return (f"AHDI bootable boot partition cap is "
                    f"{atari_hd.AHDI_BOOTABLE_BOOT_MAX_MB} MB "
                    f"(ICD IPL constraint; press B to disable bootable mode)")
        if state.format_id == "AHDI" and d.type_choice == "GEM":
            kind = f"GEM under {'TOS<1.04' if state.strict_tos else 'TOS 1.04+'}"
        elif state.format_id == "AHDI":
            kind = "BGM"
        elif _effective_kind(state, d) == "primary":
            kind = f"{state.format_id} primary (bps must stay <= 4096)"
        else:
            kind = f"{state.format_id} extended"
        return f"size exceeds {cap} MB cap for {kind}"

    # Hybrid layout rules -- surface here so the user sees the error
    # while composing instead of at W. plan_image enforces the same
    # rules at write time as a backstop.
    if state.format_id in ("PPDRIVER", "HDDRIVER"):
        kind = _effective_kind(state, d)
        # Slot 0 must be primary -- already locked in the dialog, but
        # check defensively.
        if d.slot == 0 and kind != "primary":
            return "first partition must be a primary"
        # No primary may follow an extended partition.
        if kind == "primary":
            for i, prior in enumerate(state.partitions):
                if i >= d.slot:
                    break
                if prior is None:
                    continue
                if prior.is_extended:
                    return (f"slot {i} is extended; primaries must "
                            "come before any extended partitions")
        # Total primary count after this commit must not exceed the
        # format's max.
        if kind == "primary":
            max_p = atari_hd.MAX_PRIMARY_PARTITIONS[state.format_id]
            n = max(len(state.partitions), d.slot + 1)
            primaries = 0
            for i in range(n):
                if i == d.slot:
                    primaries += 1
                    continue
                p = (state.partitions[i]
                      if i < len(state.partitions) else None)
                if p is not None and not p.is_extended:
                    primaries += 1
            if primaries > max_p:
                return (f"{state.format_id} allows at most {max_p} "
                        f"primary partition(s); make this one extended")

    # Label validation -- empty is OK (we'll default to a generated
    # name on commit), otherwise must be uppercase ASCII per the
    # FAT16 short-name rules. is_legal_label_char already filters
    # at input time so this check should normally pass.
    if d.label_buffer and len(d.label_buffer) > 11:
        return "label too long (max 11 chars)"
    for ch in d.label_buffer:
        if not is_legal_label_char(ch):
            return f"label has illegal character: {ch!r}"

    return None


# -------------------------------------------------------------------
# Help overlay (story 008)
# -------------------------------------------------------------------

# Single source of truth for keybindings. Entries are
# (group, key, description) tuples; the overlay walks them in order
# and emits a group header row whenever the group changes. Tests and
# any future docs should reference this list rather than maintain a
# parallel copy.
#
# Sized to fit 80x24: 17 entries + 4 group headers + 2 borders = 23
# rows, leaving 1 row of breathing space.
# Authorship line shown in the header row beside the program name and
# at the bottom of the help overlay (epic-005 / story 004). Single
# source of truth so future updates are one edit.
COPYRIGHT_LINE = "(C) 2026 - GOODDATA LABS SL"


HELP_ENTRIES = [
    ("Files / Quit", "N",                "New image"),
    ("Files / Quit", "L",                "Load image"),
    ("Files / Quit", "Q",                "Quit (warns if unsaved)"),
    ("Partitions",   "Up / Dn / k / j",  "Move selection"),
    ("Partitions",   "A",                "Add partition"),
    ("Partitions",   "D",                "Delete selected"),
    ("Partitions",   "E",                "Edit selected"),
    ("Partitions",   "T",                "Toggle GEM/BGM (AHDI)"),
    ("Partitions",   "F",                "Change format"),
    ("Partitions",   "W",                "Write image"),
    ("Partitions",   "B",                "Toggle bootable mode (AHDI/PPDRIVER)"),
    ("Partitions",   "U",                "Auto-fill partitions (default / max)"),
    ("Dialogs",      "Tab",              "Next field"),
    ("Dialogs",      "t / Left / Right", "Cycle Type"),
    ("Dialogs",      "Enter / S",        "Save"),
    ("Dialogs",      "A / P / H",        "Pick format (in selector)"),
    ("Dialogs",      "y / N / O",        "Confirm prompts (O = overwrite)"),
    ("Dialogs",      "Esc",              "Cancel dialog / prompt"),
    ("Anywhere",     "?",                "Toggle this help"),
]

HELP_BOX_WIDTH = 60
HELP_KEY_COL = 18  # left-padded width of the key column inside rows


def _render_help_overlay(state: State, cols: int, rows: int) -> str:
    """Centered modal help overlay. Walks HELP_ENTRIES and emits a
    group header on each transition; rows are key + description in
    two aligned columns (per the story spec). ASCII content only --
    box-drawing borders are the only non-ASCII pieces."""
    body_lines = []
    last_group = None
    for group, key, desc in HELP_ENTRIES:
        if group != last_group:
            body_lines.append(group)
            last_group = group
        # Two columns: "  KEY<padded>  DESC". Indent group entries by
        # 2 chars so the header (no indent) reads as a section break.
        row = f"  {key.ljust(HELP_KEY_COL)}{desc}"
        body_lines.append(row)

    inner = HELP_BOX_WIDTH - 2  # subtract the side borders
    # Epic-005 / story 004: copyright line at the bottom of the
    # overlay, separated from the entries by a blank row, centered
    # within the inner box width.
    body_lines.append("")
    body_lines.append(COPYRIGHT_LINE.center(inner))
    height = len(body_lines) + 2  # +2 for top/bottom borders
    box_top = max(1, (rows - height) // 2 + 1)
    box_left = max(1, (cols - HELP_BOX_WIDTH) // 2 + 1)

    title = " atari-hd help (Esc / ? to close) "
    title_pad = max(0, inner - len(title))
    left_pad = title_pad // 2
    right_pad = title_pad - left_pad
    top_border = "┌" + ("─" * left_pad) + title + ("─" * right_pad) + "┐"
    bottom_border = "└" + ("─" * inner) + "┘"

    out = [_cursor_to(box_top, box_left) + top_border]
    for i, body in enumerate(body_lines):
        if len(body) > inner:
            body = body[:inner]
        line = "│" + body + (" " * (inner - len(body))) + "│"
        out.append(_cursor_to(box_top + 1 + i, box_left) + line)
    out.append(_cursor_to(box_top + height - 1, box_left) + bottom_border)
    return "".join(out)
