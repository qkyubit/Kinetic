// Thruster: a constant force fixed to a body, pointing along a body-fixed
// direction. Click the body where the thruster sits, then the direction it
// should push towards.
#include "ConnectorItem.h"

namespace kn {

class Thruster final : public ConnectorType {
public:
    Thruster()
        : ConnectorType({QStringLiteral("mechanism.thruster"), QStringLiteral("Thruster"), QStringLiteral("Fields"),
                         QStringLiteral("go-up"), QStringLiteral(":/icons/field.svg"),
                         {QStringLiteral("rocket"), QStringLiteral("jet"), QStringLiteral("engine"), QStringLiteral("propulsion")},
                         QStringLiteral("Pushes a body with a constant force: click the body at the nozzle, then the "
                                        "direction to push towards.")})
    {
        addAttachment(false);
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Thruster"));
        dir_ = s.size();
        s.angle(QStringLiteral("direction"), QStringLiteral("Direction (body frame)"), -180, 180, 90);
        force_ = s.size();
        s.num(QStringLiteral("thrust"), QStringLiteral("Thrust"), unit::N, 0, 1e8, 500, 1).log();
        on_ = s.size();
        s.boolean(QStringLiteral("enabled"), QStringLiteral("Firing"), true);
        addAppearance(Color::fromRgb(0xff7675));
        schema_.at(cidx_.bodyB).hidden();
        schema_.at(cidx_.anchorB).hidden();
        schema_.at(cidx_.collide).hidden();
    }

    void initConnection(ItemDef& d, const SceneView& view) const override
    {
        const Vec2 a = worldA(d, view), b = worldB(d, view);
        Pose pa;
        const double bodyAngle = d.ref(cidx_.bodyA) && view.pose(d.ref(cidx_.bodyA), pa) ? pa.angle() : 0.0;
        if (distance(a, b) > 1e-6) d.set(dir_, wrapAngle(std::atan2(b.y - a.y, b.x - a.x) - bodyAngle));
        d.set(cidx_.bodyB, int64_t(0));
        d.set(cidx_.anchorB, a);
    }

    // World direction of the thrust.
    Vec2 direction(const ItemDef& d, const SceneView& view) const
    {
        Pose pa;
        const double bodyAngle = d.ref(cidx_.bodyA) && view.pose(d.ref(cidx_.bodyA), pa) ? pa.angle() : 0.0;
        const double t = bodyAngle + d.d(dir_);
        return {std::cos(t), std::sin(t)};
    }

    bool hitTest(const ItemDef& d, const SceneView& view, Vec2 w, double tol) const override
    {
        return distance(worldA(d, view), w) <= 8.0 * tol;
    }
    Aabb bounds(const ItemDef& d, const SceneView& view) const override
    {
        Aabb b;
        const Vec2 p = worldA(d, view);
        b.add(p - Vec2{0.2, 0.2});
        b.add(p + Vec2{0.2, 0.2});
        return b;
    }

    QList<Channel> channels() const override { return {{4, QStringLiteral("Thrust"), unit::N}}; }
    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& def, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Vec2 p = worldA(d, ctx.view);
        const Vec2 u = direction(d, ctx.view);
        const Vec2 n = perp(u);
        const Color col = color(d, ctx);
        const double s = ctx.px(12);
        // Nozzle: a trapezoid opening backwards (exhaust leaves opposite to the thrust).
        const Vec2 q[4] = {p + n * (s * 0.35), p - n * (s * 0.35), p - u * s - n * (s * 0.6), p - u * s + n * (s * 0.6)};
        ctx.batch.convexFill(Layer::Front, q, 4, col.withAlpha(200));
        ctx.batch.polyline(Layer::Front, q, 4, true, lineWidth(ctx, 1.5f), ctx.selected ? ctx.theme.highlight : col);
        if (d.b(on_) && ctx.simActive) {
            const double flicker = 0.8 + 0.2 * std::sin(ctx.time * 47.0);
            const Vec2 tip = p - u * (s * (1.0 + 1.6 * flicker));
            ctx.batch.triangle(Layer::Front, p - u * s - n * (s * 0.45), p - u * s + n * (s * 0.45), tip,
                               Color::fromRgb(0xfdcb6e, 220));
        }
        if (ctx.selected) ctx.batch.arrow(Layer::Overlay, p, p + u * ctx.px(40), 2.0f, ctx.theme.highlight);
    }

    int dir_, force_, on_;
};

namespace {
class ThrusterRuntime final : public ConnectorRuntime {
public:
    ThrusterRuntime(const ItemDef& d, const Thruster& t, World& w) : ConnectorRuntime(d, t, w), t_(t)
    {
        applyEdit(d, ~0ull, w);
    }

    bool applyEdit(const ItemDef& d, uint64_t, World& w) override
    {
        const double a = d.d(t_.dir_);
        local_ = {float(std::cos(a)), float(std::sin(a))};
        thrust_ = d.b(t_.on_) ? float(d.d(t_.force_)) : 0.0f;
        if (ia_ >= 0 && thrust_ > 0 && b2Body_IsValid(w.body(ia_).id)) b2Body_SetAwake(w.body(ia_).id, true);
        return true;
    }

    void preStep(World& w) override
    {
        if (ia_ < 0 || thrust_ == 0.0f) return;
        const b2Vec2 dir = b2RotateVector(w.body(ia_).rot, local_);
        w.addForce(ia_, ForceSource::Thruster, b2MulSV(thrust_, dir), anchorA(w));
    }

    bool snapshot(const World& w, ItemSnap& s) const override
    {
        fillAnchors(w, s);
        s.v[4] = thrust_;
        return true;
    }

private:
    const Thruster& t_;
    b2Vec2 local_{0, 1};
    float thrust_ = 0;
};
} // namespace

std::unique_ptr<ItemRuntime> Thruster::createRuntime(const ItemDef& def, World& w, const RestoreState*) const
{
    return std::make_unique<ThrusterRuntime>(def, *this, w);
}

KINETIC_DEFINE_ITEM(Thruster)

} // namespace kn
