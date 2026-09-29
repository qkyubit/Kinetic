#include "Geometry.h"

#include <algorithm>

namespace kn::geom {

double signedArea(const Vec2* p, int n)
{
    double a = 0.0;
    for (int i = 0; i < n; ++i) a += cross(p[i], p[(i + 1) % n]);
    return 0.5 * a;
}

Vec2 polygonCentroid(const Vec2* p, int n)
{
    if (n <= 0) return {};
    // Relative to the first vertex for numerical robustness.
    const Vec2 o = p[0];
    double a = 0.0;
    Vec2 c;
    for (int i = 1; i + 1 < n; ++i) {
        const Vec2 e1 = p[i] - o;
        const Vec2 e2 = p[i + 1] - o;
        const double t = 0.5 * cross(e1, e2);
        a += t;
        c += (e1 + e2) * (t / 3.0);
    }
    if (std::abs(a) < 1e-14) {
        Vec2 m;
        for (int i = 0; i < n; ++i) m += p[i];
        return m / double(n);
    }
    return o + c / a;
}

void makeCcw(PointList& p)
{
    if (p.size() >= 3 && signedArea(p.data(), int(p.size())) < 0.0) std::reverse(p.begin(), p.end());
}

double polygonInertiaAboutOrigin(const Vec2* p, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const Vec2 a = p[i];
        const Vec2 b = p[(i + 1) % n];
        const double cr = cross(a, b);
        sum += cr * (a.x * a.x + a.x * b.x + b.x * b.x + a.y * a.y + a.y * b.y + b.y * b.y);
    }
    return std::abs(sum) / 12.0;
}

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const double l2 = lengthSq(ab);
    const double t = l2 > 0.0 ? std::clamp(dot(p - a, ab) / l2, 0.0, 1.0) : 0.0;
    return distance(p, a + ab * t);
}

static void rdp(const PointList& pts, size_t i0, size_t i1, double tol, std::vector<char>& keep)
{
    if (i1 <= i0 + 1) return;
    double maxD = -1.0;
    size_t idx = i0;
    for (size_t i = i0 + 1; i < i1; ++i) {
        const double d = distanceToSegment(pts[i], pts[i0], pts[i1]);
        if (d > maxD) { maxD = d; idx = i; }
    }
    if (maxD > tol) {
        keep[idx] = 1;
        rdp(pts, i0, idx, tol, keep);
        rdp(pts, idx, i1, tol, keep);
    }
}

PointList simplify(const PointList& pts, double tolerance)
{
    if (pts.size() < 3) return pts;
    std::vector<char> keep(pts.size(), 0);
    keep.front() = keep.back() = 1;
    rdp(pts, 0, pts.size() - 1, tolerance, keep);
    PointList out;
    for (size_t i = 0; i < pts.size(); ++i)
        if (keep[i]) out.push_back(pts[i]);
    return out;
}

PointList cleanPolygon(const PointList& in, double minDist)
{
    PointList p;
    for (const Vec2& v : in)
        if (p.empty() || distance(p.back(), v) >= minDist) p.push_back(v);
    while (p.size() > 1 && distance(p.front(), p.back()) < minDist) p.pop_back();
    // Remove near-collinear vertices.
    bool changed = true;
    while (changed && p.size() > 3) {
        changed = false;
        for (size_t i = 0; i < p.size(); ++i) {
            const Vec2 a = p[(i + p.size() - 1) % p.size()];
            const Vec2 b = p[i];
            const Vec2 c = p[(i + 1) % p.size()];
            const double l = std::max(distance(a, b) * distance(b, c), 1e-18);
            if (std::abs(cross(b - a, c - b)) / l < 1e-3) {
                p.erase(p.begin() + long(i));
                changed = true;
                break;
            }
        }
    }
    return p;
}

static double orient(Vec2 a, Vec2 b, Vec2 c) { return cross(b - a, c - a); }

static bool onSegment(Vec2 a, Vec2 b, Vec2 p)
{
    return std::min(a.x, b.x) - 1e-12 <= p.x && p.x <= std::max(a.x, b.x) + 1e-12 && std::min(a.y, b.y) - 1e-12 <= p.y &&
           p.y <= std::max(a.y, b.y) + 1e-12;
}

