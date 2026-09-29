// Motor: a hinge that drives body A relative to body B (or the world), either
// at a target speed with a torque limit or with a constant torque.
#include "ConnectorItem.h"

namespace kn {

class MotorMechanism final : public ConnectorType {
public:
    MotorMechanism()
        : ConnectorType({QStringLiteral("mechanism.motor"), QStringLiteral("Motor"), QStringLiteral("Mechanisms"),
                         QStringLiteral("view-refresh"), QStringLiteral(":/icons/tool.svg"),
                         {QStringLiteral("engine"), QStringLiteral("drive"), QStringLiteral("rotor"), QStringLiteral("crank")},
                         QStringLiteral("Drives a body around a pivot: click the body at the axle, then the base or "
                                        "empty space.")})
    {
        addAttachment(false);
        auto& s = schema_;
        ref_ = s.size();
        s.num(QStringLiteral("referenceAngle"), QStringLiteral("Reference angle"), unit::deg, -100, 100, 0).hidden().rebuild();
        s.beginGroup(QStringLiteral("Motor"));
        mode_ = s.size();
        s.choice(QStringLiteral("mode"), QStringLiteral("Mode"), {QStringLiteral("Speed"), QStringLiteral("Torque")}, 0);
        speed_ = s.size();
        s.num(QStringLiteral("speed"), QStringLiteral("Speed (A relative to B)"), unit::radps, -200, 200, 3.14159, 3);
        maxTorque_ = s.size();
        s.num(QStringLiteral("maxTorque"), QStringLiteral("Max torque (speed mode)"), unit::Nm, 0, 1e8, 1e5, 1).log();
        torque_ = s.size();
        s.num(QStringLiteral("torque"), QStringLiteral("Torque (torque mode)"), unit::Nm, -1e7, 1e7, 500, 1);
        addAppearance(Color::fromRgb(0xd35400));
    }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        const Vec2 pivot = worldA(d, view);
        d.set(cidx_.anchorB, toAnchor(d.ref(cidx_.bodyB), pivot, view));
        Pose pa, pb;
        const double aA = d.ref(cidx_.bodyA) && view.pose(d.ref(cidx_.bodyA), pa) ? pa.angle() : 0.0;
        const double aB = d.ref(cidx_.bodyB) && view.pose(d.ref(cidx_.bodyB), pb) ? pb.angle() : 0.0;
        d.set(ref_, aA - aB);
    }

    bool hitTest(const ItemDef& d, const SceneView& view, Vec2 w, double tol) const override
    {
        return distance(worldA(d, view), w) <= 8.0 * tol;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;
    QList<Channel> channels() const override
    {
        return {{4, QStringLiteral("Motor torque"), unit::Nm}, {5, QStringLiteral("Angle"), unit::deg, kDegPerRad}};
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 p = worldA(d, ctx.view);
        const Color col = color(d, ctx);
        const double r = 12.0 / ctx.pxPerM;
        ctx.batch.disc(Layer::Front, p, r * 0.6, col.withAlpha(120), col, 1.5f);
        const double value = d.i(mode_) == 0 ? d.d(speed_) : d.d(torque_);
        const double dir = value >= 0 ? 1.0 : -1.0;
        // Spin angle for a visual cue when running.
        double spin = 0.0;
        Pose pa;
        if (d.ref(cidx_.bodyA) && ctx.view.pose(d.ref(cidx_.bodyA), pa)) spin = pa.angle();
        const double a0 = spin + 0.4, a1 = a0 + dir * 4.6;
        ctx.batch.arc(Layer::Front, p, r, a0, a1, lineWidth(ctx, 2.0f), col, 24);
        const Vec2 tip = p + Vec2{std::cos(a1), std::sin(a1)} * r;
        const Vec2 tan = Vec2{-std::sin(a1), std::cos(a1)} * dir;
        ctx.batch.arrow(Layer::Front, tip - tan * (5.0 / ctx.pxPerM), tip + tan * (3.0 / ctx.pxPerM), 2.0f, col, 7);
    }

    int ref_, mode_, speed_, maxTorque_, torque_;
};

namespace {
class MotorRuntime final : public ConnectorRuntime {
public:
    MotorRuntime(const ItemDef& d, const MotorMechanism& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        swapped_ = true; // Box2D body A is our base, so its motor speed is A relative to B
        b2RevoluteJointDef jd = b2DefaultRevoluteJointDef();
        jd.bodyIdA = bodyB(w);
        jd.bodyIdB = bodyA(w);
        jd.localAnchorA = lb_;
        jd.localAnchorB = la_;
        jd.referenceAngle = float(d.d(t.ref_));
        jd.collideConnected = d.b(t.cidx().collide);
        joint_ = b2CreateRevoluteJoint(w.id(), &jd);
        applyJointTuning(w);
        configure(d);
    }

    void configure(const ItemDef& d)
    {
        torqueMode_ = d.i(t_.mode_) == 1;
        torque_ = float(d.d(t_.torque_));
        b2RevoluteJoint_EnableMotor(joint_, !torqueMode_);
        b2RevoluteJoint_SetMotorSpeed(joint_, float(d.d(t_.speed_)));
        b2RevoluteJoint_SetMaxMotorTorque(joint_, float(d.d(t_.maxTorque_)));
        b2Joint_WakeBodies(joint_);
    }

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        const uint64_t live = propBit(t_.mode_) | propBit(t_.speed_) | propBit(t_.maxTorque_) | propBit(t_.torque_) |
                              propBit(t_.cidx().color);
        if (changed & ~live) return false;
        configure(d);
        return true;
    }

    void preStep(World& w) override
    {
        if (!torqueMode_) return;
        if (ia_ >= 0) w.addTorque(ia_, ForceSource::Motor, torque_);
        if (ib_ >= 0) w.addTorque(ib_, ForceSource::Motor, -torque_);
    }

    void postStep(World& w) override
    {
        const float motor = torqueMode_ ? 0.0f : b2RevoluteJoint_GetMotorTorque(joint_);
        recordJoint(w, ForceSource::Constraint, ForceSource::Constraint, motor);
        applied_ = torqueMode_ ? torque_ : motor;
        if (motor != 0.0f) {
            if (ia_ >= 0) w.record(ia_, ForceSource::Motor, {0, 0}, motor);
            if (ib_ >= 0) w.record(ib_, ForceSource::Motor, {0, 0}, -motor);
        }
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = applied_; // motor torque on A, N·m
        s.v[5] = b2RevoluteJoint_GetAngle(joint_);
        return true;
    }

private:
    const MotorMechanism& t_;
    bool torqueMode_ = false;
    float torque_ = 0, applied_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> MotorMechanism::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<MotorRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(MotorMechanism)

} // namespace kn
