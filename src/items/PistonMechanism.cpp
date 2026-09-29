// Linear actuator: a prismatic joint along the line from anchor A to anchor B,
// driven by a force-limited motor through its stroke. Modes: oscillate between
// the ends, extend and hold, or retract and hold.
#include "ConnectorItem.h"

namespace kn {

class PistonMechanism final : public ConnectorType {
public:
    PistonMechanism()
        : ConnectorType({QStringLiteral("mechanism.piston"), QStringLiteral("Piston"), QStringLiteral("Mechanisms"),
                         QStringLiteral("transform-move-vertical"), QStringLiteral(":/icons/tool.svg"),
                         {QStringLiteral("actuator"), QStringLiteral("hydraulic"), QStringLiteral("cylinder"),
                          QStringLiteral("linear motor")},
                         QStringLiteral("Linear actuator with a stroke and force limit between two bodies or a body "
                                        "and a point.")})
    {
        addAttachment(false);
        auto& s = schema_;
        ref_ = s.size();
        s.num(QStringLiteral("referenceAngle"), QStringLiteral("Reference angle"), unit::deg, -100, 100, 0).hidden().rebuild();
        base_ = s.size();
        s.num(QStringLiteral("baseLength"), QStringLiteral("Retracted length"), unit::m, 0.01, 1000, 1, 3).rebuild();
        s.beginGroup(QStringLiteral("Piston"));
        mode_ = s.size();
        s.choice(QStringLiteral("mode"), QStringLiteral("Mode"),
                 {QStringLiteral("Oscillate"), QStringLiteral("Extend"), QStringLiteral("Retract")}, 0);
        stroke_ = s.size();
        s.num(QStringLiteral("stroke"), QStringLiteral("Stroke"), unit::m, 0.01, 100, 1, 3);
        speed_ = s.size();
        s.num(QStringLiteral("speed"), QStringLiteral("Speed"), unit::mps, 0.01, 100, 1, 3);
        force_ = s.size();
        s.num(QStringLiteral("maxForce"), QStringLiteral("Max force"), unit::N, 0, 1e8, 5e4, 1).log();
        addAppearance(Color::fromRgb(0x5d6d7e));
    }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        d.set(base_, clampValue(schema_[base_], distance(worldA(d, view), worldB(d, view))));
        Pose pa, pb;
        const double aA = d.ref(cidx_.bodyA) && view.pose(d.ref(cidx_.bodyA), pa) ? pa.angle() : 0.0;
        const double aB = d.ref(cidx_.bodyB) && view.pose(d.ref(cidx_.bodyB), pb) ? pb.angle() : 0.0;
        d.set(ref_, aB - aA);
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;
    QList<Channel> channels() const override
    {
        return {{4, QStringLiteral("Motor force"), unit::N}, {5, QStringLiteral("Extension"), unit::m}};
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 a = worldA(d, ctx.view), b = worldB(d, ctx.view);
        const double len = distance(a, b);
        if (len < 1e-9) return;
        const Vec2 u = (b - a) / len, n = perp(u);
        const Color col = color(d, ctx);
        const double w = std::max(0.08, 7.0 / ctx.pxPerM);
        const double barrel = std::min(d.d(base_), len) * 0.9;
        const Vec2 c1 = a + u * barrel;
        const Vec2 quad[4] = {a + n * w, c1 + n * w, c1 - n * w, a - n * w};
        ctx.batch.convexFill(Layer::Front, quad, 4, col.withAlpha(150));
        ctx.batch.polyline(Layer::Front, quad, 4, true, lineWidth(ctx, 1.5f), col);
        ctx.batch.line(Layer::Front, c1 - u * (w * 0.5), b, lineWidth(ctx, 3.0f), col);
        drawPin(ctx.batch, a, ctx.pxPerM, ctx.theme.background, col);
        drawPin(ctx.batch, b, ctx.pxPerM, ctx.theme.background, col);
    }

