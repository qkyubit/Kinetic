#include "OpticsItem.h"

#include "core/Geometry.h"

namespace kn {

OpticsType::OpticsType(ItemMeta meta) : meta_(std::move(meta))
{
    meta_.kind = ItemKind::Optics;
    if (meta_.category.isEmpty()) meta_.category = QStringLiteral("Optics");
    schema_.beginGroup(QStringLiteral("Transform"));
    schema_.vec(QStringLiteral("position"), QStringLiteral("Position"), unit::m, -10000, 10000, {0, 0});
    schema_.angle(QStringLiteral("angle"), QStringLiteral("Angle"), -180, 180, 0);
}

void OpticsType::addGlass(int defaultPreset)
{
    auto& s = schema_;
    s.beginGroup(QStringLiteral("Material"));
    glass_ = s.size();
    s.choice(QStringLiteral("glass"), QStringLiteral("Glass"), optics::glassNames(), defaultPreset);
    cauchyA_ = s.size();
    s.num(QStringLiteral("cauchyA"), QStringLiteral("Cauchy A (custom)"), unit::none, 1.0, 4.0, 1.5, 4)
        .tip(QStringLiteral("n(λ) = A + B/λ² with λ in µm; used when the glass is Custom."));
    cauchyB_ = s.size();
    s.num(QStringLiteral("cauchyB"), QStringLiteral("Cauchy B (custom)"), QStringLiteral("µm²"), 0.0, 0.1, 0.004, 5);
    indexD_ = s.size();
    s.num(QStringLiteral("indexD"), QStringLiteral("Index at 587.6 nm"), unit::none, 1, 5, 1.5168, 4).readOnly();
}

void OpticsType::addColour(Color c)
{
    schema_.beginGroup(QStringLiteral("Appearance"));
    color_ = schema_.size();
    schema_.colour(QStringLiteral("color"), QStringLiteral("Colour"), c);
}

optics::Glass OpticsType::glass(const ItemDef& d) const
{
    const int p = int(d.i(glass_));
    if (p > 0 && p < optics::glassCount()) return optics::glassPreset(p);
    optics::Glass g;
    g.A = d.d(cauchyA_);
    g.B = d.d(cauchyB_);
    return g;
}

void OpticsType::propertyChanged(ItemDef& d, int) const
{
    if (glass_ >= 0) d.set(indexD_, glass(d).index(587.6));
}

void OpticsType::shapes(const ItemDef& d, ShapeList& out) const
{
    out.clear();
    thread_local PointList pts;
    bool closed = false;
    outline(d, pts, closed);
    const size_t n = pts.size();
    for (size_t i = 0; i + 1 < n + (closed ? 1 : 0); ++i) out.push_back(Shape::segment(pts[i], pts[(i + 1) % n]));
}

bool OpticsType::hitTest(const ItemDef& d, const SceneView& view, Vec2 world, double tol) const
{
    thread_local PointList pts;
    bool closed = false;
    outline(d, pts, closed);
    Pose p = pose(d);
    view.pose(d.id, p);
    const Vec2 l = p.inverse(world);
    if (closed && geom::pointInPolygon(pts.data(), int(pts.size()), l)) return true;
    const double t = std::max(tol, 4.0 * tol);
    for (size_t i = 0; i + 1 < pts.size() + (closed ? 1 : 0); ++i)
        if (geom::distanceToSegment(l, pts[i], pts[(i + 1) % pts.size()]) <= t) return true;
    return false;
}

void OpticsType::draw(const ItemDef& d, const DrawContext& ctx) const
{
    thread_local PointList pts, world;
    bool closed = false;
    outline(d, pts, closed);
    world.resize(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) world[i] = ctx.pose.apply(pts[i]);
    const Color c = d.c(color_);
    const Color edge = ctx.selected ? ctx.theme.highlight : c.withAlpha(255);
    if (closed) {
        // Fan from the item's origin (elements are star-shaped about it).
        for (size_t i = 0; i < world.size(); ++i)
            ctx.batch.triangle(Layer::Body, ctx.pose.p, world[i], world[(i + 1) % world.size()], c);
    }
    ctx.batch.polyline(Layer::Body, world.data(), int(world.size()), closed, ctx.selected ? 2.5f : 1.5f, edge);
}

void OpticsType::addPolygonElement(const ItemDef& d, const Pose& pose, optics::Scene& scene) const
{
    thread_local PointList pts;
    bool closed = false;
    outline(d, pts, closed);
    const int e = scene.addElement(glass(d));
    for (size_t i = 0; i < pts.size(); ++i)
        scene.addSegment(optics::SurfaceKind::Refract, pose.apply(pts[i]), pose.apply(pts[(i + 1) % pts.size()]), e);
}

double sag(double R, double y)
{
    if (R == 0.0) return 0.0;
    const double a = std::abs(R);
    return (R > 0 ? 1.0 : -1.0) * (a - std::sqrt(std::max(a * a - y * y, 0.0)));
}

void addSphericalSurface(optics::Scene& scene, optics::SurfaceKind kind, const Pose& pose, double vx, double R, double h,
                         int element)
{
    if (R == 0.0) { // flat
        scene.addSegment(kind, pose.apply({vx, -h}), pose.apply({vx, h}), element);
        return;
    }
    const double a = std::abs(R);
    const double alpha = std::asin(std::min(1.0, h / a));
    const double towardsVertex = R > 0 ? kPi : 0.0; // direction from the centre to the vertex
    scene.addArc(kind, pose.apply({vx + R, 0}), a, towardsVertex - alpha + pose.angle(), 2 * alpha, element);
}

void sampleSphericalSurface(PointList& out, double vx, double R, double h, bool downwards, int samples)
{
    for (int i = 0; i <= samples; ++i) {
        const double y = downwards ? h - 2 * h * i / samples : -h + 2 * h * i / samples;
        out.push_back({vx + sag(R, y), y});
    }
}

} // namespace kn
