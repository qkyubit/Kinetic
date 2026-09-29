#include "RenderBatch.h"

namespace kn {

void RenderBatch::clear()
{
    for (int i = 0; i < kLayerCount; ++i) {
        segs[i].clear();
        discs[i].clear();
        tris[i].clear();
    }
    rays.clear();
    labels.clear();
}

void RenderBatch::line(Layer l, Vec2 a, Vec2 b, float widthPx, Color c)
{
    segs[int(l)].push_back({float(a.x), float(a.y), float(b.x), float(b.y), widthPx, c.packed()});
}

void RenderBatch::polyline(Layer l, const Vec2* pts, int n, bool closed, float widthPx, Color c)
{
    for (int i = 0; i + 1 < n; ++i) line(l, pts[i], pts[i + 1], widthPx, c);
    if (closed && n > 2) line(l, pts[n - 1], pts[0], widthPx, c);
}

void RenderBatch::dashed(Layer l, Vec2 a, Vec2 b, float widthPx, Color c, double dashPx)
{
    const double len = distance(a, b);
    const double dash = px(dashPx);
    if (len <= 0.0 || dash <= 0.0) return;
    const Vec2 dir = (b - a) / len;
    const int maxDashes = 512;
    int k = 0;
    for (double t = 0.0; t < len && k < maxDashes; t += 2.0 * dash, ++k)
        line(l, a + dir * t, a + dir * std::min(t + dash, len), widthPx, c);
}

void RenderBatch::arrow(Layer l, Vec2 from, Vec2 to, float widthPx, Color c, double headPx)
{
    const Vec2 d = to - from;
    const double len = length(d);
    if (len <= 1e-12) return;
    const Vec2 dir = d / len;
    const double head = std::min(px(headPx), len * 0.6);
    const Vec2 base = to - dir * head;
    line(l, from, base + dir * px(1.0), widthPx, c);
    const Vec2 n = perp(dir) * (head * 0.45);
    triangle(l, to, base + n, base - n, c);
}

void RenderBatch::disc(Layer l, Vec2 c, double r, Color fill, Color outline, float outlinePx, bool marker,
                       double markerAngle)
{
    // For a disc the axis encodes the marker direction with zero length is not
    // possible, so the marker angle travels in a tiny axis the shader ignores.
    const float mx = float(std::cos(markerAngle)) * 1e-6f;
    const float my = float(std::sin(markerAngle)) * 1e-6f;
    discs[int(l)].push_back({float(c.x), float(c.y), mx, my, float(r), outlinePx, marker ? 1.0f : 0.0f,
                             fill.packed(), outline.packed()});
}

void RenderBatch::capsule(Layer l, Vec2 a, Vec2 b, double r, Color fill, Color outline, float outlinePx)
{
    const Vec2 c = (a + b) * 0.5;
    const Vec2 ax = (b - a) * 0.5;
    discs[int(l)].push_back({float(c.x), float(c.y), float(ax.x), float(ax.y), float(r), outlinePx, 0.0f,
                             fill.packed(), outline.packed()});
}

void RenderBatch::circleOutline(Layer l, Vec2 c, double r, Color col, float widthPx)
{
    disc(l, c, r, col.withAlpha(0), col, widthPx);
}

void RenderBatch::arc(Layer l, Vec2 c, double r, double a0, double a1, float widthPx, Color col, int segments)
{
    segments = std::clamp(segments, 1, 256);
    Vec2 prev = c + Vec2{std::cos(a0), std::sin(a0)} * r;
    for (int i = 1; i <= segments; ++i) {
        const double a = a0 + (a1 - a0) * double(i) / segments;
        const Vec2 p = c + Vec2{std::cos(a), std::sin(a)} * r;
        line(l, prev, p, widthPx, col);
        prev = p;
    }
}

void RenderBatch::triangle(Layer l, Vec2 a, Vec2 b, Vec2 c, Color col)
{
    const uint32_t p = col.packed();
    auto& t = tris[int(l)];
    t.push_back({float(a.x), float(a.y), p});
    t.push_back({float(b.x), float(b.y), p});
    t.push_back({float(c.x), float(c.y), p});
}

void RenderBatch::convexFill(Layer l, const Vec2* pts, int n, Color col)
{
    for (int i = 1; i + 1 < n; ++i) triangle(l, pts[0], pts[i], pts[i + 1], col);
}

void RenderBatch::rectFill(Layer l, Vec2 lo, Vec2 hi, Color col)
{
    triangle(l, lo, {hi.x, lo.y}, hi, col);
    triangle(l, lo, hi, {lo.x, hi.y}, col);
}

void RenderBatch::ray(Vec2 a, Vec2 b, float widthPx, Color c)
{
    rays.push_back({float(a.x), float(a.y), float(b.x), float(b.y), widthPx, c.packed()});
}

void RenderBatch::label(Vec2 pos, const QString& text, Color c, int dx, int dy)
{
    TextLabel t;
    t.pos = pos;
    t.text = text;
    t.color = c;
    t.dx = dx;
    t.dy = dy;
    labels.push_back(std::move(t));
}

void RenderBatch::valueLabel(Vec2 pos, const QString& prefix, double value, const QString& unit, int decimals,
                             Color c, int dx, int dy)
{
    TextLabel t;
    t.pos = pos;
    t.text = prefix;
    t.value = value;
    t.unit = unit;
    t.hasValue = true;
    t.decimals = decimals;
    t.color = c;
    t.dx = dx;
    t.dy = dy;
    labels.push_back(std::move(t));
}

void RenderBatch::shape(Layer l, const Shape& s, const Pose& pose, Color fill, Color outline, float outlinePx,
                        bool marker)
{
    switch (s.kind) {
    case Shape::Circle:
        disc(l, pose.apply(s.a), s.radius, fill, outline, outlinePx, marker, pose.angle());
        break;
    case Shape::Capsule:
        capsule(l, pose.apply(s.a), pose.apply(s.b), s.radius, fill, outline, outlinePx);
        break;
    case Shape::Segment:
        line(l, pose.apply(s.a), pose.apply(s.b), outlinePx, outline);
        break;
    case Shape::Polygon: {
        Vec2 w[8];
        for (int i = 0; i < s.count; ++i) w[i] = pose.apply(s.verts[size_t(i)]);
        convexFill(l, w, s.count, fill);
        if (outlinePx > 0.0f) polyline(l, w, s.count, true, outlinePx, outline);
        break;
    }
    }
}

} // namespace kn
