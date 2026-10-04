#pragma once

// Header-only glue between OpenVR and eyepointer_core. Include it from any
// OpenVR app (FrameEyePointer itself, SlopCadVR, ...) that has openvr.h on its
// include path. Your action manifest needs an action of type "eyetracking"
// bound to "/user/head/eyetracking" (see app/resources in this repository).

#include "eyepointer/GazeTracker.h"
#include "eyepointer/Math.h"

#include <openvr.h>

#include <cstring>

namespace eyepointer::openvr {

inline Vec3 toVec(const vr::HmdVector3_t& v) { return {v.v[0], v.v[1], v.v[2]}; }
inline vr::HmdVector3_t toHmd(const Vec3& v) { return {{v.x, v.y, v.z}}; }

inline Mat34 toMat(const vr::HmdMatrix34_t& m) {
    Mat34 r;
    std::memcpy(r.m, m.m, sizeof r.m);
    return r;
}

inline vr::HmdMatrix34_t toHmd(const Mat34& m) {
    vr::HmdMatrix34_t r;
    std::memcpy(r.m, m.m, sizeof r.m);
    return r;
}

/// Reads one eye-tracking sample. `error` (optional) receives the OpenVR result.
inline RawGaze readGaze(vr::VRActionHandle_t gazeAction,
                        vr::ETrackingUniverseOrigin origin = vr::TrackingUniverseStanding,
                        float secondsFromNow = 0.0f, vr::EVRInputError* error = nullptr) {
    RawGaze g;
    vr::VREyeTrackingData_t d{};
    vr::EVRInputError e =
        vr::VRInput()->GetEyeTrackingDataRelativeToNow(gazeAction, origin, secondsFromNow, &d, sizeof d);
    if (error) *error = e;
    if (e != vr::VRInputError_None) return g;
    g.valid = d.bActive && d.bValid && d.bTracked;
    g.origin = toVec(d.vGazeOrigin);
    g.target = toVec(d.vGazeTarget);
    return g;
}

/// Reads the eye-tracking sample for the frame predicted by the last
/// IVRCompositor::WaitGetPoses() (for scene applications that render).
inline RawGaze readGazeForNextFrame(vr::VRActionHandle_t gazeAction,
                                    vr::ETrackingUniverseOrigin origin = vr::TrackingUniverseStanding,
                                    vr::EVRInputError* error = nullptr) {
    RawGaze g;
    vr::VREyeTrackingData_t d{};
    vr::EVRInputError e =
        vr::VRInput()->GetEyeTrackingDataForNextFrame(gazeAction, origin, &d, sizeof d);
    if (error) *error = e;
    if (e != vr::VRInputError_None) return g;
    g.valid = d.bActive && d.bValid && d.bTracked;
    g.origin = toVec(d.vGazeOrigin);
    g.target = toVec(d.vGazeTarget);
    return g;
}

/// HMD pose `secondsFromNow` in the future; false if tracking is lost.
inline bool hmdPose(float secondsFromNow, Mat34& out,
                    vr::ETrackingUniverseOrigin origin = vr::TrackingUniverseStanding) {
    vr::TrackedDevicePose_t pose[1];
    vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(origin, secondsFromNow, pose, 1);
    if (!pose[0].bPoseIsValid) return false;
    out = toMat(pose[0].mDeviceToAbsoluteTracking);
    return true;
}

} // namespace eyepointer::openvr
