#pragma once

#include "eyepointer/Math.h"

namespace eyepointer {

struct GazeFilterParams {
    /// One Euro filter cutoff while the eye is still (Hz). Lower = steadier, laggier.
    float minCutoffHz = 0.8f;
    /// How fast the cutoff rises with eye speed (Hz per deg/s). Higher = less lag on movement.
    float beta = 0.04f;
    /// Cutoff used to smooth the speed estimate (Hz).
    float derivativeCutoffHz = 1.0f;
    /// A jump bigger than this (degrees) is treated as a saccade and taken immediately.
    float saccadeSnapDeg = 6.0f;
    /// A gap longer than this (seconds) between samples restarts the filter.
    float maxGapSeconds = 0.25f;
};

/// Smooths a stream of unit gaze directions with a One Euro filter
/// (Casiez et al., CHI 2012) applied to the direction vector, using the angular
/// speed as the derivative. Feed it head-relative directions so that head
/// motion is never smoothed, only eye motion.
class GazeFilter {
public:
    explicit GazeFilter(GazeFilterParams params = {}) : params_(params) {}

    void setParams(const GazeFilterParams& params) { params_ = params; }
    const GazeFilterParams& params() const { return params_; }

    void reset() { initialized_ = false; }
    bool initialized() const { return initialized_; }

    /// Adds a sample (any non-zero direction; it is normalized) taken at `timeSeconds`
    /// and returns the filtered unit direction.
    Vec3 update(const Vec3& direction, double timeSeconds);

    /// Smoothed eye speed in degrees per second, for diagnostics.
    float speedDegPerSec() const { return speed_; }

private:
    GazeFilterParams params_;
    bool initialized_ = false;
    Vec3 value_{0, 0, -1};
    double lastTime_ = 0;
    float speed_ = 0;
};

} // namespace eyepointer
