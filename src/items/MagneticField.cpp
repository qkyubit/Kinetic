// Uniform magnetic field perpendicular to the plane. Charged bodies inside feel
// the Lorentz force q v × B, applied as the exact rotation of their velocity
// over the step so the field does no work (explicit forces would spiral out).
#include "FieldItem.h"

namespace kn {

class MagneticField final : public FieldType {
public:
    MagneticField()
        : FieldType({QStringLiteral("field.magnetic"), QStringLiteral("Magnetic field"), QStringLiteral("Fields"),
                     QStringLiteral("magnet"), QStringLiteral(":/icons/field.svg"),
                     {QStringLiteral("lorentz"), QStringLiteral("cyclotron"), QStringLiteral("B field")},
                     QStringLiteral("Uniform field out of (+) or into (−) the screen; bends moving charges.")})
    {
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Field"));
        b_ = s.size();
        s.num(QStringLiteral("strength"), QStringLiteral("Field (out of screen)"), QStringLiteral("T"), -1e7, 1e7, 2e4, 1)
            .tip(QStringLiteral("Positive: out of the screen (⊙); negative: into it (⊗)."));
        addRegion(Rectangle, 6, 6, 3);
        addColour(Color::fromRgb(0xe84393, 50));
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Color c = drawRegion(d, ctx).withAlpha(200);
        const double b = d.d(b_);
        const int region = int(d.i(regionIndex()));
        const double hw = region == Circle ? d.d(r_) : (region == Rectangle ? 0.5 * d.d(w_) : 1.0);
        const double hh = region == Circle ? d.d(r_) : (region == Rectangle ? 0.5 * d.d(h_) : 1.0);
        const double step = std::max(ctx.px(40), std::max(hw, hh) / 12);
        const double r = ctx.px(5);
        for (double y = -hh + step / 2; y < hh; y += step)
            for (double x = -hw + step / 2; x < hw; x += step) {
                const Vec2 p = ctx.pose.apply({x, y});
                if (region == Circle && !contains(d, ctx.pose, p)) continue;
                ctx.batch.circleOutline(Layer::Back, p, r, c, 1.0f);
                if (b >= 0) {
                    ctx.batch.disc(Layer::Back, p, r * 0.3, c, c, 0.0f);
                } else {
                    ctx.batch.line(Layer::Back, p + Vec2{-r, -r} * 0.7, p + Vec2{r, r} * 0.7, 1.0f, c);
                    ctx.batch.line(Layer::Back, p + Vec2{-r, r} * 0.7, p + Vec2{r, -r} * 0.7, 1.0f, c);
                }
            }
        ctx.batch.valueLabel(ctx.pose.p, QStringLiteral("B = "), b, QStringLiteral(" T"), 3, ctx.theme.text, 12, 14);
    }

    int b_;
};

namespace {
class MagneticRuntime final : public FieldRuntime {
public:
    MagneticRuntime(const ItemDef& d, const MagneticField& t, World& w) : FieldRuntime(d, t, w), t_(t) { edited(d); }
    void edited(const ItemDef& d) override { b_ = d.d(t_.b_); }

    void applyField(World& w) override
    {
        const double h = w.dt();
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            if (!affects(w, i) || w.body(i).charge == 0.0f) continue;
            const BodyRec& b = w.body(i);
            // dv/dt = (q/m) v × B ẑ rotates v at the rate −qB/m.
            const double theta = -double(b.charge) * b_ * h / b.mass;
            const double c = std::cos(theta), s = std::sin(theta);
            const double vx = b.v.x, vy = b.v.y;
            const double k = b.mass / h;
            w.addFieldForce(i, ForceSource::Magnetic, {float(k * (c * vx - s * vy - vx)), float(k * (s * vx + c * vy - vy))});
        }
    }

private:
    const MagneticField& t_;
    double b_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> MagneticField::createRuntime(const ItemDef& d, World& w, const RestoreState*) const
{
    return std::make_unique<MagneticRuntime>(d, *this, w);
}

KINETIC_DEFINE_ITEM(MagneticField)

} // namespace kn
