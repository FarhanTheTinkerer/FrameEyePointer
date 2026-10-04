# Using eye gaze in SlopCadVR

SlopCadVR is a SteamVR scene app, so it reads gaze itself; it doesn't need the
FrameEyePointer overlay running. It reuses `eyepointer_core` for filtering and
`OpenVrGaze.h` for the OpenVR calls. Then it ray-casts the filtered gaze ray
against its own scene for hover and selection.

## 1. Add the action and binding

In SlopCadVR's action manifest:

```json
{ "name": "/actions/cad/in/gaze", "type": "eyetracking", "requirement": "optional" }
```

and a default binding for the headset (`controller_type: "frame_hmd"`, plus
`generic_hmd` for other eye-tracked headsets):

```json
{
  "controller_type": "frame_hmd",
  "bindings": {
    "/actions/cad": {
      "eyetracking": [
        { "output": "/actions/cad/in/gaze", "path": "/user/head/eyetracking" }
      ]
    }
  }
}
```

## 2. Link the core library

```cmake
set(EYEPOINTER_BUILD_APP OFF CACHE BOOL "" FORCE)
set(EYEPOINTER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/FrameEyePointer EXCLUDE_FROM_ALL)  # or FetchContent
target_link_libraries(slopcad PRIVATE eyepointer_core)
```

With the app off, nothing OpenVR-related is defined, so SlopCadVR keeps its own OpenVR target. `OpenVrGaze.h` only needs `openvr.h` on the include path, which SlopCadVR already has.

## 3. Per frame, after `WaitGetPoses`

```cpp
#include "eyepointer/GazeTracker.h"
#include "eyepointer/openvr/OpenVrGaze.h"

eyepointer::GazeTracker gazeTracker; // member; tune with setFilterParams()

void updateGaze(double timeSeconds, const vr::TrackedDevicePose_t& hmdRenderPose) {
    using namespace eyepointer;
    RawGaze raw = openvr::readGazeForNextFrame(gazeAction_);
    Mat34 head = openvr::toMat(hmdRenderPose.mDeviceToAbsoluteTracking);
    // Same pose for both: the sample is already predicted for this frame.
    GazeRay ray = gazeTracker.update(raw, head, head, timeSeconds);
    if (!ray.valid) { clearGazeHover(); return; }

    // ray.origin / ray.direction are in the tracking universe used by readGaze
    // (standing by default). Convert into model space and pick:
    if (auto hit = scene.raycast(ray.origin, ray.direction)) {
        setGazeHover(hit->entity);  // highlight face/edge/vertex under the eyes
    }
}
```

Ideas that fit CAD well:

- **Look-to-target, click-to-act.** The controller laser stays for precise
  manipulation, and gaze picks *which* feature a command applies to (for example,
  look at a face, press the bumper to start a sketch on it).
- **Gaze-weighted snapping.** When the controller laser is near several
  candidates, prefer the one you are looking at.
- Use `ray.fixationDistance` as a hint for which of several overlapping
  surfaces the user is focused on.
