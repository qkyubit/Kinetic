#include "FieldItem.h"

namespace kn {

FieldType::FieldType(ItemMeta meta) : meta_(std::move(meta))
{
    meta_.kind = ItemKind::Field;
    if (meta_.category.isEmpty()) meta_.category = QStringLiteral("Fields");
    schema_.beginGroup(QStringLiteral("Transform"));
    schema_.vec(QStringLiteral("position"), QStringLiteral("Position"), unit::m, -10000, 10000, {0, 0});
    schema_.angle(QStringLiteral("angle"), QStringLiteral("Angle"), -180, 180, 0);
}

void FieldType::addRegion(int defaultRegion, double w, double h, double r)
{
    auto& s = schema_;
    s.beginGroup(QStringLiteral("Region"));
    region_ = s.size();
    s.choice(QStringLiteral("region"), QStringLiteral("Region"),
             {QStringLiteral("Whole world"), QStringLiteral("Rectangle"), QStringLiteral("Circle")}, defaultRegion);
    w_ = s.size();
    s.num(QStringLiteral("width"), QStringLiteral("Width"), unit::m, 0.05, 10000, w, 3).log();
    h_ = s.size();
    s.num(QStringLiteral("height"), QStringLiteral("Height"), unit::m, 0.05, 10000, h, 3).log();
    r_ = s.size();
    s.num(QStringLiteral("radius"), QStringLiteral("Radius"), unit::m, 0.05, 10000, r, 3).log();
}

void FieldType::addColour(Color c)
{
    schema_.beginGroup(QStringLiteral("Appearance"));
    color_ = schema_.size();
    schema_.colour(QStringLiteral("color"), QStringLiteral("Colour"), c);
}

void FieldType::shapes(const ItemDef& d, ShapeList& out) const
{
    out.clear();
    switch (int(d.i(region_))) {
    case Rectangle: out.push_back(Shape::box(0.5 * d.d(w_), 0.5 * d.d(h_))); break;
    case Circle: out.push_back(Shape::circle({0, 0}, d.d(r_))); break;
    default: out.push_back(Shape::circle({0, 0}, 0.25)); break; // marker
    }
}

bool FieldType::scale(ItemDef& d, double sx, double sy) const
{
    d.set(w_, clampValue(schema_[w_], d.d(w_) * std::abs(sx)));
    d.set(h_, clampValue(schema_[h_], d.d(h_) * std::abs(sy)));
    d.set(r_, clampValue(schema_[r_], d.d(r_) * std::sqrt(std::abs(sx * sy))));
    return true;
}

bool FieldType::hitTest(const ItemDef& d, const SceneView& view, Vec2 world, double tol) const
{
    // Regions are picked on their outline or marker so bodies inside stay clickable.
    const Pose p = pose(d);
    const Vec2 l = p.inverse(world);
    switch (int(d.i(region_))) {
    case Rectangle: {
        const double hw = 0.5 * d.d(w_), hh = 0.5 * d.d(h_);
        const bool inOuter = std::abs(l.x) <= hw + tol && std::abs(l.y) <= hh + tol;
        const bool inInner = std::abs(l.x) < hw - tol && std::abs(l.y) < hh - tol;
        return (inOuter && !inInner) || length(l) < 0.25 + tol;
    }
    case Circle: return std::abs(length(l) - d.d(r_)) <= tol || length(l) < 0.25 + tol;
    default: return ItemType::hitTest(d, view, world, tol);
    }
}

bool FieldType::contains(const ItemDef& d, const Pose& pose, Vec2 p) const
{
    const Vec2 l = pose.inverse(p);
    switch (int(d.i(region_))) {
    case Rectangle: return std::abs(l.x) <= 0.5 * d.d(w_) && std::abs(l.y) <= 0.5 * d.d(h_);
    case Circle: return dot(l, l) <= d.d(r_) * d.d(r_);
    default: return true;
    }
}

Color FieldType::drawRegion(const ItemDef& d, const DrawContext& ctx) const
{
    const Color c = d.c(color_);
    const Color edge = ctx.selected ? ctx.theme.highlight : c.withAlpha(200);
    const float w = ctx.selected ? 2.0f : 1.5f;
    switch (int(d.i(region_))) {
    case Rectangle: {
        const double hw = 0.5 * d.d(w_), hh = 0.5 * d.d(h_);
        Vec2 q[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        for (Vec2& p : q) p = ctx.pose.apply(p);
        ctx.batch.convexFill(Layer::Back, q, 4, c.withAlpha(uint8_t(c.a / 3)));
        for (int i = 0; i < 4; ++i) ctx.batch.dashed(Layer::Front, q[i], q[(i + 1) % 4], w, edge);
        break;
    }
    case Circle: {
        const double r = d.d(r_);
        ctx.batch.disc(Layer::Back, ctx.pose.p, r, c.withAlpha(uint8_t(c.a / 3)), c.withAlpha(0), 0.0f);
        const int n = 48;
        for (int i = 0; i < n; i += 2) {
            const double a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
            ctx.batch.line(Layer::Front, ctx.pose.p + Vec2{std::cos(a0), std::sin(a0)} * r,
                           ctx.pose.p + Vec2{std::cos(a1), std::sin(a1)} * r, w, edge);
        }
        break;
    }
    default: break;
    }
    // Marker: a ring at the item's origin (the handle for whole-world fields).
    ctx.batch.circleOutline(Layer::Overlay, ctx.pose.p, ctx.px(9), edge, w);
    return c;
}

FieldRuntime::FieldRuntime(const ItemDef& d, const FieldType& t, World& w) : ItemRuntime(d.id), type_(&t)
{
    pose_ = t.pose(d);
    region_ = int(d.i(t.regionIndex()));
    const auto& sch = t.schema();
    hw_ = 0.5 * d.d(sch.indexOf(QStringLiteral("width")));
    hh_ = 0.5 * d.d(sch.indexOf(QStringLiteral("height")));
    const double r = d.d(sch.indexOf(QStringLiteral("radius")));
    r2_ = r * r;
    w.wakeAll(); // bodies asleep in the new field must feel it
}

bool FieldRuntime::applyEdit(const ItemDef& d, uint64_t, World& w)
{
    // Parameters are cheap to re-read; geometry too. Nothing needs a rebuild.
    pose_ = type_->pose(d);
    region_ = int(d.i(type_->regionIndex()));
    const auto& sch = type_->schema();
    hw_ = 0.5 * d.d(sch.indexOf(QStringLiteral("width")));
    hh_ = 0.5 * d.d(sch.indexOf(QStringLiteral("height")));
    const double r = d.d(sch.indexOf(QStringLiteral("radius")));
    r2_ = r * r;
    edited(d);
    w.wakeAll();
    return true;
}

bool FieldRuntime::affects(const World& w, int i) const
{
    const BodyRec& b = w.body(i);
    if (!b.alive || b.type != b2_dynamicBody || !b.awake) return false;
    const Vec2 l = pose_.inverse({b.com.x, b.com.y});
    switch (region_) {
    case FieldType::Rectangle: return std::abs(l.x) <= hw_ && std::abs(l.y) <= hh_;
    case FieldType::Circle: return dot(l, l) <= r2_;
    default: return true;
    }
}

} // namespace kn
