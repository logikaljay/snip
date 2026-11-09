#!/bin/sh
# Install the latest release of snip.
#
#   curl -fsSL https://raw.githubusercontent.com/logikaljay/snip/main/install.sh | sh
#
# Environment:
#   SNIP_INSTALL_DIR  where to put the binary (default: ~/.local/bin)
#   SNIP_VERSION      release tag to install, e.g. v0.1.3 (default: latest)
set -eu

REPO="logikaljay/snip"
INSTALL_DIR="${SNIP_INSTALL_DIR:-$HOME/.local/bin}"
VERSION="${SNIP_VERSION:-latest}"

say() { printf '%s\n' "$*"; }
die() { printf 'snip install: %s\n' "$*" >&2; exit 1; }

case "$(uname -s)" in
  Linux) ;;
  *) die "snip only runs on Linux (Wayland)." ;;
esac

case "$(uname -m)" in
  x86_64 | amd64) ARCH=x86_64 ;;
  aarch64 | arm64) ARCH=aarch64 ;;
  *) die "no prebuilt binary for $(uname -m); build from source instead." ;;
esac

command -v curl >/dev/null 2>&1 || die "curl is required."

ASSET="snip-linux-$ARCH"
if [ "$VERSION" = latest ]; then
  BASE="https://github.com/$REPO/releases/latest/download"
else
  BASE="https://github.com/$REPO/releases/download/$VERSION"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM

say "Downloading $ASSET ($VERSION)..."
curl -fsSL "$BASE/$ASSET" -o "$TMP/$ASSET" || die "download failed: $BASE/$ASSET"

if command -v sha256sum >/dev/null 2>&1 &&
  curl -fsSL "$BASE/SHA256SUMS" -o "$TMP/SHA256SUMS" 2>/dev/null; then
  (cd "$TMP" && grep " $ASSET\$" SHA256SUMS | sha256sum -c --status) ||
    die "checksum mismatch for $ASSET."
fi

mkdir -p "$INSTALL_DIR"
install -m 755 "$TMP/$ASSET" "$INSTALL_DIR/snip"
say "Installed snip to $INSTALL_DIR/snip"

# Check what snip needs at runtime.
missing=""
if command -v ldd >/dev/null 2>&1; then
  libs="$(ldd "$INSTALL_DIR/snip" 2>/dev/null | awk '/not found/ {print $1}')"
  case "$libs" in
    *gtk4-layer-shell*) missing="$missing gtk4-layer-shell" ;;
  esac
  case "$libs" in
    *libgtk-4*) missing="$missing gtk4" ;;
  esac
fi
command -v grim >/dev/null 2>&1 || missing="$missing grim"
command -v wl-copy >/dev/null 2>&1 || missing="$missing wl-clipboard"
command -v notify-send >/dev/null 2>&1 || missing="$missing libnotify"

if [ -n "$missing" ]; then
  say ""
  say "snip needs these packages, which seem to be missing:$missing"
  say "On Arch Linux:  sudo pacman -S --needed$missing"
fi

case ":$PATH:" in
  *":$INSTALL_DIR:"*) ;;
  *)
    say ""
    say "$INSTALL_DIR is not on your PATH. Add it, e.g.:"
    say "  export PATH=\"$INSTALL_DIR:\$PATH\""
    ;;
esac
