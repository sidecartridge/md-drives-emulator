# TUI design decisions

This document records the load-bearing decisions made for the
`scripts/atari-hd/tui/` package. Update it (don't replace it) when a
later story changes course; keep the history readable.

## 1. Framework: plain ANSI escapes

**Chosen.** All TUI output is plain ANSI escape sequences written
directly to `stdout`; all input is raw bytes read from `stdin`. The
package does its own raw-mode entry/exit, key parsing, and screen
redraw.

**Discarded:**
- *Stdlib `curses`* — works on Linux / macOS but on Windows requires
  the third-party `windows-curses` package from PyPI. That breaks the
  zero-deps stance the rest of this project guards (no `pip install`
  anywhere). Acceptable only with an explicit Windows exception, which
  this project does not have.
- *Third-party (`prompt_toolkit`, `urwid`, `textual`)* — best UX, but
  hard no on the dependency front. Same reason.

**Tradeoff accepted:** more code (raw mode, key decoding, alt-screen
management, redraw on resize) in exchange for genuine zero-deps on all
three target OSes.

## 2. Cross-platform notes

- **Unix (Linux / macOS):** `termios` + `tty` enter raw mode;
  `select` peeks for pending input when disambiguating ESC vs.
  ESC-sequence; `os.read(stdin, 1)` for byte-level reads.
- **Windows 10+:** `kernel32.SetConsoleMode` toggles
  `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on stdout (so ANSI sequences
  render) and `ENABLE_VIRTUAL_TERMINAL_INPUT` on stdin (so the
  console emits ANSI escape sequences for arrows etc., matching
  Unix); `msvcrt.getch` reads bytes one at a time. Old `cmd.exe`
  pre-Windows-10 is **not supported** — Windows Terminal / modern
  PowerShell are the baseline.
- **Terminal restoration is mandatory.** All raw-mode and
  alt-screen state is wrapped in a `contextmanager` whose `finally`
  branch fires on `KeyboardInterrupt` and unhandled exceptions. A
  crash that leaves the user without echo / cursor / normal screen
  is the worst-case TUI bug; the context manager is the only line of
  defense.
- **Minimum terminal size: 80×24.** Enforced by story 002 (the main
  screen layout). Below that, the TUI shows "terminal too small" and
  refuses to proceed. v1 does not graceful-degrade.
- **Resize redraw — POSIX yes, Windows on next keypress.** Story 002
  installs a `SIGWINCH` handler on POSIX that surfaces a synthetic
  `Key.RESIZE` to the event loop, triggering an immediate redraw at
  the new size. Windows has no `SIGWINCH` equivalent and adding a
  polling thread would violate the "no threads / no async" guideline,
  so on Windows the layout updates on the next keypress instead.
  Acceptable v1 behavior; revisit if it bites.

## 3. State management: single `@dataclass`, no globals

All TUI state lives in one `State` dataclass (`state.py`). The event
loop owns the only instance; screens read from it and return a
mutated copy (or mutate in place — either is fine, but no globals).

**Rendering is a pure function** of state: `render(state) -> frame
string`. The event loop writes the frame to stdout on a dirty flag.
This is the simplest model that lets us test rendering without a
terminal: future stories can call `render(state)` from a unittest and
assert on the returned string.

## 4. Event loop

A single `while not state.exit_requested` loop in `app.py`:

```python
with terminal_session():
    while not state.exit_requested:
        if state.dirty:
            stdout.write(render(state))
            stdout.flush()
            state.dirty = False
        key = read_key()
        state = handle_key(state, key)
```

`read_key()` blocks until a single key (or known escape sequence) is
ready. `handle_key()` dispatches based on the current screen. No
threads, no async, no signal handlers — keep the model boring so
debugging on a misbehaving terminal is straightforward.

## 5. List navigation: hard-stop at bounds (no wrap)

**Chosen.** Up / Down arrows (and `j` / `k`) move the highlighted row
in the partition list, but selection does **not** wrap from the last
row to the first. At the bounds the keypress is silently ignored.

**Rationale:** partition lists are short (≤14 rows); wrapping is more
likely to surprise the user than to help them. Every list this epic
introduces (partition list in story 003, future format chooser in
005, etc.) follows this rule for consistency -- if a user learns
that pressing Up at row 0 does nothing, they don't have to relearn
the convention elsewhere in the TUI.

## Deferred to later stories

The following design questions are deliberately left open here; each
story records its decision in this document when it lands.

- **TTY detection + `--no-tui` fallback** — story 009.
- **Partition editing model** (modal dialog / wizard / inline edit)
  — story 004.
- **Commit step** (single Write action vs. auto-save) — story 006.
- **Validators for caps / GEM-first / etc.** — must stay as pure
  functions in `atari_hd.py`; the TUI never re-implements rules.
  Locked by epic-002 / story 004 tests.
