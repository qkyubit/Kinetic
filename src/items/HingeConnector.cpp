// Pin joint (revolute) with optional angle limits and motor. The pivot is the
// first click; body A rotates relative to body B (or the world).
#include "ConnectorItem.h"

namespace kn {

class HingeConnector final : public ConnectorType {
public:
    HingeConnector()
        : ConnectorType({QStringLiteral("connector.hinge"), QStringLiteral("Hinge"), QStringLiteral("Connectors"),
                         QStringLiteral("anchor"), QStringLiteral(":/icons/connector.svg"),
                         {QStringLiteral("pivot"), QStringLiteral("revolute"), QStringLiteral("pin"), QStringLiteral("axle")},
                         QStringLiteral("Pivot: click the body at the pivot point, then the second body or empty space.")})
    {
        addAttachment(false);
        auto& s = schema_;
        ref_ = s.size();
        s.num(QStringLiteral("referenceAngle"), QStringLiteral("Reference angle"), unit::deg, -100, 100, 0).hidden().rebuild();
        s.beginGroup(QStringLiteral("Limits"));
        limit_ = s.size();
        s.boolean(QStringLiteral("enableLimit"), QStringLiteral("Limit rotation"), false);
        lower_ = s.size();
        s.angle(QStringLiteral("lowerAngle"), QStringLiteral("Lower angle"), -360, 360, -45);
        upper_ = s.size();
        s.angle(QStringLiteral("upperAngle"), QStringLiteral("Upper angle"), -360, 360, 45);
        s.beginGroup(QStringLiteral("Motor"));
        motor_ = s.size();
        s.boolean(QStringLiteral("enableMotor"), QStringLiteral("Motor"), false);
        speed_ = s.size();
        s.num(QStringLiteral("motorSpeed"), QStringLiteral("Speed (A relative to B)"), unit::radps, -200, 200, 1, 3);
        torque_ = s.size();
        s.num(QStringLiteral("maxMotorTorque"), QStringLiteral("Max torque"), unit::Nm, 0, 1e8, 1000, 1).log();
        addAppearance(Color::fromRgb(0x95a5a6));
    }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        // Both anchors sit on the pivot (the first click).
        const Vec2 pivot = worldA(d, view);
        d.set(cidx_.anchorB, toAnchor(d.ref(cidx_.bodyB), pivot, view));
        Pose pa, pb;
        const double aA = d.ref(cidx_.bodyA) && view.pose(d.ref(cidx_.bodyA), pa) ? pa.angle() : 0.0;
        const double aB = d.ref(cidx_.bodyB) && view.pose(d.ref(cidx_.bodyB), pb) ? pb.angle() : 0.0;
        d.set(ref_, aA - aB);
    }

    bool hitTest(const ItemDef& d, const SceneView& view, Vec2 w, double tol) const override
    {
        return distance(worldA(d, view), w) <= 6.0 * tol;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;
    QList<Channel> channels() const override
    {
        return {{4, QStringLiteral("Reaction force"), unit::N},
                {5, QStringLiteral("Joint angle"), unit::deg, kDegPerRad},
                {6, QStringLiteral("Motor torque"), unit::Nm}};
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 p = worldA(d, ctx.view);
        const Color col = color(d, ctx);
        drawPin(ctx.batch, p, ctx.pxPerM, ctx.theme.background, col);
        const double r = 10.0 / ctx.pxPerM;
        if (d.b(motor_)) {
            const double dir = d.d(speed_) >= 0 ? 1.0 : -1.0;
            ctx.batch.arc(Layer::Front, p, r, 0.3, 0.3 + dir * 4.5, 1.5f, col, 20);
            const double e = 0.3 + dir * 4.5;
            const Vec2 tip = p + Vec2{std::cos(e), std::sin(e)} * r;
            const Vec2 tan = Vec2{-std::sin(e), std::cos(e)} * dir;
            ctx.batch.arrow(Layer::Front, tip - tan * (4.0 / ctx.pxPerM), tip + tan * (2.0 / ctx.pxPerM), 1.5f, col, 6);
        }
        if (d.b(limit_) && ctx.selected) {
            Pose pb;
            const double base = d.ref(cidx_.bodyB) && ctx.view.pose(d.ref(cidx_.bodyB), pb) ? pb.angle() : 0.0;
            ctx.batch.arc(Layer::Overlay, p, r * 2.0, base + d.d(lower_), base + d.d(upper_), 1.0f, col, 24);
        }
    }

    int ref_, limit_, lower_, upper_, motor_, speed_, torque_;
};

namespace {
class HingeRuntime final : public ConnectorRuntime {
public:
    HingeRuntime(const ItemDef& d, const HingeConnector& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        // Box2D body A is our B (the base) so the motor speed is A relative to B.
        swapped_ = true;
        b2RevoluteJointDef jd = b2DefaultRevoluteJointDef();
        jd.bodyIdA = bodyB(w);
        jd.bodyIdB = bodyA(w);
        jd.localAnchorA = lb_;
        jd.localAnchorB = la_;
        jd.referenceAngle = float(d.d(t.ref_));
        jd.collideConnected = d.b(t.cidx().collide);
        configure(jd, d);
        joint_ = b2CreateRevoluteJoint(w.id(), &jd);
        applyJointTuning(w);
    }

    void configure(b2RevoluteJointDef& jd, const ItemDef& d) const
    {
        jd.enableLimit = d.b(t_.limit_);
        jd.lowerAngle = float(std::min(d.d(t_.lower_), d.d(t_.upper_)));
        jd.upperAngle = float(std::max(d.d(t_.lower_), d.d(t_.upper_)));
        jd.enableMotor = d.b(t_.motor_);
        jd.motorSpeed = float(d.d(t_.speed_));
        jd.maxMotorTorque = float(d.d(t_.torque_));
    }

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        const uint64_t live = propBit(t_.limit_) | propBit(t_.lower_) | propBit(t_.upper_) | propBit(t_.motor_) |
                              propBit(t_.speed_) | propBit(t_.torque_) | propBit(t_.cidx().color);
        if (changed & ~live) return false;
        b2RevoluteJointDef jd = b2DefaultRevoluteJointDef();
        configure(jd, d);
        b2RevoluteJoint_EnableLimit(joint_, jd.enableLimit);
        b2RevoluteJoint_SetLimits(joint_, jd.lowerAngle, jd.upperAngle);
        b2RevoluteJoint_EnableMotor(joint_, jd.enableMotor);
        b2RevoluteJoint_SetMotorSpeed(joint_, jd.motorSpeed);
        b2RevoluteJoint_SetMaxMotorTorque(joint_, jd.maxMotorTorque);
        b2Joint_WakeBodies(joint_);
        return true;
    }

    void postStep(World& w) override
    {
        const float motor = b2RevoluteJoint_IsMotorEnabled(joint_) ? b2RevoluteJoint_GetMotorTorque(joint_) : 0.0f;
        recordJoint(w, ForceSource::Constraint, ForceSource::Constraint, motor);
        motorTorque_ = motor;
        if (motor != 0.0f) {
            if (ia_ >= 0) w.record(ia_, ForceSource::Motor, {0, 0}, motor);
            if (ib_ >= 0) w.record(ib_, ForceSource::Motor, {0, 0}, -motor);
        }
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = b2Length(b2Joint_GetConstraintForce(joint_));
        s.v[5] = b2RevoluteJoint_GetAngle(joint_);
        s.v[6] = motorTorque_;
        return true;
    }

private:
    const HingeConnector& t_;
    float motorTorque_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> HingeConnector::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<HingeRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(HingeConnector)

} // namespace kn
