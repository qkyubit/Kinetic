// Gear coupling between the rotations of two bodies:
//   thetaA + s * ratio * thetaB = const   (s = +1 meshing gears, -1 belt/chain)
// Box2D v3 has no gear joint, so this is a torque-level constraint solved each
// step from the known external torques, with Baumgarte correction of the
// accumulated angle error. Pair it with hinges at the gear centres.
#include "ConnectorItem.h"

namespace kn {

class GearConnector final : public ConnectorType {
public:
    GearConnector()
        : ConnectorType({QStringLiteral("connector.gear"), QStringLiteral("Gear"), QStringLiteral("Connectors"),
                         QStringLiteral("games-config-options"), QStringLiteral(":/icons/connector.svg"),
                         {QStringLiteral("cog"), QStringLiteral("transmission"), QStringLiteral("belt"),
                          QStringLiteral("ratio")},
                         QStringLiteral("Couples the rotation of two bodies with a gear ratio (hinge them first).")})
    {
        addAttachment(false);
        for (int i : {cidx_.anchorA, cidx_.anchorB, cidx_.collide}) schema_.at(i).hidden();
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Gear"));
        ratio_ = s.size();
        s.num(QStringLiteral("ratio"), QStringLiteral("Ratio (turns of A per turn of B)"), unit::none, 0.05, 20, 1, 3)
            .log();
        dir_ = s.size();
        s.choice(QStringLiteral("direction"), QStringLiteral("Direction"),
                 {QStringLiteral("Opposite (meshing gears)"), QStringLiteral("Same (belt or chain)")}, 0);
        addAppearance(Color::fromRgb(0xb7950b));
    }

    Vec2 centre(const ItemDef& d, int bodyProp, const SceneView& view) const
    {
        Pose p;
        return d.ref(bodyProp) && view.pose(d.ref(bodyProp), p) ? p.p : Vec2{};
    }

    bool hitTest(const ItemDef& d, const SceneView& view, Vec2 w, double tol) const override
    {
        const Vec2 a = centre(d, cidx_.bodyA, view), b = centre(d, cidx_.bodyB, view);
        return distance(w, a) <= 6 * tol || distance(w, b) <= 6 * tol;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;
    QList<Channel> channels() const override
    {
        return {{4, QStringLiteral("Coupling torque"), unit::Nm},
                {5, QStringLiteral("Angle error"), unit::deg, kDegPerRad}};
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 a = centre(d, cidx_.bodyA, ctx.view), b = centre(d, cidx_.bodyB, ctx.view);
        const double dist = distance(a, b);
        const Color col = color(d, ctx);
        const double r = d.d(ratio_);
        const bool meshing = d.i(dir_) == 0;
        // Pitch circles: meshing gears touch (rA + rB = D); belt pulleys share a tangent.
        const double rA = meshing ? dist / (1.0 + r) : std::min(0.3, dist * 0.25);
        const double rB = meshing ? dist - rA : rA * r;
        Pose pa, pb;
        const double angA = d.ref(cidx_.bodyA) && ctx.view.pose(d.ref(cidx_.bodyA), pa) ? pa.angle() : 0.0;
        const double angB = d.ref(cidx_.bodyB) && ctx.view.pose(d.ref(cidx_.bodyB), pb) ? pb.angle() : 0.0;
        auto teeth = [&](Vec2 c, double rad, double ang) {
            ctx.batch.circleOutline(Layer::Front, c, rad, col, lineWidth(ctx, 1.5f));
            const int n = std::clamp(int(rad * 40.0), 6, 60);
            for (int i = 0; i < n; ++i) {
                const double t = ang + kTwoPi * i / n;
                const Vec2 dir{std::cos(t), std::sin(t)};
                ctx.batch.line(Layer::Front, c + dir * rad, c + dir * (rad + 4.0 / ctx.pxPerM), 1.5f, col);
            }
        };
        if (dist < 1e-6) return;
        teeth(a, rA, angA);
        teeth(b, rB, angB);
        if (!meshing) {
            const Vec2 n = perp(normalized(b - a));
            ctx.batch.line(Layer::Front, a + n * rA, b + n * rB, 1.5f, col);
            ctx.batch.line(Layer::Front, a - n * rA, b - n * rB, 1.5f, col);
        }
    }

    int ratio_, dir_;
};

namespace {

class GearRuntime final : public ConnectorRuntime {
public:
    GearRuntime(const ItemDef& d, const GearConnector& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        configure(d);
        prevA_ = angleOf(w, ia_);
        prevB_ = angleOf(w, ib_);
        thetaA_ = prevA_;
        thetaB_ = prevB_;
        c0_ = thetaA_ + sign_ * ratio_ * thetaB_;
    }

