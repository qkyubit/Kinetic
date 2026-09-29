// Prismatic joint: body A slides along an axis fixed in body B (or the world),
// with optional travel limits and a linear motor. The axis runs from the first
// click towards the second.
#include "ConnectorItem.h"

namespace kn {

class SliderConnector final : public ConnectorType {
public:
    SliderConnector()
        : ConnectorType({QStringLiteral("connector.slider"), QStringLiteral("Slider"), QStringLiteral("Connectors"),
                         QStringLiteral("transform-move-horizontal"), QStringLiteral(":/icons/connector.svg"),
                         {QStringLiteral("prismatic"), QStringLiteral("rail"), QStringLiteral("track"), QStringLiteral("linear")},
                         QStringLiteral("Prismatic joint: click the sliding body, then the rail body or a point along "
                                        "the axis.")})
    {
        addAttachment(false);
        auto& s = schema_;
        ref_ = s.size();
        s.num(QStringLiteral("referenceAngle"), QStringLiteral("Reference angle"), unit::deg, -100, 100, 0).hidden().rebuild();
        s.beginGroup(QStringLiteral("Axis"));
        axis_ = s.size();
        s.angle(QStringLiteral("axisAngle"), QStringLiteral("Axis angle (in B's frame)"), -180, 180, 0).rebuild();
        s.beginGroup(QStringLiteral("Limits"));
        limit_ = s.size();
        s.boolean(QStringLiteral("enableLimit"), QStringLiteral("Limit travel"), false);
        lower_ = s.size();
        s.num(QStringLiteral("lower"), QStringLiteral("Lower translation"), unit::m, -1000, 1000, -1, 3);
        upper_ = s.size();
        s.num(QStringLiteral("upper"), QStringLiteral("Upper translation"), unit::m, -1000, 1000, 1, 3);
        s.beginGroup(QStringLiteral("Motor"));
        motor_ = s.size();
        s.boolean(QStringLiteral("enableMotor"), QStringLiteral("Motor"), false);
        speed_ = s.size();
        s.num(QStringLiteral("motorSpeed"), QStringLiteral("Speed"), unit::mps, -100, 100, 1, 3);
        force_ = s.size();
        s.num(QStringLiteral("maxMotorForce"), QStringLiteral("Max force"), unit::N, 0, 1e8, 1e4, 1).log();
        addAppearance(Color::fromRgb(0x95a5a6));
    }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        const Vec2 a = worldA(d, view), b = worldB(d, view);
        const Vec2 dir = distance(a, b) > 1e-3 ? normalized(b - a) : Vec2{1, 0};
        d.set(cidx_.anchorB, toAnchor(d.ref(cidx_.bodyB), a, view));
        Pose pa, pb;
        const double aA = d.ref(cidx_.bodyA) && view.pose(d.ref(cidx_.bodyA), pa) ? pa.angle() : 0.0;
        const double aB = d.ref(cidx_.bodyB) && view.pose(d.ref(cidx_.bodyB), pb) ? pb.angle() : 0.0;
        d.set(ref_, aA - aB);
        d.set(axis_, wrapAngle(std::atan2(dir.y, dir.x) - aB));
    }

    Vec2 axisWorld(const ItemDef& d, const SceneView& view) const
    {
        Pose pb;
        const double aB = d.ref(cidx_.bodyB) && view.pose(d.ref(cidx_.bodyB), pb) ? pb.angle() : 0.0;
        const double a = aB + d.d(axis_);
        return {std::cos(a), std::sin(a)};
    }

