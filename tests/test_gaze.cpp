#include "TestHarness.h"

#include "eyepointer/GazeFilter.h"
#include "eyepointer/GazeTracker.h"
#include "eyepointer/Reticle.h"

#include <algorithm>
#include <cmath>

using namespace eyepointer;

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;

Vec3 dirFromYawPitch(float yawDeg, float pitchDeg) {
    float y = yawDeg * kDeg, p = pitchDeg * kDeg;
    return {std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
}

float degBetween(const Vec3& a, const Vec3& b) { return angleBetween(normalize(a), normalize(b)) / kDeg; }

Mat34 yawPose(float yawDeg, Vec3 pos = {}) {
    float y = yawDeg * kDeg;
    // Rotation about +Y.
    Vec3 x{std::cos(y), 0, -std::sin(y)};
    Vec3 up{0, 1, 0};
    Vec3 z{std::sin(y), 0, std::cos(y)};
    return Mat34::fromAxes(x, up, z, pos);
}

} // namespace

TEST("math: billboard faces the viewer") {
    Mat34 m = billboard({0, 1, -2}, {0, 1.6f, 0});
    Vec3 toViewer = normalize(Vec3{0, 0.6f, 2});
    CHECK_NEAR(dot(m.axisZ(), toViewer), 1.0, 1e-5);
    CHECK_NEAR(dot(m.axisX(), m.axisY()), 0.0, 1e-5);
    CHECK(m.axisY().y > 0.9f);
    // Straight down still gives an orthonormal frame.
    Mat34 d = billboard({0, 0, 0}, {0, 5, 0});
    CHECK_NEAR(length(d.axisX()), 1.0, 1e-5);
    CHECK_NEAR(dot(d.axisX(), d.axisZ()), 0.0, 1e-5);
}

TEST("math: inverse transform round trips") {
    Mat34 m = yawPose(37, {1, 2, 3});
    Vec3 p{0.3f, -0.2f, 5};
    Vec3 back = m.inverseTransformPoint(m.transformPoint(p));
    CHECK_NEAR(back.x, p.x, 1e-5);
    CHECK_NEAR(back.y, p.y, 1e-5);
    CHECK_NEAR(back.z, p.z, 1e-5);
}

TEST("filter: first sample passes through") {
    GazeFilter f;
    Vec3 d = dirFromYawPitch(10, 5);
    Vec3 out = f.update(d, 1.0);
    CHECK(degBetween(out, d) < 1e-3f);
}

TEST("filter: fixation jitter is suppressed") {
    GazeFilter f;
    float worst = 0;
    for (int i = 0; i < 180; ++i) {
        // +-0.4 deg alternating noise at 90 Hz around straight ahead.
        float n = (i % 2 ? 0.4f : -0.4f);
        Vec3 out = f.update(dirFromYawPitch(n, -n), i / 90.0);
        if (i > 45) worst = std::max(worst, degBetween(out, {0, 0, -1}));
    }
    CHECK(worst < 0.1f);
}

TEST("filter: saccades are taken immediately") {
    GazeFilter f;
    for (int i = 0; i < 90; ++i) f.update({0, 0, -1}, i / 90.0);
    Vec3 target = dirFromYawPitch(15, 0);
    Vec3 out = f.update(target, 90 / 90.0);
    CHECK(degBetween(out, target) < 1e-3f);
}

TEST("filter: smooth pursuit lag stays small") {
    GazeFilter f;
    Vec3 out;
    float yaw = 0;
    for (int i = 0; i < 180; ++i) {
        yaw = 30.0f * i / 90.0f; // 30 deg/s
        out = f.update(dirFromYawPitch(yaw, 0), i / 90.0);
    }
    float lag = degBetween(out, dirFromYawPitch(yaw, 0));
    CHECK(lag < 3.5f);
}

TEST("filter: a gap restarts the filter") {
    GazeFilter f;
    for (int i = 0; i < 10; ++i) f.update({0, 0, -1}, i / 90.0);
    Vec3 d = dirFromYawPitch(3, 0); // below the saccade threshold
    Vec3 out = f.update(d, 5.0);
    CHECK(degBetween(out, d) < 1e-3f);
}

TEST("tracker: head rotation never lags") {
    GazeTracker t;
    // Eyes fixed straight ahead in the head while the head turns quickly.
    GazeRay ray;
    for (int i = 0; i < 90; ++i) {
        Mat34 head = yawPose(i * 2.0f, {0, 1.7f, 0});
        Vec3 origin = head.position();
        Vec3 target = origin + head.rotate({0, 0, -1}) * 1.5f;
        ray = t.update({true, origin, target}, head, head, i / 90.0);
        CHECK(ray.valid);
        CHECK(degBetween(ray.direction, head.rotate({0, 0, -1})) < 1e-3f);
    }
    CHECK_NEAR(ray.fixationDistance, 1.5, 1e-4);
}

TEST("tracker: result follows the display pose") {
    GazeTracker t;
    Mat34 now = yawPose(0, {0, 1.7f, 0});
    Mat34 later = yawPose(10, {0.1f, 1.7f, 0});
    Vec3 origin = now.position();
    GazeRay ray = t.update({true, origin, origin + Vec3{0, 0, -2}}, now, later, 0.0);
    CHECK(degBetween(ray.direction, later.rotate({0, 0, -1})) < 1e-3f);
    CHECK_NEAR(ray.origin.x, 0.1, 1e-5);
}

TEST("tracker: invalid samples are rejected") {
    GazeTracker t;
    Mat34 head;
    CHECK(!t.update({false, {}, {0, 0, -1}}, head, head, 0).valid);
    CHECK(!t.update({true, {}, {}}, head, head, 0).valid);
    CHECK(!t.update({true, {NAN, 0, 0}, {0, 0, -1}}, head, head, 0).valid);
}

TEST("reticle: centre is coloured, corners are clear") {
    auto img = makeReticleImage(64, 0x00E5FF, 1.0f);
    CHECK(img.size() == 64u * 64u * 4u);
    const uint8_t* centre = &img[(32 * 64 + 32) * 4];
    CHECK(centre[3] > 200);
    CHECK(centre[1] > 200 && centre[0] < 30);
    CHECK(img[3] == 0);                       // top-left corner
    CHECK(img[(64 * 64 - 1) * 4 + 3] == 0);   // bottom-right corner
}
