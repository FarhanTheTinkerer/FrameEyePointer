#pragma once

#include <cmath>

namespace eyepointer {

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline bool isFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/// Returns the unit vector, or a zero vector if `v` is too short to normalize.
inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    return len > 1e-9f ? v * (1.0f / len) : Vec3{};
}

/// Angle between two unit vectors in radians.
inline float angleBetween(const Vec3& a, const Vec3& b) {
    float c = dot(a, b);
    c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
    return std::acos(c);
}

/// Row-major 3x4 rigid transform, laid out like vr::HmdMatrix34_t (rotation in the
/// left 3x3, translation in the last column).
struct Mat34 {
    float m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};

    Vec3 position() const { return {m[0][3], m[1][3], m[2][3]}; }
    Vec3 axisX() const { return {m[0][0], m[1][0], m[2][0]}; }
    Vec3 axisY() const { return {m[0][1], m[1][1], m[2][1]}; }
    Vec3 axisZ() const { return {m[0][2], m[1][2], m[2][2]}; }

    Vec3 rotate(const Vec3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    /// Applies the transposed rotation (the inverse for an orthonormal rotation).
    Vec3 inverseRotate(const Vec3& v) const {
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z};
    }
    Vec3 transformPoint(const Vec3& p) const { return rotate(p) + position(); }
    Vec3 inverseTransformPoint(const Vec3& p) const { return inverseRotate(p - position()); }

    static Mat34 fromAxes(const Vec3& x, const Vec3& y, const Vec3& z, const Vec3& pos) {
        Mat34 r;
        r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x; r.m[0][3] = pos.x;
        r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y; r.m[1][3] = pos.y;
        r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z; r.m[2][3] = pos.z;
        return r;
    }
};

/// Builds a transform at `position` whose +Z axis points at `viewer`, the
/// orientation OpenVR overlays need to face the user. +Y stays as close to
/// world up as possible.
inline Mat34 billboard(const Vec3& position, const Vec3& viewer) {
    Vec3 z = normalize(viewer - position);
    if (length(z) < 0.5f) z = {0, 0, 1};
    Vec3 up{0, 1, 0};
    Vec3 x = cross(up, z);
    if (length(x) < 1e-4f) x = cross(Vec3{0, 0, -1}, z); // looking straight up or down
    x = normalize(x);
    Vec3 y = cross(z, x);
    return Mat34::fromAxes(x, y, z, position);
}

} // namespace eyepointer