    bool hitTest(const ItemDef& d, const SceneView& view, Vec2 w, double tol) const override
    {
        return distance(worldA(d, view), w) <= 6.0 * tol;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;
    QList<Channel> channels() const override
    {
        return {{4, QStringLiteral("Reaction force"), unit::N},
                {5, QStringLiteral("Translation"), unit::m},
                {6, QStringLiteral("Sliding speed"), unit::mps}};
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 p = worldA(d, ctx.view);
        const Vec2 origin = worldB(d, ctx.view);
        const Vec2 u = axisWorld(d, ctx.view);
        const Color col = color(d, ctx);
        double lo = -1.5, hi = 1.5;
        if (d.b(limit_)) {
            lo = std::min(d.d(lower_), d.d(upper_));
            hi = std::max(d.d(lower_), d.d(upper_));
        }
        const Vec2 n = perp(u) * (3.0 / ctx.pxPerM);
        // Rail with end stops, drawn through the B-side origin of the axis.
        ctx.batch.line(Layer::Front, origin + u * lo + n, origin + u * hi + n, 1.2f, col);
        ctx.batch.line(Layer::Front, origin + u * lo - n, origin + u * hi - n, 1.2f, col);
        if (d.b(limit_)) {
            ctx.batch.line(Layer::Front, origin + u * lo + n * 2.5, origin + u * lo - n * 2.5, 2.0f, col);
            ctx.batch.line(Layer::Front, origin + u * hi + n * 2.5, origin + u * hi - n * 2.5, 2.0f, col);
        }
        drawPin(ctx.batch, p, ctx.pxPerM, ctx.theme.background, col);
    }

    int ref_, axis_, limit_, lower_, upper_, motor_, speed_, force_;
};

namespace {
class SliderRuntime final : public ConnectorRuntime {
public:
    SliderRuntime(const ItemDef& d, const SliderConnector& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        // Box2D body A is our B (the rail): translation = motion of our A along the axis.
        swapped_ = true;
        b2PrismaticJointDef jd = b2DefaultPrismaticJointDef();
        jd.bodyIdA = bodyB(w);
        jd.bodyIdB = bodyA(w);
        jd.localAnchorA = lb_;
        jd.localAnchorB = la_;
        const float ax = float(d.d(t.axis_));
        jd.localAxisA = {std::cos(ax), std::sin(ax)};
        jd.referenceAngle = float(d.d(t.ref_));
        jd.collideConnected = d.b(t.cidx().collide);
        configure(jd, d);
        joint_ = b2CreatePrismaticJoint(w.id(), &jd);
        applyJointTuning(w);
    }

    void configure(b2PrismaticJointDef& jd, const ItemDef& d) const
    {
        jd.enableLimit = d.b(t_.limit_);
        jd.lowerTranslation = float(std::min(d.d(t_.lower_), d.d(t_.upper_)));
        jd.upperTranslation = float(std::max(d.d(t_.lower_), d.d(t_.upper_)));
        jd.enableMotor = d.b(t_.motor_);
        jd.motorSpeed = float(d.d(t_.speed_));
        jd.maxMotorForce = float(d.d(t_.force_));
    }

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        const uint64_t live = propBit(t_.limit_) | propBit(t_.lower_) | propBit(t_.upper_) | propBit(t_.motor_) |
                              propBit(t_.speed_) | propBit(t_.force_) | propBit(t_.cidx().color);
        if (changed & ~live) return false;
        b2PrismaticJointDef jd = b2DefaultPrismaticJointDef();
        configure(jd, d);
        b2PrismaticJoint_EnableLimit(joint_, jd.enableLimit);
        b2PrismaticJoint_SetLimits(joint_, jd.lowerTranslation, jd.upperTranslation);
        b2PrismaticJoint_EnableMotor(joint_, jd.enableMotor);
        b2PrismaticJoint_SetMotorSpeed(joint_, jd.motorSpeed);
        b2PrismaticJoint_SetMaxMotorForce(joint_, jd.maxMotorForce);
        b2Joint_WakeBodies(joint_);
        return true;
    }

    void postStep(World& w) override { recordJoint(w, ForceSource::Constraint, ForceSource::Constraint); }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = b2Length(b2Joint_GetConstraintForce(joint_));
        s.v[5] = b2PrismaticJoint_GetTranslation(joint_);
        s.v[6] = b2PrismaticJoint_GetSpeed(joint_);
        return true;
    }

private:
    const SliderConnector& t_;
};
} // namespace

std::unique_ptr<ItemRuntime> SliderConnector::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<SliderRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(SliderConnector)

} // namespace kn
