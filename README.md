# vkSumi

A Vulkan layer for runtime color grading on Linux. Brightness, contrast, saturation, hue,
gamma, temperature, tint, RGB gain, 3-band lift/gain. Live `.conf` file with hot reload.
Per-game configs. The Adrenalin / NVIDIA Freestyle equivalent the platform's been missing.

Made because vkBasalt is unmaintained and the only other option is `ReShade -> wine ->
inject -> pray (waste of time for some minor tweaks, ain't installing all that.)`.

## What it does

- Hooks into any Vulkan game (native or via Proton/DXVK)
- Sliders for the usual color grading suspects
- 0 = no change for every knob, easy to read off
- Edit your `~/.config/vkSumi/vkSumi.conf` while the game runs, save, see it instantly
- Per-game overrides auto-created on first launch

## Install

```fish
meson setup builddir --prefix="$HOME/.local"
ninja -C builddir
meson install -C builddir
```

Drops the `.so` in `~/.local/lib/`, the layer manifest in
`~/.local/share/vulkan/implicit_layer.d/`, and the toggle script in `~/.local/bin/`.

Currently available through AUR:

```fish
yay -S vksumi
```

## Use

The layer only activates when `ENABLE_VKSUMI=1` is set, otherwise it's invisible.

Native Linux:
```fish
env ENABLE_VKSUMI=1 some-vulkan-game
```

Steam (any game) - right-click -> Properties -> Launch Options:
```
ENABLE_VKSUMI=1 %command%
```

Works for Proton/DXVK too since DXVK speaks Vulkan underneath.

Kill switch if a game breaks: `DISABLE_VKSUMI=1 ...` (always wins over enable).

## Config

Lives at `~/.config/vkSumi/vkSumi.conf` (global) and
`~/.config/vkSumi/games/<exe-name>.conf` (per-game, auto-created on first launch).
Per-game files merge on top of global, only set what you want different.

```ini
enabled     = true
toggle_keys = Shift_R+F9

brightness  = 0.0    # [-1 .. 1]
contrast    = 0.0    # [-1 .. 1]
exposure    = 0.0    # EV stops
saturation  = 0.0    # -1 = grayscale
hue_deg     = 0.0    # [-180 .. 180]
gamma       = 0.0    # + brightens midtones
temperature = 0.0    # warm (+) cool (-)
tint        = 0.0    # magenta (+) green (-)

red_gain    = 0.0
green_gain  = 0.0
blue_gain   = 0.0

shadows     = 0.0
midtones    = 0.0
highlights  = 0.0
```

See `vkSumi.conf.example` for the full annotated version.

## Hotkey

Two ways:

**In-layer** (X11 + XWayland, so Wine/Proton games work):
Set `toggle_keys = Shift_R+F9` in the conf. Press the combo while the game window is
focused. Format is "+"-separated X11 keysym names (full list in
`/usr/include/X11/keysymdef.h`).

**Script + compositor bind**:
```
# Hyprland (~/.config/hypr/hyprland.conf)
bind = SHIFT, F9, exec, vksumi-toggle

# sway (~/.config/sway/config)
bindsym Shift+F9 exec vksumi-toggle
```
The script flips `enabled` in the conf, inotify catches it, instant reload.

## Known limits

- **HDR**: detected and passed through unchanged. Doing color grading correctly on
  PQ / scRGB needs a different shader path, not done yet.
- **Native Wayland Vulkan games**: in-layer hotkey doesn't work (Wayland blocks key
  grabs from clients). Use the toggle script instead.

## Debugging

```fish
ENABLE_VKSUMI=1 VKSUMI_DEBUG=1 some-game
```
Dumps every layer call to stderr. Useful when something looks weird.

## Credits

Architecture lifted from [vkBasalt](https://github.com/DadSchoorse/vkBasalt) (RIP).
[MangoHud](https://github.com/flightlessmango/MangoHud)'s code helped figure out a few
tricky bits.

## License

MIT.
