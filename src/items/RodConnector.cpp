// Rigid massless rod (distance joint): keeps the anchor distance fixed.
#include "ConnectorItem.h"

namespace kn {

class RodConnector final : public ConnectorType {
public:
    RodConnector()
        : ConnectorType({QStringLiteral("connector.rod"), QStringLiteral("Rod"), QStringLiteral("Connectors"),
                         QStringLiteral("draw-line"), QStringLiteral(":/icons/connector.svg"),
                         {QStringLiteral("distance"), QStringLiteral("link"), QStringLiteral("bar"), QStringLiteral("pendulum")},
                         QStringLiteral("Massless rigid rod keeping two anchors at a fixed distance.")})
    {
        addAttachment(false);
        schema_.beginGroup(QStringLiteral("Rod"));
        len_ = schema_.size();
        schema_.num(QStringLiteral("length"), QStringLiteral("Length"), unit::m, 0.01, 1000, 2, 3);
        addAppearance(Color::fromRgb(0x7f8c8d));
    }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        d.set(len_, clampValue(schema_[len_], distance(worldA(d, view), worldB(d, view))));
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;
    QList<Channel> channels() const override
    {
        return {{4, QStringLiteral("Tension (compression −)"), unit::N}, {5, QStringLiteral("Length"), unit::m}};
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 a = worldA(d, ctx.view), b = worldB(d, ctx.view);
        const Color col = color(d, ctx);
        ctx.batch.line(Layer::Front, a, b, lineWidth(ctx, 4.0f), col);
        drawPin(ctx.batch, a, ctx.pxPerM, ctx.theme.background, col);
        drawPin(ctx.batch, b, ctx.pxPerM, ctx.theme.background, col);
    }

    int len_;
};

namespace {
class RodRuntime final : public ConnectorRuntime {
public:
    RodRuntime(const ItemDef& d, const RodConnector& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        b2DistanceJointDef jd = b2DefaultDistanceJointDef();
        jd.bodyIdA = bodyA(w);
        jd.bodyIdB = bodyB(w);
        jd.localAnchorA = la_;
        jd.localAnchorB = lb_;
        jd.length = float(d.d(t.len_));
        jd.enableSpring = false;
        jd.collideConnected = d.b(t.cidx().collide);
        joint_ = b2CreateDistanceJoint(w.id(), &jd);
        applyJointTuning(w);
    }

    bool applyEdit(const ItemDef& d, uint64_t changed, World&) override
    {
        if (changed & ~(propBit(t_.len_) | propBit(t_.cidx().color))) return false;
        b2DistanceJoint_SetLength(joint_, float(d.d(t_.len_)));
        b2Joint_WakeBodies(joint_);
        return true;
    }

    void postStep(World& w) override
    {
        recordJoint(w, ForceSource::Tension, ForceSource::Tension);
        const b2Vec2 axis = b2Normalize(b2Sub(anchorB(w), anchorA(w)));
        tension_ = -b2Dot(b2Joint_GetConstraintForce(joint_), axis);
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = tension_; // positive: tension, negative: compression
        s.v[5] = b2Distance(anchorA(w), anchorB(w));
        return true;
    }

private:
    const RodConnector& t_;
    float tension_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> RodConnector::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<RodRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(RodConnector)

} // namespace kn