    void configure(const ItemDef& d)
    {
        ratio_ = d.d(t_.ratio_);
        sign_ = d.i(t_.dir_) == 0 ? 1.0 : -1.0;
    }

    static double angleOf(const World& w, int bi)
    {
        if (bi < 0) return 0.0;
        const b2Rot q = w.body(bi).rot;
        return std::atan2(double(q.s), double(q.c));
    }

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        const auto& ix = t_.cidx();
        if (changed & (propBit(ix.bodyA) | propBit(ix.bodyB))) return false;
        configure(d);
        c0_ = thetaA_ + sign_ * ratio_ * thetaB_; // new ratio: couple from the current angles
        return true;
    }

    void track(const World& w)
    {
        const double a = angleOf(w, ia_), b = angleOf(w, ib_);
        thetaA_ += wrapAngle(a - prevA_);
        thetaB_ += wrapAngle(b - prevB_);
        prevA_ = a;
        prevB_ = b;
    }

    static double externalTorque(const BodyRec& b)
    {
        double t = 0;
        for (int i = 0; i < b.forceCount; ++i) t += b.forces[i].torque;
        for (int i = 0; i < b.lastForceCount; ++i) {
            const uint8_t src = b.lastForces[i].source;
            if (src == uint8_t(ForceSource::Normal) || src == uint8_t(ForceSource::Friction)) t += b.lastForces[i].torque;
        }
        return t;
    }

    void preStep(World& w) override
    {
        track(w);
        const double h = w.dt();
        double invIA = 0, invIB = 0, wA = 0, wB = 0, alphaA = 0, alphaB = 0;
        if (ia_ >= 0 && w.body(ia_).type == b2_dynamicBody && w.body(ia_).inertia > 0) {
            const BodyRec& b = w.body(ia_);
            invIA = 1.0 / b.inertia;
            wA = b.w;
            alphaA = externalTorque(b) * invIA;
        } else if (ia_ >= 0) {
            wA = w.body(ia_).w;
        }
        if (ib_ >= 0 && w.body(ib_).type == b2_dynamicBody && w.body(ib_).inertia > 0) {
            const BodyRec& b = w.body(ib_);
            invIB = 1.0 / b.inertia;
            wB = b.w;
            alphaB = externalTorque(b) * invIB;
        } else if (ib_ >= 0) {
            wB = w.body(ib_).w;
        }
        const double sr = sign_ * ratio_;
        const double K = invIA + sr * sr * invIB;
        torque_ = 0.0;
        if (K <= 0.0) return;
        const double C = thetaA_ + sr * thetaB_ - c0_;
        const double predicted = wA + sr * wB + h * (alphaA + sr * alphaB);
        const double lambda = -(predicted + 0.2 * C / h) / (h * K);
        torque_ = lambda;
        if (ia_ >= 0) w.addTorque(ia_, ForceSource::Constraint, float(lambda));
        if (ib_ >= 0) w.addTorque(ib_, ForceSource::Constraint, float(sr * lambda));
    }

    void saveState(std::vector<double>& out) const override { out = {thetaA_, thetaB_, prevA_, prevB_, c0_}; }
    void loadState(const std::vector<double>& s) override
    {
        if (s.size() != 5) return;
        thetaA_ = s[0];
        thetaB_ = s[1];
        prevA_ = s[2];
        prevB_ = s[3];
        c0_ = s[4];
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = float(torque_);
        s.v[5] = float(thetaA_ + sign_ * ratio_ * thetaB_ - c0_); // constraint error, rad
        return true;
    }

private:
    const GearConnector& t_;
    double ratio_ = 1, sign_ = 1;
    double thetaA_ = 0, thetaB_ = 0, prevA_ = 0, prevB_ = 0, c0_ = 0;
    double torque_ = 0;
};

} // namespace

std::unique_ptr<ItemRuntime> GearConnector::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<GearRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(GearConnector)

} // namespace kn
