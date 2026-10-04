#pragma once

#include "eyepointer/GazeFilter.h"
#include "eyepointer/Math.h"

namespace eyepointer {

/// One eye-tracker sample in tracking space, as vr::VREyeTrackingData_t gives it:
/// a ray origin (between the eyes) and the point being looked at.
struct RawGaze {
    bool valid = false;
    Vec3 origin;
    Vec3 target;
};

/// A filtered gaze ray in tracking space.
struct GazeRay {
    bool valid = false;
    Vec3 origin;
    Vec3 direction;               ///< unit
    float fixationDistance = 0;   ///< |target - origin| of the raw sample, metres
};

/// Turns raw samples into a stable ray. The direction is filtered relative to the
/// head, so turning your head never lags; only eye motion is smoothed. The result
/// is re-expressed with the head pose predicted for display.
class GazeTracker {
public:
    explicit GazeTracker(GazeFilterParams params = {}) : filter_(params) {}

    void setFilterParams(const GazeFilterParams& p) { filter_.setParams(p); }
    void reset() { filter_.reset(); }
    const GazeFilter& filter() const { return filter_; }

    /// `headAtSample` is the head pose at the time the sample refers to,
    /// `headForDisplay` the pose the result should be drawn with (may be the same).
    GazeRay update(const RawGaze& sample, const Mat34& headAtSample, const Mat34& headForDisplay,
                   double timeSeconds);

private:
    GazeFilter filter_;
};

} // namespace eyepointer
