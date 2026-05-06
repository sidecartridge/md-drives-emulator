# atari_hd test suite

Stdlib-only `unittest` suite for `scripts/atari-hd/atari_hd.py`. Runs on
macOS, Linux, and Windows with zero installs.

## Run

From the repo root:

```bash
python -m unittest discover scripts/atari-hd/tests -v
```

Exits 0 on green, non-zero on any failure.

## Layout

| File | Purpose |
|------|---------|
| `__init__.py` | Marks the directory as a Python package. |
| `_support.py` | `sys.path` bootstrap + helper stubs (`parse_bpb`, `parse_ahdi_root`). |
| `test_smoke.py` | Discovery / import sanity check. |
| `parity.py` | Developer-only byte-parity check vs. `mkfs.vfat` (landed by epic-001 / story 003). Not part of the `unittest` suite — runs as a standalone script when `dosfstools` is installed. |

Test modules import the script under test via:

```python
from _support import atari_hd
```

When `unittest discover` roots at `scripts/atari-hd/tests/`, that
directory is on `sys.path`, so `_support` resolves directly. The
bootstrap inside `_support.py` then adds `scripts/atari-hd/` to
`sys.path` so `import atari_hd` finds the script regardless of the
caller's cwd.

## Rules

- **Stdlib only.** No third-party imports anywhere under `tests/`.
- **No external binaries.** No `mkfs.vfat`, no `mount`, no `hdiutil`.
- **No fixtures in git.** Every artifact lives under
  `tempfile.TemporaryDirectory()`.
- **Tests stay tiny.** Total runtime budget under 30 s on a modern
  laptop.

## Design rationale

Lives in `scripts/atari-hd/epics/epic-002-test-suite/` (local working
tree only — `epics/` is gitignored). That folder holds the epic
overview, the coverage map, and the per-story plan; this README is
just the user-facing run instructions.
