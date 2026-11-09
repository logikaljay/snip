# snip

A macOS-style (Cmd+Shift+4) region screenshot tool for Hyprland and other wlroots-based Wayland compositors.

Running `snip` freezes every monitor and shows a crosshair overlay. Drag a box to capture that region. While you drag, the area outside the box is dimmed and a label next to the cursor shows the size in real pixels.

## Usage

| Command              | What it does                                            |
| -------------------- | ------------------------------------------------------- |
| `snip`               | Select a region interactively and save it to a file     |
| `snip --full`        | Save the whole screen (all monitors) without an overlay |
| `snip --full --copy` | Copy the whole screen to the clipboard                  |

`--copy` only applies with `--full`. In region mode, hold Ctrl instead.

While selecting:

- **Drag** with the left mouse button to select a region. Releasing the button takes the shot.
- **Hold Ctrl** when you release to copy to the clipboard instead of saving. The border turns blue and the label shows `⧉ copy`.
- **Esc** or **right-click** cancels.
- A plain click (under 3 px of movement) is ignored, so you can start the drag again.

### Output

Saved screenshots go to `~/Pictures/Screenshots` (the XDG Pictures directory) with names like `Screenshot 2026-11-09 at 16.35.02.png`. Set `OMARCHY_SCREENSHOT_DIR` to save somewhere else. Each save sends a desktop notification through `notify-send`.

Copied screenshots go to the Wayland clipboard as `image/png` via `wl-copy`.

## Dependencies

Build:

- A C compiler, `make`, `pkg-config`
- `gtk4`
- `gtk4-layer-shell`

Runtime:

- `grim` to capture the screen
- `wl-clipboard` (`wl-copy`) to copy to the clipboard
- `libnotify` (`notify-send`) for save notifications

On Arch Linux:

```sh
sudo pacman -S gtk4 gtk4-layer-shell grim wl-clipboard libnotify
```

## Install

Install the latest prebuilt release (x86_64 or aarch64) to `~/.local/bin`:

```sh
curl -fsSL https://raw.githubusercontent.com/logikaljay/snip/main/install.sh | sh
```

The script picks the binary for your CPU, verifies its checksum, and lists any runtime dependencies you're missing. You can change what it does with environment variables:

```sh
# Install somewhere else
curl -fsSL https://raw.githubusercontent.com/logikaljay/snip/main/install.sh | sudo env SNIP_INSTALL_DIR=/usr/local/bin sh

# Install a specific release
curl -fsSL https://raw.githubusercontent.com/logikaljay/snip/main/install.sh | SNIP_VERSION=v0.1.3 sh
```

Run the same command again to update.

## Build from source

```sh
make
make install            # installs to ~/.local/bin/snip
make install PREFIX=/usr/local
make clean
```

## Hyprland keybindings

Example bindings for `~/.config/hypr/hyprland.conf` (or `bindings.conf` on Omarchy):

```ini
bind = SUPER SHIFT, 4, exec, snip
bind = SUPER SHIFT, 3, exec, snip --full
bind = SUPER CTRL SHIFT, 3, exec, snip --full --copy
```

## How it works

1. `grim` captures each monitor in parallel to a temporary PPM file in `$XDG_RUNTIME_DIR`, which is deleted right after it is loaded.
2. A full-screen layer-shell window on the overlay layer shows each frozen image, with exclusive keyboard focus.
3. When you release the mouse, the selection is scaled to physical pixels (so HiDPI and fractional scaling stay sharp), cropped from the frozen image, and saved or copied.

The selection works within a single monitor; it can't cross from one monitor to another.
