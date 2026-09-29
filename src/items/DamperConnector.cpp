// Linear dashpot: F = -c dL/dt along the axis, no stiffness.
#include "ConnectorItem.h"

namespace kn {

class DamperConnector final : public ConnectorType {
public:
    DamperConnector()
        : ConnectorType({QStringLiteral("connector.damper"), QStringLiteral("Damper"), QStringLiteral("Connectors"),
                         QStringLiteral("format-connect-node"), QStringLiteral(":/icons/connector.svg"),
                         {QStringLiteral("dashpot"), QStringLiteral("shock absorber"), QStringLiteral("viscous")},
                         QStringLiteral("Viscous dashpot resisting the rate of change of distance.")})
    {
        addAttachment(true);
        schema_.at(cidx_.collide).hidden();
        schema_.beginGroup(QStringLiteral("Damper"));
        c_ = schema_.size();
        schema_.num(QStringLiteral("coefficient"), QStringLiteral("Damping coefficient c"), unit::damping, 0, 1e6, 50, 3)
            .log();
        addAppearance(Color::fromRgb(0x16a085));
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;
    QList<Channel> channels() const override { return {{4, QStringLiteral("Force (tension +)"), unit::N}}; }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 a = worldA(d, ctx.view), b = worldB(d, ctx.view);
        const Color col = color(d, ctx);
        const double len = distance(a, b);
        if (len < 1e-9) return;
        const Vec2 u = (b - a) / len, n = perp(u);
        const double w = std::min(0.12, 0.2 * len + 0.02);
        // Cylinder from A to the middle, piston rod from the middle to B.
        const Vec2 c0 = a + u * (0.15 * len), c1 = a + u * (0.6 * len);
        const float lw = lineWidth(ctx, 1.8f);
        ctx.batch.line(Layer::Front, a, c0, lw, col);
        ctx.batch.line(Layer::Front, c0 + n * w, c1 + n * w, lw, col);
        ctx.batch.line(Layer::Front, c0 - n * w, c1 - n * w, lw, col);
        ctx.batch.line(Layer::Front, c0 + n * w, c0 - n * w, lw, col);
        const Vec2 head = a + u * (0.45 * len);
        ctx.batch.line(Layer::Front, head + n * (w * 0.8), head - n * (w * 0.8), lw, col);
        ctx.batch.line(Layer::Front, head, b, lw, col);
    }

    int c_;
};

namespace {
class DamperRuntime final : public ConnectorRuntime {
public:
    DamperRuntime(const ItemDef& d, const DamperConnector& t, World& w) : ConnectorRuntime(d, t, w), t_(t), c_(d.d(t.c_)) {}

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        if (changed != propBit(t_.c_) && changed != propBit(t_.cidx().color) &&
            changed != (propBit(t_.c_) | propBit(t_.cidx().color)))
            return false;
        c_ = d.d(t_.c_);
        return true;
    }

    void preStep(World& w) override
    {
        const b2Vec2 pA = anchorA(w), pB = anchorB(w);
        const b2Vec2 d = b2Sub(pB, pA);
        const float len = b2Length(d);
        if (len < 1e-7f) return;
        const b2Vec2 u = b2MulSV(1.0f / len, d);
        const double rate = double(b2Dot(b2Sub(velocityB(w, pB), velocityA(w, pA)), u));
        force_ = float(-c_ * rate);
        if (force_ != 0.0f) applyPair(w, ForceSource::Damper, b2MulSV(force_, u), pA, pB);
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = -force_;
        return true;
    }

private:
    const DamperConnector& t_;
    double c_;
    float force_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> DamperConnector::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<DamperRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(DamperConnector)

} // namespace kn
