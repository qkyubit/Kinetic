// Vortex: a swirling force field. Bodies inside the region are pushed around
// the centre with a force proportional to their mass, strongest at the centre
// and fading linearly to zero at the edge. (The README's worked example.)
#include "FieldItem.h"

namespace kn {

class VortexField final : public FieldType {
public:
    VortexField()
        : FieldType({QStringLiteral("field.vortex"), QStringLiteral("Vortex"), QStringLiteral("Fields"),
                     QStringLiteral("view-refresh"), QStringLiteral(":/icons/field.svg"),
                     {QStringLiteral("swirl"), QStringLiteral("whirlpool"), QStringLiteral("tornado")},
                     QStringLiteral("Pushes bodies around its centre (counter-clockwise for positive strength).")})
    {
        schema_.beginGroup(QStringLiteral("Vortex"));
        strength_ = schema_.size();
        schema_.num(QStringLiteral("strength"), QStringLiteral("Strength"), unit::mps2, -1000, 1000, 15, 2)
            .tip(QStringLiteral("Tangential acceleration at the centre."));
        addRegion(Circle, 4, 4, 3);
        addColour(Color::fromRgb(0x00cec9, 60));
    }

    // Tangential acceleration at p (m/s²).
    Vec2 acceleration(const ItemDef& d, const Pose& pose, Vec2 p) const
    {
        const Vec2 r = p - pose.p;
        const double dist = length(r), R = d.d(r_);
        if (dist < 1e-9 || dist > R) return {};
        return perp(r / dist) * (d.d(strength_) * (1.0 - dist / R));
    }

    bool addField(const ItemDef& d, const Pose& pose, FieldKind k, Vec2 p, Vec2& inout) const override
    {
        if (k == FieldKind::Gravity && contains(d, pose, p)) inout += acceleration(d, pose, p);
        return false;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Color c = drawRegion(d, ctx).withAlpha(220);
        const double s = d.d(strength_) >= 0 ? 1.0 : -1.0, r = std::min(d.d(r_) * 0.5, ctx.px(40));
        ctx.batch.arc(Layer::Front, ctx.pose.p, r, 0.0, s * 1.5 * kPi, 2.0f, c);
        const double th = s * 1.5 * kPi;
        const Vec2 tip = ctx.pose.p + Vec2{std::cos(th), std::sin(th)} * r;
        const Vec2 tangent = Vec2{-std::sin(th), std::cos(th)} * s;
        ctx.batch.arrow(Layer::Front, tip - tangent * ctx.px(10), tip, 2.0f, c, 9);
    }

    int strength_;
};

namespace {
class VortexRuntime final : public FieldRuntime {
public:
    VortexRuntime(const ItemDef& d, const VortexField& t, World& w) : FieldRuntime(d, t, w), t_(t) { edited(d); }
    void edited(const ItemDef& d) override { def_ = std::make_shared<ItemDef>(d); }

    // Called before every physics step; must not allocate.
    void applyField(World& w) override
    {
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            if (!affects(w, i)) continue;
            const BodyRec& b = w.body(i);
            const Vec2 a = t_.acceleration(*def_, pose_, {b.com.x, b.com.y});
            w.addFieldForce(i, ForceSource::Field, {float(a.x * b.mass), float(a.y * b.mass)});
        }
    }

private:
    const VortexField& t_;
};
} // namespace

std::unique_ptr<ItemRuntime> VortexField::createRuntime(const ItemDef& d, World& w, const RestoreState*) const
{
    return std::make_unique<VortexRuntime>(d, *this, w);
}

KINETIC_DEFINE_ITEM(VortexField)

} // namespace kn