bool segmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const double o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) && ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0))) return true;
    if (o1 == 0 && onSegment(a, b, c)) return true;
    if (o2 == 0 && onSegment(a, b, d)) return true;
    if (o3 == 0 && onSegment(c, d, a)) return true;
    if (o4 == 0 && onSegment(c, d, b)) return true;
    return false;
}

bool isSimplePolygon(const PointList& p)
{
    const size_t n = p.size();
    if (n < 3) return false;
    for (size_t i = 0; i < n; ++i) {
        const Vec2 a = p[i], b = p[(i + 1) % n];
        for (size_t j = i + 1; j < n; ++j) {
            // Skip adjacent edges (they share a vertex).
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;
            if (segmentsIntersect(a, b, p[j], p[(j + 1) % n])) return false;
        }
    }
    return true;
}

PointList convexHull(PointList pts)
{
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) return pts;
    PointList h(2 * pts.size());
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && orient(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
        h[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && orient(h[k - 2], h[k - 1], pts[i - 1]) <= 0) --k;
        h[k++] = pts[i - 1];
    }
    h.resize(k - 1);
    return h;
}

static bool pointInTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c)
{
    return orient(a, b, p) >= 0 && orient(b, c, p) >= 0 && orient(c, a, p) >= 0;
}

bool triangulate(const PointList& poly, std::vector<std::array<int, 3>>& tris)
{
    tris.clear();
    const int n = int(poly.size());
    if (n < 3) return false;
    std::vector<int> v;
    v.resize(size_t(n));
    for (int i = 0; i < n; ++i) v[size_t(i)] = i;
    int guard = 0;
    while (v.size() > 3 && guard++ < 10 * n * n) {
        bool found = false;
        const size_t m = v.size();
        for (size_t i = 0; i < m; ++i) {
            const int ip = v[(i + m - 1) % m], ic = v[i], in = v[(i + 1) % m];
            const Vec2 a = poly[size_t(ip)], b = poly[size_t(ic)], c = poly[size_t(in)];
            if (orient(a, b, c) <= 1e-14) continue; // reflex or degenerate
            bool empty = true;
            for (size_t j = 0; j < m && empty; ++j) {
                const int q = v[j];
                if (q == ip || q == ic || q == in) continue;
                const Vec2 pq = poly[size_t(q)];
                if (pq == a || pq == b || pq == c) continue;
                if (pointInTriangle(pq, a, b, c)) empty = false;
            }
            if (!empty) continue;
            tris.push_back({ip, ic, in});
            v.erase(v.begin() + long(i));
            found = true;
            break;
        }
        if (!found) {
            // Degenerate remainder (collinear chain): drop the flattest vertex.
            size_t best = 0;
            double bestA = 1e300;
            for (size_t i = 0; i < m; ++i) {
                const double ar = std::abs(orient(poly[size_t(v[(i + m - 1) % m])], poly[size_t(v[i])],
                                                  poly[size_t(v[(i + 1) % m])]));
                if (ar < bestA) { bestA = ar; best = i; }
            }
            if (bestA > 1e-9) return false;
            v.erase(v.begin() + long(best));
        }
    }
    if (v.size() == 3 && orient(poly[size_t(v[0])], poly[size_t(v[1])], poly[size_t(v[2])]) > 1e-14)
        tris.push_back({v[0], v[1], v[2]});
    return !tris.empty();
}

static bool isConvex(const PointList& p)
{
    const size_t n = p.size();
    for (size_t i = 0; i < n; ++i)
        if (orient(p[i], p[(i + 1) % n], p[(i + 2) % n]) < -1e-12) return false;
    return true;
}

