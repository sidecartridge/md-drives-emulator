#!/usr/bin/env sh
# atari-hd one-line installer (macOS / Linux).
#
# Usage:
#     curl -fsSL https://github.com/sidecartridge/md-drives-emulator/releases/latest/download/install.sh | sh
#
# Optional environment / flags:
#     ATARI_HD_VERSION=v0.1.0  (or --version=v0.1.0)
#         Install a specific tagged release instead of latest.
#     ATARI_HD_PREFIX=/opt     (or --prefix=/opt)
#         Install root. Defaults to ~/.local. Tarball lands under
#         $prefix/share/atari-hd/, shim at $prefix/bin/atari-hd.
#
# The script:
#   1. Picks a release (--version or "latest").
#   2. Downloads the tarball + .sha256 from the GitHub release.
#   3. Verifies SHA-256.
#   4. Refuses to overwrite a newer install (compares version.txt).
#      Same-version is a no-op; older->newer upgrades in place.
#   5. Extracts to $prefix/share/atari-hd/ and writes a shim at
#      $prefix/bin/atari-hd that exec's atari_hd.py.
#   6. Tells the user how to add $prefix/bin to PATH if it isn't.
#
# Idempotent. No sudo for the default prefix.

set -eu

REPO="sidecartridge/md-drives-emulator"
DEFAULT_PREFIX="${HOME}/.local"

VERSION="${ATARI_HD_VERSION:-latest}"
PREFIX="${ATARI_HD_PREFIX:-$DEFAULT_PREFIX}"

# Parse argv flags (overrides env).
while [ $# -gt 0 ]; do
    case "$1" in
        --version=*) VERSION="${1#*=}" ;;
        --version) VERSION="${2:?}"; shift ;;
        --prefix=*) PREFIX="${1#*=}" ;;
        --prefix) PREFIX="${2:?}"; shift ;;
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

# Need: tar (extracting), shasum (verifying), python3 (runtime check).
require tar
if command -v shasum >/dev/null 2>&1; then
    SHASUM="shasum -a 256"
elif command -v sha256sum >/dev/null 2>&1; then
    SHASUM="sha256sum"
else
    warn "atari-hd installer: need shasum or sha256sum on PATH; neither found."
    exit 127
fi

# Resolve the version + asset filenames.
if [ "$VERSION" = "latest" ]; then
    BASE_URL="https://github.com/$REPO/releases/latest/download"
else
    BASE_URL="https://github.com/$REPO/releases/download/$VERSION"
fi

TARBALL="atari-hd${VERSION:+-}${VERSION#latest}.tar.gz"
# Latest doesn't carry the version in the filename; releases tagged
# vX.Y.Z do.
if [ "$VERSION" = "latest" ]; then
    TARBALL="atari-hd-latest.tar.gz"
fi
SHA_FILE="$TARBALL.sha256"

# Workspace.
TMPDIR=$(mktemp -d "${TMPDIR:-/tmp}/atari-hd-install.XXXXXX")
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

say "atari-hd installer"
say "  version : $VERSION"
say "  prefix  : $PREFIX"

say "Downloading $TARBALL ..."
$DL_CMD "$TMPDIR/$TARBALL" "$BASE_URL/$TARBALL"
$DL_CMD "$TMPDIR/$SHA_FILE" "$BASE_URL/$SHA_FILE"

say "Verifying SHA-256 ..."
(
    cd "$TMPDIR"
    # The .sha256 file's filename may differ from local; rewrite to
    # match what shasum expects.
    awk -v f="$TARBALL" '{print $1 "  " f}' "$SHA_FILE" > "$SHA_FILE.local"
    $SHASUM -c "$SHA_FILE.local" >/dev/null
)
say "  ok"

# Extract to a staging dir so we can compare versions before clobbering.
STAGE="$TMPDIR/stage"
mkdir -p "$STAGE"
tar -xzf "$TMPDIR/$TARBALL" -C "$STAGE"

# The tarball is expected to extract into a single top-level
# directory matching atari-hd-<version>/. Locate it.
SRC=$(find "$STAGE" -mindepth 1 -maxdepth 1 -type d | head -n 1)
if [ -z "$SRC" ] || [ ! -f "$SRC/version.txt" ]; then
    warn "atari-hd installer: unexpected tarball layout (no version.txt)."
    exit 1
fi

NEW_VERSION=$(cat "$SRC/version.txt")
say "  release : v$NEW_VERSION"

# Compare against the existing install (if any).
if [ -f "$INSTALL_DIR/version.txt" ]; then
    OLD_VERSION=$(cat "$INSTALL_DIR/version.txt")
    if [ "$OLD_VERSION" = "$NEW_VERSION" ]; then
        say "  already up to date (v$OLD_VERSION); refreshing files."
    else
        # Naive comparison: refuse to install older over newer.
        # Users can pass --version explicitly to override.
        sort_check=$(printf '%s\n%s\n' "$OLD_VERSION" "$NEW_VERSION" | sort -V | tail -n 1)
        if [ "$sort_check" = "$OLD_VERSION" ] && \
           [ "$OLD_VERSION" != "$NEW_VERSION" ]; then
            warn "atari-hd installer: refusing to install v$NEW_VERSION over"
            warn "  newer existing v$OLD_VERSION."
            warn "  Pass --version=v$NEW_VERSION explicitly if you want to downgrade."
            exit 1
        fi
        say "  upgrading from v$OLD_VERSION -> v$NEW_VERSION"
    fi
fi

# Install.
mkdir -p "$INSTALL_DIR" "$BIN_DIR"
# Wipe the install dir to avoid stale files from prior versions.
rm -rf "$INSTALL_DIR"
mv "$SRC" "$INSTALL_DIR"

# Shim. Prefer symlink; fall back to a hand-written wrapper when the
# fs doesn't support links (e.g., some tmpfs setups).
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
                say "  echo 'set -gx PATH \$HOME/.local/bin \$PATH' >> $rcfile"
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
