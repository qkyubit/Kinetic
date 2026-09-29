// Linear spring with optional damping: F = -k (L - L0) - c dL/dt along the axis.
// Integrated as an explicit force (symplectic with Box2D's integrator), so the
// elastic energy ½k(L-L0)² is exact; see docs for the stiffness stability limit.
#include "ConnectorItem.h"

namespace kn {

class SpringConnector final : public ConnectorType {
public:
    SpringConnector()
        : ConnectorType({QStringLiteral("connector.spring"), QStringLiteral("Spring"), QStringLiteral("Connectors"),
                         QStringLiteral("draw-spiral"), QStringLiteral(":/icons/connector.svg"),
                         {QStringLiteral("hooke"), QStringLiteral("elastic"), QStringLiteral("oscillator")},
                         QStringLiteral("Hookean spring with optional damping between two bodies or a body and a "
                                        "point.")})
    {
        addAttachment(true);
        schema_.at(cidx_.collide).hidden(); // a force, not a joint: bodies always collide
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Spring"));
        k_ = s.size();
        s.num(QStringLiteral("stiffness"), QStringLiteral("Stiffness k"), unit::stiffness, 0.01, 1e7, 500, 2).log();
        rest_ = s.size();
        s.num(QStringLiteral("restLength"), QStringLiteral("Rest length"), unit::m, 0, 1000, 1, 3);
        mode_ = s.size();
        s.choice(QStringLiteral("dampingMode"), QStringLiteral("Damping given as"),
                 {QStringLiteral("Damping ratio"), QStringLiteral("Coefficient")}, 0);
        zeta_ = s.size();
        s.num(QStringLiteral("dampingRatio"), QStringLiteral("Damping ratio ζ"), unit::none, 0, 10, 0, 3)
            .tip(QStringLiteral("1 = critical damping, using the reduced mass of the two bodies."));
        c_ = s.size();
        s.num(QStringLiteral("dampingCoefficient"), QStringLiteral("Damping coefficient c"), unit::damping, 0, 1e6, 0, 3)
            .log();
        s.beginGroup(QStringLiteral("Appearance"));
        coils_ = s.size();
        s.integer(QStringLiteral("coils"), QStringLiteral("Coils"), 2, 40, 10);
        width_ = s.size();
        s.num(QStringLiteral("width"), QStringLiteral("Drawn width"), unit::m, 0.02, 5, 0.25, 2);
        addAppearance(Color::fromRgb(0x27ae60));
    }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        d.set(rest_, clampValue(schema_[rest_], distance(worldA(d, view), worldB(d, view))));
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 a = worldA(d, ctx.view), b = worldB(d, ctx.view);
        Color col = color(d, ctx);
        if (ctx.snap && ctx.snap->v[7] > 0.5f && !ctx.selected) col = Color::fromRgb(0xda4453); // unstable
        drawZigzag(ctx.batch, Layer::Front, a, b, int(d.i(coils_)), 0.5 * d.d(width_), lineWidth(ctx, 1.8f), col);
        ctx.batch.disc(Layer::Front, a, 3.0 / ctx.pxPerM, col, col, 0);
        ctx.batch.disc(Layer::Front, b, 3.0 / ctx.pxPerM, col, col, 0);
    }

    int k_, rest_, mode_, zeta_, c_, coils_, width_;
};

namespace {
class SpringRuntime final : public ConnectorRuntime {
public:
    SpringRuntime(const ItemDef& d, const SpringConnector& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        configure(d);
    }

    void configure(const ItemDef& d)
    {
        k_ = d.d(t_.k_);
        rest_ = d.d(t_.rest_);
        useRatio_ = d.i(t_.mode_) == 0;
        zeta_ = d.d(t_.zeta_);
        c_ = d.d(t_.c_);
    }

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        const auto& ix = t_.cidx();
        if (changed & (propBit(ix.bodyA) | propBit(ix.bodyB) | propBit(ix.anchorA) | propBit(ix.anchorB) |
                       propBit(ix.collide) | (1ull << 63)))
            return false;
        configure(d);
        return true;
    }

    double reducedMass(const World& w) const
    {
        const double ma = ia_ >= 0 && w.body(ia_).type == b2_dynamicBody ? w.body(ia_).mass : 0.0;
        const double mb = ib_ >= 0 && w.body(ib_).type == b2_dynamicBody ? w.body(ib_).mass : 0.0;
        if (ma > 0 && mb > 0) return ma * mb / (ma + mb);
        return std::max(ma, mb);
    }

    void preStep(World& w) override
    {
        const b2Vec2 pA = anchorA(w), pB = anchorB(w);
        // Elastic force from the predicted anchor positions (see World::predictAlpha).
        const b2Vec2 qA = ia_ >= 0 ? w.predictPoint(ia_, pA) : pA;
        const b2Vec2 qB = ib_ >= 0 ? w.predictPoint(ib_, pB) : pB;
        const b2Vec2 d = b2Sub(qB, qA);
        const float len = b2Length(d);
        length_ = b2Distance(pA, pB);
        if (len < 1e-7f) {
            tension_ = 0;
            return;
        }
        const b2Vec2 u = b2MulSV(1.0f / len, d);
        const double ext = double(len) - rest_;
        const double rate = double(b2Dot(b2Sub(velocityB(w, pB), velocityA(w, pA)), u));
        const double m = reducedMass(w);
        const double c = useRatio_ ? 2.0 * zeta_ * std::sqrt(k_ * m) : c_;
        const double fs = -k_ * ext;  // along u, acting on B
        const double fd = -c * rate;
        tension_ = float(-(fs + fd));
        if (fs != 0.0) applyPair(w, ForceSource::Spring, b2MulSV(float(fs), u), pA, pB);
        if (fd != 0.0) applyPair(w, ForceSource::Damper, b2MulSV(float(fd), u), pA, pB);
        // Explicit integration is stable while omega * dt stays below ~2.
        const double dt = w.dt();
        unstable_ = m > 0.0 && k_ / m * dt * dt > 3.0;
    }

    PotentialEnergy potentialEnergy(const World& w) const override
    {
        const double len = b2Distance(anchorA(w), anchorB(w));
        return {0.5 * k_ * (len - rest_) * (len - rest_), 0.0};
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = tension_;
        s.v[5] = length_;
        s.v[7] = unstable_ ? 1.0f : 0.0f;
        return true;
    }

private:
    const SpringConnector& t_;
    double k_ = 0, rest_ = 0, zeta_ = 0, c_ = 0;
    bool useRatio_ = true;
    float tension_ = 0, length_ = 0;
    bool unstable_ = false;
};
} // namespace

std::unique_ptr<ItemRuntime> SpringConnector::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<SpringRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(SpringConnector)

} // namespace kn