std::vector<PointList> convexDecompose(const PointList& input, int maxVerts)
{
    PointList poly = cleanPolygon(input, 1e-4);
    makeCcw(poly);
    std::vector<PointList> result;
    if (poly.size() < 3) return result;
    if (!isSimplePolygon(poly)) poly = convexHull(poly);
    if (int(poly.size()) <= maxVerts && isConvex(poly)) {
        result.push_back(poly);
        return result;
    }
    std::vector<std::array<int, 3>> tris;
    if (!triangulate(poly, tris)) {
        poly = convexHull(poly);
        tris.clear();
        for (int i = 1; i + 1 < int(poly.size()); ++i) tris.push_back({0, i, i + 1});
    }
    std::vector<std::vector<int>> parts;
    for (const auto& t : tris) parts.push_back({t[0], t[1], t[2]});

    auto toPoints = [&](const std::vector<int>& idx) {
        PointList pts;
        for (int i : idx) pts.push_back(poly[size_t(i)]);
        return pts;
    };

    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t a = 0; a < parts.size() && !merged; ++a) {
            for (size_t b = a + 1; b < parts.size() && !merged; ++b) {
                const auto& A = parts[a];
                const auto& B = parts[b];
                for (size_t i = 0; i < A.size() && !merged; ++i) {
                    const int u = A[i], w = A[(i + 1) % A.size()];
                    auto itB = std::find(B.begin(), B.end(), w);
                    if (itB == B.end()) continue;
                    const size_t jb = size_t(itB - B.begin());
                    if (B[(jb + 1) % B.size()] != u) continue;
                    // A: ... u, w ...; B: ... w, u ...  -> merged: A from w around to u, then B's interior.
                    std::vector<int> m;
                    for (size_t k = 0; k < A.size(); ++k) m.push_back(A[(i + 1 + k) % A.size()]); // w ... u
                    for (size_t k = 2; k < B.size(); ++k) m.push_back(B[(jb + k) % B.size()]);
                    PointList pts = cleanPolygon(toPoints(m), 1e-9);
                    if (int(pts.size()) > maxVerts || !isConvex(pts)) continue;
                    parts[a] = m;
                    parts.erase(parts.begin() + long(b));
                    merged = true;
                }
            }
        }
    }
    for (const auto& p : parts) {
        PointList pts = cleanPolygon(toPoints(p), 1e-9);
        if (pts.size() >= 3 && std::abs(signedArea(pts.data(), int(pts.size()))) > 1e-10) result.push_back(pts);
    }
    return result;
}

int clipHalfPlane(const Vec2* in, int n, Vec2 normal, double offset, Vec2* out)
{
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const Vec2 a = in[i];
        const Vec2 b = in[(i + 1) % n];
        const double da = dot(normal, a) - offset;
        const double db = dot(normal, b) - offset;
        if (da <= 0.0) out[m++] = a;
        if ((da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0)) out[m++] = a + (b - a) * (da / (da - db));
    }
    return m;
}

AreaMoment circleBelow(Vec2 c, double r, double level)
{
    AreaMoment res;
    const double d = level - c.y; // surface height above the centre
    if (d >= r) {
        res.area = kPi * r * r;
        res.centroid = c;
        return res;
    }
    if (d <= -r) return res;
    const double s = std::sqrt(std::max(r * r - d * d, 0.0));
    const double capArea = r * r * std::acos(d / r) - d * s;
    res.area = kPi * r * r - capArea;
    if (res.area <= 0.0) { res.area = 0.0; return res; }
    const double ybar = -(2.0 / 3.0) * s * s * s / res.area;
    res.centroid = {c.x, c.y + ybar};
    return res;
}

static AreaMoment polyAreaMoment(const Vec2* p, int n)
{
    AreaMoment r;
    if (n < 3) return r;
    r.area = std::abs(signedArea(p, n));
    r.centroid = polygonCentroid(p, n);
    return r;
}

static constexpr int kClipCap = 96;

AreaMoment convexInBox(const Vec2* p, int n, Vec2 lo, Vec2 hi)
{
    if (n <= 0 || n > kClipCap - 8) return {};
    std::array<Vec2, kClipCap> a{}, b{};
    int m = clipHalfPlane(p, n, {1, 0}, hi.x, a.data());
    m = clipHalfPlane(a.data(), m, {-1, 0}, -lo.x, b.data());
    m = clipHalfPlane(b.data(), m, {0, 1}, hi.y, a.data());
    m = clipHalfPlane(a.data(), m, {0, -1}, -lo.y, b.data());
    return polyAreaMoment(b.data(), m);
}

// Area-preserving regular polygon approximation of a circle (world space).
static int circlePolygon(Vec2 c, double r, Vec2* out, int segments)
{
    const double k = std::sqrt(kPi / (0.5 * segments * std::sin(kTwoPi / segments)));
    for (int i = 0; i < segments; ++i) {
        const double a = kTwoPi * i / segments;
        out[i] = c + Vec2{std::cos(a), std::sin(a)} * (r * k);
    }
    return segments;
}

