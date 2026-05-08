#!/usr/bin/env sh
# atari-hd one-line installer (macOS / Linux).
#
# Pulls the current `main` snapshot of scripts/atari-hd/ straight
# from this repository -- no GitHub releases, no signed tarballs,
# no version pinning beyond what's currently committed to main.
# The trust boundary is HTTPS to github.com.
#
# Usage:
#     curl -fsSL https://raw.githubusercontent.com/sidecartridge/md-drives-emulator/main/scripts/atari-hd/install.sh | sh
#
# Optional environment / flags:
#     ATARI_HD_REF=branch_or_tag    (or --ref=branch_or_tag)
#         Install from a specific git ref instead of main.
#     ATARI_HD_PREFIX=/opt          (or --prefix=/opt)
#         Install root. Defaults to ~/.local. Tarball lands under
#         $prefix/share/atari-hd/, shim at $prefix/bin/atari-hd.
#
# The script:
#   1. Downloads the repo's tarball at the chosen ref.
#   2. Extracts only scripts/atari-hd/ into the install dir.
#   3. Writes a shim at $prefix/bin/atari-hd that exec's atari_hd.py.
#   4. Tells the user how to add $prefix/bin to PATH if it isn't.
#
# Idempotent. No sudo for the default prefix. Re-running fetches
# whatever's currently on main and replaces the install in place.

set -eu

REPO="sidecartridge/md-drives-emulator"
DEFAULT_PREFIX="${HOME}/.local"

REF="${ATARI_HD_REF:-main}"
PREFIX="${ATARI_HD_PREFIX:-$DEFAULT_PREFIX}"

# Parse argv flags (overrides env).
while [ $# -gt 0 ]; do
    case "$1" in
        --ref=*)    REF="${1#*=}" ;;
        --ref)      REF="${2:?}"; shift ;;
        --prefix=*) PREFIX="${1#*=}" ;;
        --prefix)   PREFIX="${2:?}"; shift ;;
        -h|--help)
            sed -n '2,/^set -eu/p' "$0" | sed 's/^# \{0,1\}//; /^set -eu/d'
            exit 0
            ;;
        *)
            echo "atari-hd installer: unknown argument $1" >&2
            echo "  Try --help for usage." >&2
            exit 2
            ;;
    esac
    shift
done

INSTALL_DIR="$PREFIX/share/atari-hd"
BIN_DIR="$PREFIX/bin"
SHIM_PATH="$BIN_DIR/atari-hd"
TARBALL_URL="https://codeload.github.com/$REPO/tar.gz/$REF"
# Codeload's bare-ref endpoint auto-resolves $REF as a branch, a tag,
# or a commit SHA. Don't prefix refs/heads/ here -- that would lock
# the URL to branches and break --ref=v0.1.0 / --ref=<sha>.

say() { printf '%s\n' "$*"; }
warn() { printf '%s\n' "$*" >&2; }

require() {
    if ! command -v "$1" >/dev/null 2>&1; then
        warn "atari-hd installer: required command '$1' not found on PATH."
        warn "  Install it via your package manager and re-run."
        exit 127
    fi
}

# Pick a downloader (curl preferred; wget as fallback).
if command -v curl >/dev/null 2>&1; then
    DL_CMD="curl -fsSL --retry 3 -o"
elif command -v wget >/dev/null 2>&1; then
    DL_CMD="wget -q -O"
else
    warn "atari-hd installer: need curl or wget on PATH; neither found."
    exit 127
fi
require tar

# Workspace.
TMPDIR=$(mktemp -d "${TMPDIR:-/tmp}/atari-hd-install.XXXXXX")
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

say "atari-hd installer"
say "  source : github.com/$REPO @ $REF"
say "  prefix : $PREFIX"

say "Downloading $REF tarball ..."
$DL_CMD "$TMPDIR/repo.tar.gz" "$TARBALL_URL"

