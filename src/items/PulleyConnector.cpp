// Ideal pulley: an inextensible rope from body A over two fixed wheels to body B,
//   |pA - gA| + ratio * |pB - gB| <= length.
// Box2D v3 has no pulley joint, so this is a force-level constraint solved each
// step: the tension is the force that makes the constraint's velocity error
// vanish by the end of the step, given the known external forces (gravity and
// forces applied earlier in the step). It is exact for Atwood-type systems.
#include "ConnectorItem.h"

#include "core/Geometry.h"

namespace kn {

class PulleyConnector final : public ConnectorType {
public:
    PulleyConnector()
        : ConnectorType({QStringLiteral("connector.pulley"), QStringLiteral("Pulley"), QStringLiteral("Connectors"),
                         QStringLiteral("games-config-board"), QStringLiteral(":/icons/connector.svg"),
                         {QStringLiteral("atwood"), QStringLiteral("block and tackle"), QStringLiteral("rope"),
                          QStringLiteral("wheel")},
                         QStringLiteral("Rope over two fixed pulley wheels; the ratio makes a block and tackle.")})
    {
        addAttachment(false);
        schema_.at(cidx_.collide).hidden();
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Pulley"));
        gA_ = s.size();
        s.vec(QStringLiteral("groundA"), QStringLiteral("Wheel A (world)"), unit::m, -10000, 10000, {0, 5});
        gB_ = s.size();
        s.vec(QStringLiteral("groundB"), QStringLiteral("Wheel B (world)"), unit::m, -10000, 10000, {1, 5});
        ratio_ = s.size();
        s.num(QStringLiteral("ratio"), QStringLiteral("Ratio"), unit::none, 0.1, 10, 1, 3)
            .tip(QStringLiteral("Rope length on side B counts this many times (mechanical advantage)."));
        len_ = s.size();
        s.num(QStringLiteral("length"), QStringLiteral("Total rope length"), unit::m, 0.01, 10000, 4, 3);
        addAppearance(Color::fromRgb(0xa0522d));
    }

    void extraPoints(const ItemDef&, std::vector<int>& v) const override { v = {gA_, gB_}; }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        const Vec2 a = worldA(d, view), b = worldB(d, view);
        const double top = std::max(a.y, b.y) + 2.0;
        d.set(gA_, Vec2{a.x, top});
        d.set(gB_, Vec2{b.x, top});
        d.set(len_, clampValue(schema_[len_], distance(a, d.v(gA_)) + d.d(ratio_) * distance(b, d.v(gB_))));
    }

    bool hitTest(const ItemDef& d, const SceneView& view, Vec2 w, double tol) const override;
    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 a = worldA(d, ctx.view), b = worldB(d, ctx.view);
        const Vec2 ga = d.v(gA_), gb = d.v(gB_);
        const Color col = color(d, ctx);
        const float lw = lineWidth(ctx, 2.0f);
        const double r = 0.12;
        ctx.batch.line(Layer::Front, a, ga, lw, col);
        ctx.batch.line(Layer::Front, b, gb, lw, col);
        ctx.batch.line(Layer::Front, ga + Vec2{0, r}, gb + Vec2{0, r}, lw, col);
        for (Vec2 g : {ga, gb}) {
            ctx.batch.disc(Layer::Front, g, r, ctx.theme.background, col, 2.0f);
            ctx.batch.disc(Layer::Front, g, 0.025, col, col, 0.0f);
            ctx.batch.line(Layer::Back, g, g + Vec2{0, 0.35}, 3.0f, ctx.theme.dim);
        }
        if (ctx.snap && ctx.selected) {
            static const QString t = QStringLiteral("T = "), n = QStringLiteral(" N");
            ctx.batch.valueLabel((ga + a) * 0.5, t, ctx.snap->v[4], n, 2, ctx.theme.text, 10, 0);
        }
    }

    int gA_, gB_, ratio_, len_;
};

bool PulleyConnector::hitTest(const ItemDef& d, const SceneView& view, Vec2 w, double tol) const
{
    const Vec2 a = worldA(d, view), b = worldB(d, view);
    const double t = tol * 2.0;
    return geom::distanceToSegment(w, a, d.v(gA_)) <= t || geom::distanceToSegment(w, b, d.v(gB_)) <= t ||
           geom::distanceToSegment(w, d.v(gA_), d.v(gB_)) <= t;
}

namespace {

class PulleyRuntime final : public ConnectorRuntime {
public:
    PulleyRuntime(const ItemDef& d, const PulleyConnector& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        configure(d);
    }