AreaMoment shapeInBox(const Shape& s, const Pose& pose, Vec2 lo, Vec2 hi)
{
    std::array<Vec2, kClipCap> buf{};
    switch (s.kind) {
    case Shape::Circle: {
        const Vec2 c = pose.apply(s.a);
        const double r = s.radius;
        if (c.x + r < lo.x || c.x - r > hi.x || c.y + r < lo.y || c.y - r > hi.y) return {};
        if (c.x - r >= lo.x && c.x + r <= hi.x && c.y - r >= lo.y) return circleBelow(c, r, hi.y);
        const int n = circlePolygon(c, r, buf.data(), 64);
        return convexInBox(buf.data(), n, lo, hi);
    }
    case Shape::Polygon: {
        for (int i = 0; i < s.count; ++i) buf[size_t(i)] = pose.apply(s.verts[size_t(i)]);
        return convexInBox(buf.data(), s.count, lo, hi);
    }
    case Shape::Capsule: {
        const Vec2 a = pose.apply(s.a), b = pose.apply(s.b);
        const Vec2 d = normalized(b - a);
        const double base = std::atan2(d.y, d.x);
        const int half = 32;
        int n = 0;
        for (int i = 0; i <= half; ++i) {
            const double ang = base - kPi / 2 + kPi * i / half;
            buf[size_t(n++)] = b + Vec2{std::cos(ang), std::sin(ang)} * s.radius;
        }
        for (int i = 0; i <= half; ++i) {
            const double ang = base + kPi / 2 + kPi * i / half;
            buf[size_t(n++)] = a + Vec2{std::cos(ang), std::sin(ang)} * s.radius;
        }
        return convexInBox(buf.data(), n, lo, hi);
    }
    case Shape::Segment: break;
    }
    return {};
}

double projectedWidth(const Shape& s, const Pose& pose, Vec2 axis)
{
    double lo = 1e300, hi = -1e300;
    auto add = [&](Vec2 p) {
        const double t = dot(p, axis);
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    };
    switch (s.kind) {
    case Shape::Circle: add(pose.apply(s.a)); break;
    case Shape::Capsule:
    case Shape::Segment: add(pose.apply(s.a)); add(pose.apply(s.b)); break;
    case Shape::Polygon:
        for (int i = 0; i < s.count; ++i) add(pose.apply(s.verts[size_t(i)]));
        break;
    }
    return (hi - lo) + 2.0 * s.radius;
}

double shapeArea(const Shape& s)
{
    switch (s.kind) {
    case Shape::Circle: return kPi * s.radius * s.radius;
    case Shape::Capsule: return kPi * s.radius * s.radius + 2.0 * s.radius * distance(s.a, s.b);
    case Shape::Polygon: return std::abs(signedArea(s.verts.data(), s.count));
    case Shape::Segment: return 0.0;
    }
    return 0.0;
}

Aabb shapeBounds(const Shape& s, const Pose& pose)
{
    Aabb b;
    switch (s.kind) {
    case Shape::Circle: b.add(pose.apply(s.a)); break;
    case Shape::Capsule:
    case Shape::Segment: b.add(pose.apply(s.a)); b.add(pose.apply(s.b)); break;
    case Shape::Polygon:
        for (int i = 0; i < s.count; ++i) b.add(pose.apply(s.verts[size_t(i)]));
        break;
    }
    b.inflate(s.radius);
    return b;
}

bool pointInPolygon(const Vec2* poly, int n, Vec2 p)
{
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
            inside = !inside;
    }
    return inside;
}

bool shapeContains(const Shape& s, Vec2 p, double tol)
{
    switch (s.kind) {
    case Shape::Circle: return distance(p, s.a) <= s.radius + tol;
    case Shape::Capsule: return distanceToSegment(p, s.a, s.b) <= s.radius + tol;
    case Shape::Segment: return distanceToSegment(p, s.a, s.b) <= tol;
    case Shape::Polygon: {
        if (pointInPolygon(s.verts.data(), s.count, p)) return true;
        for (int i = 0; i < s.count; ++i)
            if (distanceToSegment(p, s.verts[size_t(i)], s.verts[size_t((i + 1) % s.count)]) <= tol) return true;
        return false;
    }
    }
    return false;
}

PointList regularPolygon(int sides, double r, double a0)
{
    PointList p;
    for (int i = 0; i < sides; ++i) {
        const double a = a0 + kTwoPi * i / sides;
        p.push_back({r * std::cos(a), r * std::sin(a)});
    }
    return p;
}

} // namespace kn::geom