say "Extracting scripts/atari-hd/ ..."
tar -xzf "$TMPDIR/repo.tar.gz" -C "$TMPDIR"
# The tarball extracts as "<repo>-<ref>/" (refs with slashes get
# flattened). Locate the only top-level dir and dive into
# scripts/atari-hd/.
SRC_ROOT=$(find "$TMPDIR" -mindepth 1 -maxdepth 1 -type d \
                ! -name "atari-hd-install.*" | head -n 1)
SRC="$SRC_ROOT/scripts/atari-hd"
if [ ! -f "$SRC/atari_hd.py" ] || [ ! -f "$SRC/version.txt" ]; then
    warn "atari-hd installer: tarball doesn't contain scripts/atari-hd/."
    warn "  REF=$REF may be wrong; try --ref=main."
    exit 1
fi

NEW_VERSION=$(cat "$SRC/version.txt")
if [ -f "$INSTALL_DIR/version.txt" ]; then
    OLD_VERSION=$(cat "$INSTALL_DIR/version.txt")
    if [ "$OLD_VERSION" = "$NEW_VERSION" ]; then
        say "  installed v$OLD_VERSION; refreshing files."
    else
        say "  installed v$OLD_VERSION -> v$NEW_VERSION"
    fi
else
    say "  fresh install: v$NEW_VERSION"
fi

# Stage a clean tree (skip drivers/ epics/ tests/ __pycache__/).
mkdir -p "$INSTALL_DIR" "$BIN_DIR"
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
# Copy the runtime files only. drivers/ epics/ tests/ are deliberately
# excluded -- they're contributor / user-supplied territory.
for item in atari_hd.py tui assets tools \
            atari-hd atari-hd.cmd version.txt \
            README.md BOOTABLE.md; do
    if [ -e "$SRC/$item" ]; then
        cp -R "$SRC/$item" "$INSTALL_DIR/"
    fi
done
find "$INSTALL_DIR" -type d -name __pycache__ -prune -exec rm -rf {} +
chmod 755 "$INSTALL_DIR/atari-hd" 2>/dev/null || :

# Shim. Prefer symlink; fall back to a wrapper when the fs doesn't
# support links.
if ln -sfn "$INSTALL_DIR/atari-hd" "$SHIM_PATH" 2>/dev/null; then
    :
else
    cat > "$SHIM_PATH" <<EOF
#!/usr/bin/env sh
exec "$INSTALL_DIR/atari-hd" "\$@"
EOF
    chmod 755 "$SHIM_PATH"
fi

say ""
say "Installed atari-hd v$NEW_VERSION"
say "  package  : $INSTALL_DIR"
say "  shim     : $SHIM_PATH"

# PATH hint.
case ":$PATH:" in
    *":$BIN_DIR:"*)
        say "  $BIN_DIR is already on your PATH; run 'atari-hd' to start."
        ;;
    *)
        say ""
        say "$BIN_DIR is not on your PATH yet. Add it for your shell:"
        case "${SHELL:-}" in
            */zsh)  rcfile="\$HOME/.zshrc" ;;
            */bash) rcfile="\$HOME/.bashrc (Linux) / \$HOME/.bash_profile (macOS)" ;;
            */fish) rcfile="\$HOME/.config/fish/config.fish" ;;
            *)      rcfile="your shell's rc file" ;;
        esac
        case "${SHELL:-}" in
            */fish)
                say "  echo 'set -gx PATH $BIN_DIR \$PATH' >> $rcfile"
                ;;
            *)
                say "  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> $rcfile"
                ;;
        esac
        say "  Then open a new shell, or 'source' the file."
        ;;
esac

# Python sanity.
if ! command -v python3 >/dev/null 2>&1; then
    say ""
    warn "Note: python3 isn't on your PATH. Install Python 3.10+ before running atari-hd."
    warn "  https://www.python.org/downloads/"
fi
