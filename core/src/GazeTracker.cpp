#include "eyepointer/GazeTracker.h"

namespace eyepointer {

GazeRay GazeTracker::update(const RawGaze& sample, const Mat34& headAtSample,
                            const Mat34& headForDisplay, double timeSeconds) {
    GazeRay out;
    if (!sample.valid || !isFinite(sample.origin) || !isFinite(sample.target)) {
        filter_.reset();
        return out;
    }
    Vec3 delta = sample.target - sample.origin;
    float dist = length(delta);
    if (dist < 1e-4f) {
        filter_.reset();
        return out;
    }

    Vec3 dirHead = headAtSample.inverseRotate(delta * (1.0f / dist));
    Vec3 originHead = headAtSample.inverseTransformPoint(sample.origin);
    Vec3 filtered = filter_.update(dirHead, timeSeconds);

    out.valid = true;
    out.origin = headForDisplay.transformPoint(originHead);
    out.direction = normalize(headForDisplay.rotate(filtered));
    out.fixationDistance = dist;
    return out;
}

} // namespace eyepointer
