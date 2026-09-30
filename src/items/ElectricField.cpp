// Uniform electric field: force qE on charged bodies inside the region.
#include "FieldItem.h"

namespace kn {

class ElectricField final : public FieldType {
public:
    ElectricField()
        : FieldType({QStringLiteral("field.electric"), QStringLiteral("Electric field"), QStringLiteral("Fields"),
                     QStringLiteral("battery"), QStringLiteral(":/icons/field.svg"),
                     {QStringLiteral("capacitor"), QStringLiteral("voltage"), QStringLiteral("charge"),
                      QStringLiteral("uniform field")},
                     QStringLiteral("Uniform electric field acting on charged bodies (set a body's charge).")})
    {
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Field"));
        e_ = s.size();
        s.vec(QStringLiteral("field"), QStringLiteral("Field strength"), QStringLiteral("V/m"), -1e9, 1e9, {0.0, 1e5})
            .tip(QStringLiteral("In world axes."));
        addRegion(Rectangle, 6, 4, 3);
        addColour(Color::fromRgb(0xf1c40f, 60));
    }

    bool addField(const ItemDef& d, const Pose& pose, FieldKind k, Vec2 p, Vec2& inout) const override
    {
        if (k == FieldKind::Electric && contains(d, pose, p)) inout += d.v(e_);
        return false;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Color c = drawRegion(d, ctx).withAlpha(230);
        const Vec2 e = d.v(e_);
        const double m = length(e);
        if (m > 0) ctx.batch.arrow(Layer::Front, ctx.pose.p, ctx.pose.p + e * (ctx.px(40) / m), 2.0f, c);
        ctx.batch.valueLabel(ctx.pose.p, QStringLiteral("E = "), m, QStringLiteral(" V/m"), 0, ctx.theme.text, 12, 14);
    }

    int e_;
};

namespace {
class ElectricRuntime final : public FieldRuntime {
public:
    ElectricRuntime(const ItemDef& d, const ElectricField& t, World& w) : FieldRuntime(d, t, w), t_(t) { edited(d); }
    void edited(const ItemDef& d) override { e_ = d.v(t_.e_); }

    void applyField(World& w) override
    {
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            if (!affects(w, i) || w.body(i).charge == 0.0f) continue;
            const float q = w.body(i).charge;
            w.addFieldForce(i, ForceSource::Electric, {q * float(e_.x), q * float(e_.y)});
        }
    }

    PotentialEnergy potentialEnergy(const World& w) const override
    {
        PotentialEnergy pe;
        if (region_ != FieldType::WholeWorld) return pe;
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            const BodyRec& b = w.body(i);
            if (b.alive && b.type == b2_dynamicBody) pe.field -= double(b.charge) * dot(e_, Vec2{b.com.x, b.com.y});
        }
        return pe;
    }

private:
    const ElectricField& t_;
    Vec2 e_;
};
} // namespace

std::unique_ptr<ItemRuntime> ElectricField::createRuntime(const ItemDef& d, World& w, const RestoreState*) const
{
    return std::make_unique<ElectricRuntime>(d, *this, w);
}

KINETIC_DEFINE_ITEM(ElectricField)

} // namespace kn
