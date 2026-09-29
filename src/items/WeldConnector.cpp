// Rigid weld: locks the relative position and rotation of two bodies.
#include "ConnectorItem.h"

namespace kn {

class WeldConnector final : public ConnectorType {
public:
    WeldConnector()
        : ConnectorType({QStringLiteral("connector.weld"), QStringLiteral("Weld"), QStringLiteral("Connectors"),
                         QStringLiteral("insert-link"), QStringLiteral(":/icons/connector.svg"),
                         {QStringLiteral("glue"), QStringLiteral("fix"), QStringLiteral("rigid"), QStringLiteral("attach")},
                         QStringLiteral("Glues two bodies (or a body and the world) together at a point.")})
    {
        addAttachment(false);
        ref_ = schema_.size();
        schema_.num(QStringLiteral("referenceAngle"), QStringLiteral("Reference angle"), unit::deg, -100, 100, 0)
            .hidden()
            .rebuild();
        addAppearance(Color::fromRgb(0xbdc3c7));
    }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        const Vec2 p = worldA(d, view);
        d.set(cidx_.anchorB, toAnchor(d.ref(cidx_.bodyB), p, view));
        Pose pa, pb;
        const double aA = d.ref(cidx_.bodyA) && view.pose(d.ref(cidx_.bodyA), pa) ? pa.angle() : 0.0;
        const double aB = d.ref(cidx_.bodyB) && view.pose(d.ref(cidx_.bodyB), pb) ? pb.angle() : 0.0;
        d.set(ref_, aB - aA);
    }

    bool hitTest(const ItemDef& d, const SceneView& view, Vec2 w, double tol) const override
    {
        return distance(worldA(d, view), w) <= 6.0 * tol;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 p = worldA(d, ctx.view);
        const Color col = color(d, ctx);
        const double s = 5.0 / ctx.pxPerM;
        ctx.batch.line(Layer::Front, p + Vec2{-s, -s}, p + Vec2{s, s}, lineWidth(ctx, 2.5f), col);
        ctx.batch.line(Layer::Front, p + Vec2{-s, s}, p + Vec2{s, -s}, lineWidth(ctx, 2.5f), col);
    }

    int ref_;
};

namespace {
class WeldRuntime final : public ConnectorRuntime {
public:
    WeldRuntime(const ItemDef& d, const WeldConnector& t, World& w) : ConnectorRuntime(d, t, w)
    {
        b2WeldJointDef jd = b2DefaultWeldJointDef();
        jd.bodyIdA = bodyA(w);
        jd.bodyIdB = bodyB(w);
        jd.localAnchorA = la_;
        jd.localAnchorB = lb_;
        jd.referenceAngle = float(d.d(t.ref_));
        jd.linearHertz = 0.0f; // rigid
        jd.angularHertz = 0.0f;
        jd.collideConnected = d.b(t.cidx().collide);
        joint_ = b2CreateWeldJoint(w.id(), &jd);
        applyJointTuning(w);
    }

    bool applyEdit(const ItemDef&, uint64_t changed, World&) override { return changed == 0; }
    void postStep(World& w) override { recordJoint(w, ForceSource::Constraint, ForceSource::Constraint); }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = b2Length(b2Joint_GetConstraintForce(joint_));
        s.v[5] = b2Joint_GetConstraintTorque(joint_);
        return true;
    }
};
} // namespace

std::unique_ptr<ItemRuntime> WeldConnector::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<WeldRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(WeldConnector)

} // namespace kn