    void configure(const ItemDef& d)
    {
        const Vec2 ga = d.v(t_.gA_), gb = d.v(t_.gB_);
        gA_ = {float(ga.x), float(ga.y)};
        gB_ = {float(gb.x), float(gb.y)};
        ratio_ = d.d(t_.ratio_);
        length_ = d.d(t_.len_);
    }

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        const auto& ix = t_.cidx();
        if (changed & (propBit(ix.bodyA) | propBit(ix.bodyB) | propBit(ix.anchorA) | propBit(ix.anchorB))) return false;
        configure(d);
        return true;
    }

    struct Side {
        double invMass = 0, invI = 0;
        b2Vec2 u{0, 0}; // unit vector from the wheel to the anchor
        double rxu = 0; // lever arm of the rope about the centre of mass
        double len = 0;
        double cdot = 0; // rate of rope length change on this side
        double accel = 0; // external acceleration projected on the rope
    };

    Side side(const World& w, int bi, b2Vec2 p, b2Vec2 g, b2Vec2 vp) const
    {
        Side s;
        const b2Vec2 d = b2Sub(p, g);
        s.len = b2Length(d);
        s.u = s.len > 1e-9 ? b2MulSV(float(1.0 / s.len), d) : b2Vec2{0, -1};
        s.cdot = b2Dot(s.u, vp);
        if (bi < 0) return s;
        const BodyRec& b = w.body(bi);
        if (b.type != b2_dynamicBody) return s;
        s.invMass = b.invMass;
        s.invI = b.inertia > 0 ? 1.0 / b.inertia : 0.0;
        s.rxu = b2Cross(b2Sub(p, b.com), s.u);
        // Known external forces: gravity plus everything applied so far this step.
        const b2Vec2 g0 = {float(w.settings().gravity.x), float(w.settings().gravity.y)};
        double fx = 0, fy = 0, tq = 0;
        for (int i = 0; i < b.forceCount; ++i) {
            if (b.forces[i].source == uint8_t(ForceSource::Gravity)) continue; // applied by Box2D below
            fx += b.forces[i].fx;
            fy += b.forces[i].fy;
            tq += b.forces[i].torque;
        }
        // Contact forces are only known after the step; last step's are the best
        // estimate (this makes a body resting on the floor hold the rope steadily).
        for (int i = 0; i < b.lastForceCount; ++i) {
            const uint8_t src = b.lastForces[i].source;
            if (src != uint8_t(ForceSource::Normal) && src != uint8_t(ForceSource::Friction)) continue;
            fx += b.lastForces[i].fx;
            fy += b.lastForces[i].fy;
            tq += b.lastForces[i].torque;
        }
        const double ax = fx * s.invMass + double(g0.x) * b.gravityScale;
        const double ay = fy * s.invMass + double(g0.y) * b.gravityScale;
        s.accel = double(s.u.x) * ax + double(s.u.y) * ay + s.rxu * tq * s.invI;
        return s;
    }

    void preStep(World& w) override
    {
        const b2Vec2 pA = anchorA(w), pB = anchorB(w);
        const Side a = side(w, ia_, pA, gA_, velocityA(w, pA));
        const Side b = side(w, ib_, pB, gB_, velocityB(w, pB));
        const double r = ratio_;
        const double h = w.dt();
        const double C = a.len + r * b.len - length_;
        const double cdot = a.cdot + r * b.cdot;
        const double accel = a.accel + r * b.accel;
        const double K = a.invMass + a.rxu * a.rxu * a.invI + r * r * (b.invMass + b.rxu * b.rxu * b.invI);
        tension_ = 0.0;
        current_ = C + length_;
        if (K <= 0.0) return;
        // Target rate at the end of the step: remove stretch gently (Baumgarte),
        // or allow a slack rope to close exactly its gap but no further.
        const double target = C > 0.0 ? -0.2 * C / h : -C / h;
        const double predicted = cdot + h * accel;
        const double T = (predicted - target) / (h * K);
        if (T <= 0.0) return; // slack
        tension_ = T;
        if (ia_ >= 0) w.addForce(ia_, ForceSource::Tension, b2MulSV(float(-T), a.u), pA);
        if (ib_ >= 0) w.addForce(ib_, ForceSource::Tension, b2MulSV(float(-T * r), b.u), pB);
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = float(tension_);
        s.v[5] = float(current_);
        s.v[6] = float(tension_ * ratio_);
        return true;
    }

private:
    const PulleyConnector& t_;
    b2Vec2 gA_{0, 0}, gB_{0, 0};
    double ratio_ = 1, length_ = 1;
    double tension_ = 0, current_ = 0;
};

} // namespace

std::unique_ptr<ItemRuntime> PulleyConnector::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<PulleyRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(PulleyConnector)

} // namespace kn
