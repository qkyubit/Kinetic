// Fluid container region: an axis-aligned body of still liquid whose surface is
// the top edge. Bodies get
//   buoyancy  ρ A_sub |g|, opposite to gravity, at the centroid of the
//             submerged area (exact for polygons; circles exact while between
//             the side walls, otherwise 64-gon)
//   drag      −(k μ W + ½ ρ Cd W |v|) v at the same point, with W the silhouette
//             width across the motion scaled by the submerged fraction
//   rotation  −(k μ W R² + ½ ρ Cd W R³ |ω|) ω
// all per metre of depth, matching the 2D mass convention. k = 6 (Stokes-like).
#include "core/Geometry.h"
#include "core/Item.h"
#include "core/RenderBatch.h"
#include "core/Runtime.h"
#include "core/World.h"

namespace kn {

namespace {
struct Liquid {
    const char* name;
    double density;   // kg/m³
    double viscosity; // Pa·s (about 20 °C)
    uint32_t color;
};
const Liquid kLiquids[] = {
    {"Custom", 1000, 1.0e-3, 0x3498db},  {"Water", 998, 1.0e-3, 0x3498db},  {"Sea water", 1025, 1.08e-3, 0x1f8a9e},
    {"Olive oil", 911, 0.084, 0xd4ac0d}, {"Mercury", 13534, 1.53e-3, 0xaab7b8}, {"Honey", 1420, 10.0, 0xe59866},
    {"Glycerol", 1261, 1.41, 0xd6eaf8},
};
constexpr double kViscousK = 6.0;
} // namespace

class FluidRegion final : public ItemType {
public:
    FluidRegion()
    {
        meta_ = {QStringLiteral("fluid.region"), QStringLiteral("Liquid"), QStringLiteral("Fluids"),
                 QStringLiteral("water"), QStringLiteral(":/icons/fluid.svg"),
                 {QStringLiteral("water"), QStringLiteral("tank"), QStringLiteral("buoyancy"), QStringLiteral("oil"),
                  QStringLiteral("mercury"), QStringLiteral("honey"), QStringLiteral("pool")},
                 QStringLiteral("A region of still liquid: buoyancy and drag on submerged bodies. The surface is the "
                                "top edge."),
                 ItemKind::Fluid};
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Region"));
        s.vec(QStringLiteral("position"), QStringLiteral("Centre"), unit::m, -10000, 10000, {0, 0});
        w_ = s.size();
        s.num(QStringLiteral("width"), QStringLiteral("Width"), unit::m, 0.05, 10000, 6, 3).log();
        h_ = s.size();
        s.num(QStringLiteral("height"), QStringLiteral("Depth"), unit::m, 0.05, 10000, 3, 3).log();
        s.beginGroup(QStringLiteral("Liquid"));
        preset_ = s.size();
        QStringList names;
        for (const Liquid& l : kLiquids) names << QString::fromLatin1(l.name);
        s.choice(QStringLiteral("liquid"), QStringLiteral("Liquid"), names, 1);
        rho_ = s.size();
        s.num(QStringLiteral("density"), QStringLiteral("Density"), unit::density, 1, 30000, kLiquids[1].density, 1).log();
        mu_ = s.size();
        s.num(QStringLiteral("viscosity"), QStringLiteral("Viscosity"), unit::pas, 0, 1000, kLiquids[1].viscosity, 4)
            .tip(QStringLiteral("Dynamic viscosity: linear drag k μ W v."));
        s.beginGroup(QStringLiteral("Appearance"));
        color_ = s.size();
        s.colour(QStringLiteral("color"), QStringLiteral("Colour"), Color::fromRgb(kLiquids[1].color, 90));
    }
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }

    void propertyChanged(ItemDef& d, int index) const override
    {
        if (index == preset_) {
            const int64_t p = d.i(preset_);
            if (p > 0 && p < int64_t(std::size(kLiquids))) {
                d.set(rho_, kLiquids[p].density);
                d.set(mu_, kLiquids[p].viscosity);
                d.set(color_, Color::fromRgb(kLiquids[p].color, d.c(color_).a));
            }
        } else if (index == rho_ || index == mu_) {
            d.set(preset_, int64_t(0));
        }
    }

    void shapes(const ItemDef& d, ShapeList& out) const override
    {
        out.clear();
        out.push_back(Shape::box(0.5 * d.d(w_), 0.5 * d.d(h_)));
    }
    bool scale(ItemDef& d, double sx, double sy) const override
    {
        d.set(w_, clampValue(schema_[w_], d.d(w_) * std::abs(sx)));
        d.set(h_, clampValue(schema_[h_], d.d(h_) * std::abs(sy)));
        return true;
    }
    bool hitTest(const ItemDef& d, const SceneView&, Vec2 world, double tol) const override
    {
        // The surface and walls pick the liquid; the inside stays free for bodies.
        const Vec2 c = pose(d).p;
        const double hw = 0.5 * d.d(w_), hh = 0.5 * d.d(h_);
        const Vec2 l = world - c;
        const bool outer = std::abs(l.x) <= hw + tol && std::abs(l.y) <= hh + tol;
        const bool inner = std::abs(l.x) < hw - tol && std::abs(l.y) < hh - tol;
        return outer && !inner;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 c = ctx.pose.p;
        const double hw = 0.5 * d.d(w_), hh = 0.5 * d.d(h_);
        const Color col = d.c(color_);
        // In front of bodies, translucent, so submerged parts look submerged.
        ctx.batch.rectFill(Layer::Front, c - Vec2{hw, hh}, c + Vec2{hw, hh}, col);
        const Color edge = ctx.selected ? ctx.theme.highlight : col.withAlpha(230);
        ctx.batch.line(Layer::Front, c + Vec2{-hw, hh}, c + Vec2{hw, hh}, ctx.selected ? 3.0f : 2.0f, edge);
        const Vec2 q[4] = {c + Vec2{-hw, hh}, c + Vec2{-hw, -hh}, c + Vec2{hw, -hh}, c + Vec2{hw, hh}};
        ctx.batch.polyline(Layer::Front, q, 4, false, 1.0f, edge.withAlpha(120));
        static QString names[std::size(kLiquids)];
        const int64_t p = std::clamp<int64_t>(d.i(preset_), 0, int64_t(std::size(kLiquids)) - 1);
        if (names[p].isEmpty()) names[p] = QString::fromLatin1(kLiquids[p].name);
        static const QString rhoLabel = QStringLiteral(" · ρ = ");
        ctx.batch.valueLabel(c + Vec2{-hw, hh}, names[p] + rhoLabel, d.d(rho_), unit::density, 0, ctx.theme.text, 60, -10);
    }

