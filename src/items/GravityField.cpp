// Gravity zone: a uniform acceleration or a point attractor (inverse-square,
// softened) inside a region, optionally replacing the world's gravity there.
#include "FieldItem.h"

namespace kn {

class GravityField final : public FieldType {
public:
    enum Mode : int { Uniform, Point };

    GravityField()
        : FieldType({QStringLiteral("field.gravity"), QStringLiteral("Gravity zone"), QStringLiteral("Fields"),
                     QStringLiteral("globe"), QStringLiteral(":/icons/field.svg"),
                     {QStringLiteral("planet"), QStringLiteral("attractor"), QStringLiteral("moon"),
                      QStringLiteral("inverse square"), QStringLiteral("orbit")},
                     QStringLiteral("Uniform gravity or a point attractor inside a region.")})
    {
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Gravity"));
        mode_ = s.size();
        s.choice(QStringLiteral("mode"), QStringLiteral("Mode"), {QStringLiteral("Uniform"), QStringLiteral("Point attractor")},
                 Uniform);
        acc_ = s.size();
        s.vec(QStringLiteral("acceleration"), QStringLiteral("Acceleration"), unit::mps2, -1000, 1000, {0.0, -1.62})
            .tip(QStringLiteral("Uniform mode: acceleration in world axes."));
        g1_ = s.size();
        s.num(QStringLiteral("strength"), QStringLiteral("Strength at 1 m"), unit::mps2, 0, 1e7, 50, 3)
            .log()
            .tip(QStringLiteral("Point mode: acceleration at 1 m from the centre, falling off as 1/r² "
                                "(softened by the world's softening length)."));
        replace_ = s.size();
        s.boolean(QStringLiteral("replaceGravity"), QStringLiteral("Replace world gravity"), true)
            .tip(QStringLiteral("Cancel the world's gravity inside the region."));
        addRegion(Rectangle, 6, 4, 3);
        addColour(Color::fromRgb(0x6c5ce7, 70));
    }

    bool addField(const ItemDef& d, const Pose& pose, FieldKind k, Vec2 p, Vec2& inout) const override
    {
        if (k != FieldKind::Gravity || !contains(d, pose, p)) return false;
        if (d.i(mode_) == Uniform) {
            inout += d.v(acc_);
        } else {
            const Vec2 r = p - pose.p;
            const double d2 = dot(r, r) + 0.0025;
            inout -= r * (d.d(g1_) / (d2 * std::sqrt(d2)));
        }
        return d.b(replace_);
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Color c = drawRegion(d, ctx);
        const Color ink = c.withAlpha(230);
        if (d.i(mode_) == Uniform) {
            const Vec2 a = d.v(acc_);
            const double m = length(a);
            if (m > 0) ctx.batch.arrow(Layer::Front, ctx.pose.p, ctx.pose.p + a * (ctx.px(40) / m), 2.0f, ink);
            ctx.batch.valueLabel(ctx.pose.p, QStringLiteral("g = "), m, QStringLiteral(" m/s²"), 2, ctx.theme.text, 12, 14);
        } else {
            for (int i = 0; i < 8; ++i) {
                const double t = i * kPi / 4;
                const Vec2 u{std::cos(t), std::sin(t)};
                ctx.batch.arrow(Layer::Front, ctx.pose.p + u * ctx.px(34), ctx.pose.p + u * ctx.px(14), 1.5f, ink, 7);
            }
            ctx.batch.disc(Layer::Front, ctx.pose.p, ctx.px(6), ink, ink);
        }
    }

    int mode_, acc_, g1_, replace_;
};

namespace {
class GravityRuntime final : public FieldRuntime {
public:
    GravityRuntime(const ItemDef& d, const GravityField& t, World& w) : FieldRuntime(d, t, w), t_(t) { edited(d); }

    void edited(const ItemDef& d) override
    {
        mode_ = int(d.i(t_.mode_));
        acc_ = d.v(t_.acc_);
        g1_ = d.d(t_.g1_);
        replace_ = d.b(t_.replace_);
    }

    b2Vec2 acceleration(const World& w, b2Vec2 p) const
    {
        b2Vec2 a{0, 0};
        if (mode_ == GravityField::Uniform) {
            a = {float(acc_.x), float(acc_.y)};
        } else {
            const double rx = p.x - pose_.p.x, ry = p.y - pose_.p.y;
            const double eps = w.settings().coulombSoftening;
            const double d2 = rx * rx + ry * ry + eps * eps;
            const double k = -g1_ / (d2 * std::sqrt(d2));
            a = {float(k * rx), float(k * ry)};
        }
        if (replace_) a = b2Sub(a, {float(w.settings().gravity.x), float(w.settings().gravity.y)});
        return a;
    }

    void preStep(World& w) override
    {
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            if (!affects(w, i)) continue;
            const BodyRec& b = w.body(i);
            const b2Vec2 p = mode_ == GravityField::Point ? w.predictPoint(i, b.com) : b.com;
            w.addFieldForce(i, ForceSource::Field, b2MulSV(b.mass * b.gravityScale, acceleration(w, p)));
        }
    }

    // Counted only for whole-world fields: a region boundary would make the
    // potential discontinuous.
    PotentialEnergy potentialEnergy(const World& w) const override
    {
        PotentialEnergy e;
        if (region_ != FieldType::WholeWorld) return e;
        const Vec2 g = w.settings().gravity;
        const double eps = w.settings().coulombSoftening;
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            const BodyRec& b = w.body(i);
            if (!b.alive || b.type != b2_dynamicBody) continue;
            const double m = double(b.mass) * b.gravityScale;
            const Vec2 c{b.com.x, b.com.y};
            if (mode_ == GravityField::Uniform)
                e.field -= m * dot(acc_, c);
            else
                e.field -= m * g1_ / std::sqrt(dot(c - pose_.p, c - pose_.p) + eps * eps);
            if (replace_) e.field += m * dot(g, c);
        }
        return e;
    }

private:
    const GravityField& t_;
    int mode_ = 0;
    Vec2 acc_;
    double g1_ = 0;
    bool replace_ = false;
};
} // namespace

std::unique_ptr<ItemRuntime> GravityField::createRuntime(const ItemDef& d, World& w, const RestoreState*) const
{
    return std::make_unique<GravityRuntime>(d, *this, w);
}

KINETIC_DEFINE_ITEM(GravityField)

} // namespace kn
