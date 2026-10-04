# FrameEyePointer on the Steam Frame

A laser pointer driven by your eyes, running on the headset itself. Look at
something, press the right bumper, and it gets clicked: the SteamVR dashboard,
Steam, overlays, and desktop panels, anywhere a controller's laser works.

## How it works

```
 eye tracker ──► frameeyepointerd (service) ──socket──► frameeyepointer driver (in SteamVR)
 controllers ──►   picks an aim source,                  an invisible controller whose ray
 hand tracker ─►   smooths it, sequences clicks          is your gaze: SteamVR's own laser
 (optional)                                              follows it and clicks with it
```

- **The driver** (`driver/frameeyepointer`) adds a virtual controller to SteamVR,
  in the treadmill role so it never takes a hand away from your real
  controllers. SteamVR's laser mouse uses it like any controller. The ray starts at
  your eyes, so you see only the hit dot, not a beam.
- **The service** (`frameeyepointerd`) starts and stops with SteamVR. Every frame
  it reads the aim sources and button sources listed in your settings,
  sends the ray to the driver, and turns button presses into laser clicks.
  Input sources are modules; see [INPUT_SOURCES.md](INPUT_SOURCES.md) for adding
  hand tracking.

## Install

You need the Steam Frame, a keyboard (Bluetooth or on-screen), and about 10 MB.

1. Get the package: on the headset, open the latest successful **build** run in the
   repository's Actions tab and download `FrameEyePointer-steamframe-arm64`. Or
   build it yourself (below).
2. In the launcher, choose Launch a program → Desktop, then open System → Konsole.
3. Unpack and install:
   ```sh
   cd ~/Downloads
   unzip FrameEyePointer-steamframe-arm64.zip    # GitHub's wrapper around the tarball
   tar xzf FrameEyePointer-steamframe-arm64.tar.gz
   cd FrameEyePointer && ./install.sh
   ```
   Nothing needs `sudo`. Files go to `~/.local/share/frameeyepointer`, the driver is
   registered with SteamVR, and a user service is set up to start with SteamVR. It
   offers to restart SteamVR, which it needs to load the driver.
4. Check it:
   ```sh
   ~/.local/share/frameeyepointer/install.sh probe
   ```
   Look for `laser driver: loaded` and `gaze action: N of 90 samples valid`.

The service turns on SteamVR's **Enable global input from overlays
(Experimental)** setting, so the bumpers reach it while the dashboard or another app
has input focus. Set `global_input = false` in the settings to stop that.

## Use

| Input | What it does |
| --- | --- |
| Look | The laser's dot goes where you look |
| Right bumper | Click there |
| Left bumper | Right click there |
| Right thumbstick | Scroll there |
| Hold left View (0.6 s) | Turn the eye laser on or off |

Bindings can be changed in SteamVR's controller bindings under FrameEyePointer.

**In games:** the game keeps every button. When you open the dashboard during a game,
the eye laser works on the dashboard. During games the gaze comes from the eye
tracker's shared memory instead of SteamVR's eye tracking action, because reading
the action while a game runs makes SteamVR restart its eye tracker.

## Settings

`~/.config/frameeyepointer/frameeyepointer.ini`, created on first start. Changes
apply within a second. The file explains each setting. The main ones:

- `[inputs] aim_sources`, `button_sources`: which input modules to use and in
  what order.
- `[filter]`: smoothing. Lower `min_cutoff_hz` makes the dot steadier while you
  fixate; higher `beta` makes it lag less when your eyes move.
- `[click] mode`: `tap` (default) or `hold` (lets you drag; see below).

## The controller click problem (read this if clicks misbehave)

Steam reads the Frame controllers directly, not through SteamVR's bindings, and
about 40 ms after **every** press and release it switches SteamVR out of laser mode.
Frametop documented this ([docs/gaze-controllers.md](https://github.com/DeeJanuz/frametop/blob/main/docs/gaze-controllers.md)).
FrameEyePointer works around it:

- In **tap** mode a press sends a complete click right away (down, then up 25 ms
  later), so it lands before Steam's switch.
- After each press and release, the eye laser takes the laser back (`reclaim_delay_ms`).
- An invisible overlay keeps SteamVR in laser mode while the eye laser is on
  (`force_laser_mode`).

This hasn't been tested on hardware yet. If clicks get lost, try a larger or
smaller `tap_ms` (10–35) and `reclaim_delay_ms` (60–150). Dragging (`mode = hold`) is
likely to break when Steam switches modes; that's a Steam limitation.

## SteamOS beta notes

Frametop reports its gaze mode can't read the eye tracker on SteamOS beta 0.4.3.
Frametop reads the shared memory at fixed offsets for layout version 4. Newer
SteamVR builds use version 5, whose fields sit 5 bytes later (as
[frameeyeosc](https://github.com/konsti219/frameeyeosc) handles). FrameEyePointer
checks the version and refuses layouts it doesn't know, and outside games it uses
SteamVR's documented eye tracking action, which doesn't depend on that layout.
If `probe` shows an unknown layout version, send its output (it includes a hex dump
of the header) so the layout can be added.

## Uninstall

```sh
~/.local/share/frameeyepointer/install.sh uninstall
```

Then restart SteamVR. Your settings stay in `~/.config/frameeyepointer`.

## Build

On an x86-64 Ubuntu 24.04 machine (its cross toolchain has glibc 2.39, like the Frame):

```sh
sudo apt install g++-aarch64-linux-gnu cmake ninja-build
cmake -S . -B build/frame -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
      -DEYEPOINTER_BUILD_APP=OFF -DEYEPOINTER_BUILD_TESTS=OFF
cmake --build build/frame
cmake --install build/frame --prefix dist/FrameEyePointer
```

`dist/FrameEyePointer` is the package: copy it to the headset and run `./install.sh`.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `laser driver: NOT loaded` | Restart SteamVR. Check `vrpathreg show` lists `~/.local/share/frameeyepointer/driver/frameeyepointer`. |
| `gaze action: 0 of 90 samples valid` | Eye tracking on in SteamVR settings? Headset worn? Is the eye tracker calibrated? |
| `unknown layout version` | Only affects games. Report the probe output. |
| Bumpers do nothing | `install.sh log` should show `turned on ... global input`. In SteamVR's settings, check that "Enable global input from overlays" is on. |
| The dot shows but clicks miss | See "The controller click problem" above. |

`install.sh log` shows the service log. It prints a status line every 30 s with
how often it had a gaze, how many clicks it sent, and which device held the laser.