    int w_, h_, preset_, rho_, mu_, color_;

private:
    ItemMeta meta_;
    PropertySchema schema_;
};

namespace {
class FluidRuntime final : public ItemRuntime {
public:
    FluidRuntime(const ItemDef& d, const FluidRegion& t, World& w) : ItemRuntime(d.id), t_(t)
    {
        applyEdit(d, ~0ull, w);
    }

    bool applyEdit(const ItemDef& d, uint64_t, World& w) override
    {
        const Vec2 c = t_.pose(d).p;
        const Vec2 half{0.5 * d.d(t_.w_), 0.5 * d.d(t_.h_)};
        lo_ = c - half;
        hi_ = c + half;
        rho_ = d.d(t_.rho_);
        mu_ = d.d(t_.mu_);
        w.wakeAll();
        return true;
    }

    void preStep(World& w) override
    {
        const Vec2 g = w.settings().gravity;
        const double h = w.dt(), alpha = w.predictAlpha();
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            const BodyRec& b = w.body(i);
            if (!b.alive || b.type != b2_dynamicBody || !b.awake) continue;
            const b2AABB box = b2Body_ComputeAABB(b.id);
            if (box.upperBound.x < lo_.x || box.lowerBound.x > hi_.x || box.upperBound.y < lo_.y ||
                box.lowerBound.y > hi_.y)
                continue;
            // Submerged area at the predicted pose (see World::predictPoint).
            const Vec2 com{b.com.x, b.com.y}, v{b.v.x, b.v.y};
            const Vec2 origin{b.origin.x, b.origin.y};
            const double dA = alpha * h * b.w;
            const Vec2 rel = rotate(origin - com, std::cos(dA), std::sin(dA));
            const double c = double(b.rot.c) * std::cos(dA) - double(b.rot.s) * std::sin(dA);
            const double s = double(b.rot.s) * std::cos(dA) + double(b.rot.c) * std::sin(dA);
            const Pose pose{com + v * (alpha * h) + rel, c, s};
            double area = 0, total = 0;
            Vec2 moment;
            for (const Shape& sh : b.shapes) {
                const geom::AreaMoment am = geom::shapeInBox(sh, pose, lo_, hi_);
                area += am.area;
                moment += am.centroid * am.area;
                total += geom::shapeArea(sh);
            }
            if (area <= 0.0 || total <= 0.0) continue;
            const Vec2 cb = moment / area;
            const Vec2 cbNow = cb - v * (alpha * h); // apply at the current position
            const b2Vec2 at{float(cbNow.x), float(cbNow.y)};
            w.addFieldForce(i, ForceSource::Buoyancy, {float(-rho_ * area * g.x), float(-rho_ * area * g.y)}, at);

            const double frac = area / total;
            const double R = std::sqrt(total / kPi);
            const b2Vec2 pv = b2Add(b.v, b2CrossSV(b.w, b2Sub(at, b.com)));
            const double speed = b2Length(pv);
            if (speed > 1e-9) {
                const b2Vec2 dir = b2MulSV(float(1.0 / speed), pv);
                const double W = w.silhouetteWidth(i, {-dir.y, dir.x}) * frac;
                double f = (kViscousK * mu_ * W + 0.5 * rho_ * b.dragCoefficient * W * speed) * speed;
                f = std::min(f, double(b.mass) * speed / h);
                w.addFieldForce(i, ForceSource::Drag, b2MulSV(float(-f), dir), at);
            }
            const double om = b.w;
            if (std::abs(om) > 1e-9) {
                const double W = 2.0 * R * frac;
                double tq = (kViscousK * mu_ * W * R * R + 0.5 * rho_ * b.dragCoefficient * W * R * R * R * std::abs(om)) * om;
                const double cap = double(b.inertia) * std::abs(om) / h;
                tq = std::clamp(tq, -cap, cap);
                w.addFieldTorque(i, ForceSource::Drag, float(-tq));
            }
        }
    }

private:
    const FluidRegion& t_;
    Vec2 lo_, hi_;
    double rho_ = 1000, mu_ = 1e-3;
};
} // namespace

std::unique_ptr<ItemRuntime> FluidRegion::createRuntime(const ItemDef& d, World& w, const RestoreState*) const
{
    return std::make_unique<FluidRuntime>(d, *this, w);
}

KINETIC_DEFINE_ITEM(FluidRegion)

} // namespace kn
