// Inextensible rope: a Box2D distance joint that only resists stretching
// beyond its length (slack below it). Tension comes from the joint impulse.
#include "ConnectorItem.h"

namespace kn {

class RopeConnector final : public ConnectorType {
public:
    RopeConnector()
        : ConnectorType({QStringLiteral("connector.rope"), QStringLiteral("Rope"), QStringLiteral("Connectors"),
                         QStringLiteral("draw-bezier-curves"), QStringLiteral(":/icons/connector.svg"),
                         {QStringLiteral("string"), QStringLiteral("cable"), QStringLiteral("chain"), QStringLiteral("tether")},
                         QStringLiteral("Inextensible rope with a maximum length; goes slack when shorter.")})
    {
        addAttachment(true);
        schema_.beginGroup(QStringLiteral("Rope"));
        len_ = schema_.size();
        schema_.num(QStringLiteral("maxLength"), QStringLiteral("Length"), unit::m, 0.01, 1000, 2, 3);
        addAppearance(Color::fromRgb(0xa0522d));
    }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        d.set(len_, clampValue(schema_[len_], distance(worldA(d, view), worldB(d, view))));
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 a = worldA(d, ctx.view), b = worldB(d, ctx.view);
        const Color col = color(d, ctx);
        drawRope(ctx.batch, Layer::Front, a, b, d.d(len_), lineWidth(ctx, 2.0f), col);
        ctx.batch.disc(Layer::Front, a, 2.5 / ctx.pxPerM, col, col, 0);
        ctx.batch.disc(Layer::Front, b, 2.5 / ctx.pxPerM, col, col, 0);
    }

    int len_;
};

namespace {
class RopeRuntime final : public ConnectorRuntime {
public:
    RopeRuntime(const ItemDef& d, const RopeConnector& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        b2DistanceJointDef jd = b2DefaultDistanceJointDef();
        jd.bodyIdA = bodyA(w);
        jd.bodyIdB = bodyB(w);
        jd.localAnchorA = la_;
        jd.localAnchorB = lb_;
        const float L = float(d.d(t.len_));
        jd.length = L;
        jd.enableSpring = true; // zero stiffness: free between the limits
        jd.hertz = 0.0f;
        jd.dampingRatio = 0.0f;
        jd.enableLimit = true;
        jd.minLength = 0.0f;
        jd.maxLength = L;
        jd.collideConnected = d.b(t.cidx().collide);
        joint_ = b2CreateDistanceJoint(w.id(), &jd);
        applyJointTuning(w);
    }

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        if (changed & ~(propBit(t_.len_) | propBit(t_.cidx().color))) return false;
        const float L = float(d.d(t_.len_));
        b2DistanceJoint_SetLength(joint_, L);
        b2DistanceJoint_SetLengthRange(joint_, 0.0f, L);
        b2Joint_WakeBodies(joint_);
        return true;
    }

    void postStep(World& w) override
    {
        recordJoint(w, ForceSource::Tension, ForceSource::Tension);
        const b2Vec2 pA = anchorA(w), pB = anchorB(w);
        const b2Vec2 axis = b2Normalize(b2Sub(pB, pA));
        tension_ = -b2Dot(b2Joint_GetConstraintForce(joint_), axis);
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = tension_;
        s.v[5] = b2Distance(anchorA(w), anchorB(w));
        return true;
    }

private:
    const RopeConnector& t_;
    float tension_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> RopeConnector::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<RopeRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(RopeConnector)

} // namespace kn
