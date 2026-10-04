# FrameEyePointer

An eye-tracked "laser pointer" for SteamVR on the Steam Frame. Wherever you look,
a reticle appears; look at a desktop overlay and the Windows mouse follows your
eyes; press a controller button to click.

It is a small SteamVR **overlay app** that runs next to whatever game or app you
have open. It reads gaze through the official OpenVR eye-tracking API
(`IVRInput::GetEyeTrackingDataRelativeToNow`, OpenVR SDK 2.15.6), so it runs on
the PC that runs SteamVR (the Frame streams from it) with no headset-side hacks.

## What it does

| You look at | What happens |
| --- | --- |
| A desktop overlay ([Desktop+](https://github.com/elvissteinjr/DesktopPlus)) | The reticle lands on it **and the Windows cursor moves there**. Buttons click, drag and scroll. |
| The SteamVR dashboard | The reticle lands on it (pointer only, see [Limitations](#limitations)). |
| Anything else | The reticle floats at the depth your eyes are focused on. |

### Default controls (Steam Frame controllers)

| Input | Action |
| --- | --- |
| Right **bumper** | Left click where you look (hold and look away to drag) |
| Left **bumper** | Right click where you look |
| Right thumbstick up/down | Scroll, only while looking at a desktop overlay |
| Hold left **View** button | Turn the eye pointer on/off |

All of these can be rebound in **SteamVR Settings → Controllers → Manage
Controller Bindings → Frame Eye Pointer**. Bindings are also included for Index
and Touch controllers (B/Y buttons click).

## Install (Windows)

1. Download `FrameEyePointer-windows-x64` from the latest successful run on the
   [Actions tab](../../actions/workflows/build.yml) and unzip it somewhere permanent
   (for example `C:\Tools\FrameEyePointer`). Or [build it](#build) yourself.
2. Start SteamVR with the Frame connected, and make sure eye tracking is turned on
   for the headset.
3. Run once from a terminal to check everything is visible:
   ```powershell
   .\FrameEyePointer.exe --probe
   ```
   This prints whether eye tracking works, your controllers, the overlays it can
   see and your monitors (also written to `FrameEyePointer.log`).
4. Run `FrameEyePointer.exe`. To start it with SteamVR every time:
   ```powershell
   .\FrameEyePointer.exe --autostart on
   ```
   It shows up in SteamVR's startup overlay apps list, where you can also turn it off.

Use `--console` to see the live log. Without it the app runs with no window
and logs to `FrameEyePointer.log`.

## Settings

`FrameEyePointer.ini` is created next to the exe on first run. Edits apply while
the app is running. Main knobs:

- `[pointer]` `reticle_degrees`, `reticle_color`, `reticle_alpha`, and
  `free_depth` (`fixation` = your eyes' focus distance, or a fixed number of metres).
- `[filter]` gaze smoothing. Lower `min_cutoff_hz` gives a steadier pointer while you fixate,
  and a higher `beta` gives less lag when your eyes move. Saccades bigger than
  `saccade_snap_deg` are applied immediately.
- `[mouse]` `deadband_px` (cursor ignores tiny eye jitter), `drag_threshold_px`.
- `[target.*]` the overlays the pointer lands on. Each has the overlay `key` (a
  trailing `*` matches `key0`..`key31`), `mode = desktop | pointer` and, for
  desktops, `desktop = auto | virtual | primary | monitor:N | rect:x,y,w,h`.
  `auto` works out which monitor(s) an overlay shows from its texture size.
  `--probe` lists overlay keys and monitor numbers.

## How it works

```
 SteamVR (PC) ── eye gaze ray ──► GazeTracker ──► gaze ray in standing space
   /user/head/eyetracking          (filter in head space:      │
                                    One Euro + saccade snap)   ▼
                                               ComputeOverlayIntersection on target overlays
                                                     │                       │
                                         reticle overlay at hit       desktop overlay hit?
                                         (billboard, constant         UV ─► monitor pixel
                                          angular size)               PointerController ─► SetCursorPos / SendInput
```

- **Filtering** happens on the eye direction *relative to the head*, so turning
  your head never lags. Only eye motion is smoothed. The ray is then drawn with
  the head pose predicted for the next displayed frame.
- **Clicks** land where the cursor was resting, not where fixation jitter puts it
  at the moment of the press. Holding a button and looking more than
  `drag_threshold_px` away starts a drag.
- **Desktop mapping** uses the overlay's texture UV (v up), then picks the
  monitor rectangle from the overlay's mouse scale. Gaps between monitors are ignored.

### Layout

| Path | What |
| --- | --- |
| `core/` | `eyepointer_core`: platform-independent gaze filter, gaze tracker, desktop mapping, pointer/click logic, settings and reticle image. No OpenVR dependency. Unit tested. |
| `core/include/eyepointer/openvr/OpenVrGaze.h` | Header-only OpenVR glue (read gaze, HMD pose, conversions), shared by this app and [SlopCadVR](docs/SLOPCADVR_INTEGRATION.md). |
| `app/` | The overlay app, platform layer (Windows mouse, monitors), action manifest and bindings. |
| `third_party/openvr/` | OpenVR SDK 2.15.6 header and runtime libraries, unmodified. |

## Build

Windows (Visual Studio 2022):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
cmake --install build --config Release --prefix dist/FrameEyePointer
```

Linux (core tests and the app; desktop mouse control is Windows-only for now):

```sh
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

Cross-compiling the Windows exe from Linux:

```sh
cmake -S . -B build/win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build/win -j
```

## Limitations

- **SteamVR dashboard and other apps' overlays are pointer-only.** OpenVR has no
  public way for one app to send clicks into another app's overlay. The SteamVR
  controller laser is drawn by SteamVR itself. The eye pointer can only *click*
  where Windows' own mouse reaches, which is desktop overlays like Desktop+.
- **Inside games** the reticle is visible but cannot click game UI, for the same
  reason. That would need a SteamVR driver that adds a virtual "eye" controller.
- Eye tracking must be exposed to OpenVR apps by the headset's driver. Run
  `--probe` to check.
- Desktop mouse control is implemented for Windows only.

## Credits

- [ValveSoftware/openvr](https://github.com/ValveSoftware/openvr): SDK and eye-tracking API.
- [konsti219/frameeyeosc](https://github.com/konsti219/frameeyeosc): showed the
  Frame's eye data (gaze, fixation point, lids) and its head-relative, −Z-forward convention.
- [zisonMyu/SteamGazeOverlay](https://github.com/zisonMyu/SteamGazeOverlay): confirmed
  the `eyetracking` binding path (`/user/head/eyetracking`) and Desktop+ UV conventions.
  No code was copied.
- One Euro filter: Casiez, Roussel and Vogel, CHI 2012.
