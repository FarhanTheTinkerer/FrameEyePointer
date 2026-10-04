#include "eyepointer/GazeFilter.h"

namespace eyepointer {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kRadToDeg = 180.0f / kPi;

float smoothingAlpha(float cutoffHz, float dt) {
    float tau = 1.0f / (2.0f * kPi * cutoffHz);
    return 1.0f / (1.0f + tau / dt);
}

} // namespace

Vec3 GazeFilter::update(const Vec3& direction, double timeSeconds) {
    Vec3 raw = normalize(direction);
    if (length(raw) < 0.5f) return value_;

    double dtd = timeSeconds - lastTime_;
    if (!initialized_ || dtd <= 0.0 || dtd > params_.maxGapSeconds) {
        initialized_ = true;
        value_ = raw;
        lastTime_ = timeSeconds;
        speed_ = 0;
        return value_;
    }
    float dt = static_cast<float>(dtd);
    lastTime_ = timeSeconds;

    float jumpDeg = angleBetween(value_, raw) * kRadToDeg;
    if (jumpDeg >= params_.saccadeSnapDeg) {
        value_ = raw;
        speed_ = jumpDeg / dt;
        return value_;
    }

    float instantSpeed = jumpDeg / dt;
    speed_ += smoothingAlpha(params_.derivativeCutoffHz, dt) * (instantSpeed - speed_);

    float cutoff = params_.minCutoffHz + params_.beta * speed_;
    float a = smoothingAlpha(cutoff, dt);
    value_ = normalize(value_ * (1.0f - a) + raw * a);
    return value_;
}

} // namespace eyepointer