    int ref_, base_, mode_, stroke_, speed_, force_;
};

namespace {
class PistonRuntime final : public ConnectorRuntime {
public:
    PistonRuntime(const ItemDef& d, const PistonMechanism& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        const b2Vec2 pA = anchorA(w), pB = anchorB(w);
        b2Vec2 axisWorld = b2Normalize(b2Sub(pB, pA));
        if (b2Length(axisWorld) < 0.5f) axisWorld = {1, 0};
        const b2Rot qa = ia_ >= 0 ? w.body(ia_).rot : b2Rot{1, 0};
        b2PrismaticJointDef jd = b2DefaultPrismaticJointDef();
        jd.bodyIdA = bodyA(w);
        jd.bodyIdB = bodyB(w);
        jd.localAnchorA = la_;
        jd.localAnchorB = lb_;
        jd.localAxisA = b2InvRotateVector(qa, axisWorld);
        jd.referenceAngle = float(d.d(t.ref_));
        jd.enableLimit = true;
        jd.enableMotor = true;
        jd.collideConnected = d.b(t.cidx().collide);
        base_ = float(d.d(t.base_));
        configure(jd, d);
        joint_ = b2CreatePrismaticJoint(w.id(), &jd);
        applyJointTuning(w);
    }

    void configure(b2PrismaticJointDef& jd, const ItemDef& d)
    {
        mode_ = int(d.i(t_.mode_));
        stroke_ = float(d.d(t_.stroke_));
        speed_ = float(d.d(t_.speed_));
        jd.lowerTranslation = base_;
        jd.upperTranslation = base_ + stroke_;
        jd.maxMotorForce = float(d.d(t_.force_));
        if (mode_ == 1) dir_ = 1;
        if (mode_ == 2) dir_ = -1;
        jd.motorSpeed = float(dir_) * speed_;
    }

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        const uint64_t live = propBit(t_.mode_) | propBit(t_.stroke_) | propBit(t_.speed_) | propBit(t_.force_) |
                              propBit(t_.cidx().color);
        if (changed & ~live) return false;
        b2PrismaticJointDef jd = b2DefaultPrismaticJointDef();
        configure(jd, d);
        b2PrismaticJoint_SetLimits(joint_, jd.lowerTranslation, jd.upperTranslation);
        b2PrismaticJoint_SetMaxMotorForce(joint_, jd.maxMotorForce);
        b2PrismaticJoint_SetMotorSpeed(joint_, jd.motorSpeed);
        b2Joint_WakeBodies(joint_);
        return true;
    }

    void postStep(World& w) override
    {
        // Motor force along the axis is reported as Motor, the rest as Constraint.
        const float motor = b2PrismaticJoint_GetMotorForce(joint_);
        const b2Vec2 axis = b2Normalize(b2Sub(anchorB(w), anchorA(w)));
        const b2Vec2 fm = b2MulSV(motor, axis);
        const b2Vec2 total = b2Joint_GetConstraintForce(joint_);
        const b2Vec2 fc = b2Sub(total, fm);
        const float torque = b2Joint_GetConstraintTorque(joint_);
        const b2Vec2 pA = anchorA(w), pB = anchorB(w);
        if (ib_ >= 0) {
            const b2Vec2 rb = b2Sub(pB, w.body(ib_).com);
            w.record(ib_, ForceSource::Motor, fm, b2Cross(rb, fm));
            w.record(ib_, ForceSource::Constraint, fc, b2Cross(rb, fc) + torque);
        }
        if (ia_ >= 0) {
            const b2Vec2 ra = b2Sub(pA, w.body(ia_).com);
            w.record(ia_, ForceSource::Motor, b2Neg(fm), b2Cross(ra, b2Neg(fm)));
            w.record(ia_, ForceSource::Constraint, b2Neg(fc), b2Cross(ra, b2Neg(fc)) - torque);
        }
        motorForce_ = motor;
        // Oscillate: reverse at the ends of the stroke.
        const float x = b2PrismaticJoint_GetTranslation(joint_);
        if (mode_ == 0) {
            const float eps = std::max(1e-3f, 0.01f * stroke_);
            if (dir_ > 0 && x >= base_ + stroke_ - eps) dir_ = -1;
            else if (dir_ < 0 && x <= base_ + eps) dir_ = 1;
            b2PrismaticJoint_SetMotorSpeed(joint_, float(dir_) * speed_);
        }
        extension_ = x - base_;
    }

    void saveState(std::vector<double>& out) const override { out = {double(dir_)}; }
    void loadState(const std::vector<double>& s) override
    {
        if (s.size() == 1) {
            dir_ = s[0] < 0 ? -1 : 1;
            b2PrismaticJoint_SetMotorSpeed(joint_, float(dir_) * speed_);
        }
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = motorForce_;
        s.v[5] = extension_;
        s.v[6] = float(dir_);
        return true;
    }

private:
    const PistonMechanism& t_;
    int mode_ = 0, dir_ = 1;
    float base_ = 1, stroke_ = 1, speed_ = 1, motorForce_ = 0, extension_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> PistonMechanism::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<PistonRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(PistonMechanism)

} // namespace kn
