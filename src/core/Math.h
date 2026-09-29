// Small 2D math toolkit used by the document model, geometry helpers and renderer.
// All quantities are SI (metres, radians) unless noted otherwise.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace kn {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kDegPerRad = 180.0 / kPi;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(double s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2&) const = default;
};

constexpr Vec2 operator*(double s, Vec2 v) { return {v.x * s, v.y * s}; }
constexpr double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
// Left perpendicular (counter-clockwise rotation by 90 degrees).
constexpr Vec2 perp(Vec2 v) { return {-v.y, v.x}; }
inline double length(Vec2 v) { return std::hypot(v.x, v.y); }
constexpr double lengthSq(Vec2 v) { return v.x * v.x + v.y * v.y; }
inline double distance(Vec2 a, Vec2 b) { return length(a - b); }
inline Vec2 normalized(Vec2 v)
{
    const double l = length(v);
    return l > 0.0 ? v / l : Vec2{};
}
inline Vec2 rotate(Vec2 v, double c, double s) { return {c * v.x - s * v.y, s * v.x + c * v.y}; }
inline Vec2 rotate(Vec2 v, double angle) { return rotate(v, std::cos(angle), std::sin(angle)); }
constexpr Vec2 lerp(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }

// Rigid transform: world = p + R * local.
struct Pose {
    Vec2 p;
    double c = 1.0;
    double s = 0.0;

    static Pose fromAngle(Vec2 p, double angle) { return {p, std::cos(angle), std::sin(angle)}; }
    double angle() const { return std::atan2(s, c); }
    Vec2 apply(Vec2 local) const { return p + rotate(local, c, s); }
    Vec2 applyVector(Vec2 local) const { return rotate(local, c, s); }
    Vec2 inverse(Vec2 world) const
    {
        const Vec2 d = world - p;
        return {c * d.x + s * d.y, -s * d.x + c * d.y};
    }
};

struct Aabb {
    Vec2 lo{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec2 hi{-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};

    bool valid() const { return lo.x <= hi.x && lo.y <= hi.y; }
    void add(Vec2 p)
    {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    void add(const Aabb& b)
    {
        if (b.valid()) { add(b.lo); add(b.hi); }
    }
    void inflate(double r)
    {
        lo -= Vec2{r, r};
        hi += Vec2{r, r};
    }
    bool contains(Vec2 p) const { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; }
    bool overlaps(const Aabb& b) const
    {
        return lo.x <= b.hi.x && b.lo.x <= hi.x && lo.y <= b.hi.y && b.lo.y <= hi.y;
    }
    Vec2 center() const { return (lo + hi) * 0.5; }
    Vec2 size() const { return hi - lo; }
};

// Wrap an angle into (-pi, pi].
inline double wrapAngle(double a)
{
    a = std::fmod(a + kPi, kTwoPi);
    if (a <= 0.0) a += kTwoPi;
    return a - kPi;
}

// 8-bit-per-channel sRGB colour with alpha. Item colours are scene data, not UI style.
struct Color {
    uint8_t r = 128, g = 128, b = 128, a = 255;

    constexpr bool operator==(const Color&) const = default;
    constexpr uint32_t packed() const { return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24; }
    static constexpr Color fromRgb(uint32_t rgb, uint8_t alpha = 255)
    {
        return {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), alpha};
    }
    constexpr uint32_t rgb() const { return uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b); }
    constexpr Color withAlpha(uint8_t alpha) const { return {r, g, b, alpha}; }
};

} // namespace kn
