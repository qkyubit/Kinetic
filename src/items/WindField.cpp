// Wind zone / fan: air moving along the item's +x axis. Bodies inside feel
// quadratic drag ½ ρ Cd A |u − v| (u − v), with A their silhouette width
// across the relative velocity (per metre of depth).
#include "FieldItem.h"

namespace kn {

class WindField final : public FieldType {
public:
    WindField()
        : FieldType({QStringLiteral("field.wind"), QStringLiteral("Wind / fan"), QStringLiteral("Fields"),
                     QStringLiteral("weather-windy"), QStringLiteral(":/icons/field.svg"),
                     {QStringLiteral("air"), QStringLiteral("blower"), QStringLiteral("draft"), QStringLiteral("breeze")},
                     QStringLiteral("Moving air inside a region; a fan's speed falls off towards the far end.")})
    {
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Wind"));
        speed_ = s.size();
        s.num(QStringLiteral("speed"), QStringLiteral("Wind speed"), unit::mps, -200, 200, 10, 2)
            .tip(QStringLiteral("Along the item's +x axis (rotate the item to aim it)."));
        fan_ = s.size();
        s.boolean(QStringLiteral("fan"), QStringLiteral("Fan falloff"), false)
            .tip(QStringLiteral("Speed falls linearly from full at the back (−x edge) to zero at the front."));
        addRegion(Rectangle, 6, 3, 3);
        addColour(Color::fromRgb(0x74b9ff, 70));
    }

    // Wind velocity at a point already known to be inside.
    Vec2 velocityAt(const ItemDef& d, const Pose& pose, Vec2 p) const
    {
        double u = d.d(speed_);
        if (d.b(fan_)) {
            const double len = d.i(regionIndex()) == Circle ? 2 * d.d(r_) : d.d(w_);
            u *= std::clamp(0.5 - pose.inverse(p).x / len, 0.0, 1.0);
        }
        return pose.applyVector({u, 0});
    }

    bool addField(const ItemDef& d, const Pose& pose, FieldKind k, Vec2 p, Vec2& inout) const override
    {
        if (k == FieldKind::Wind && contains(d, pose, p)) inout += velocityAt(d, pose, p);
        return false;
    }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Color c = drawRegion(d, ctx).withAlpha(200);
        // Streaks drifting with the wind.
        const double u = d.d(speed_);
        const int region = int(d.i(regionIndex()));
        const double hw = region == Circle ? d.d(r_) : (region == Rectangle ? 0.5 * d.d(w_) : 2.0);
        const double hh = region == Circle ? d.d(r_) : (region == Rectangle ? 0.5 * d.d(h_) : 1.0);
        const double len = std::min(hw * 0.4, ctx.px(30));
        const double shift = std::fmod(ctx.time * u, 2 * hw);
        for (int row = 0; row < 5; ++row) {
            const double y = -hh + (row + 0.5) * (2 * hh / 5);
            for (int k = 0; k < 3; ++k) {
                double x = -hw + std::fmod(k * (2 * hw / 3) + row * hw * 0.37 + shift + 4 * hw, 2 * hw);
                const Vec2 a = ctx.pose.apply({x, y});
                if (region == Circle && !contains(d, ctx.pose, a)) continue;
                const Vec2 v = velocityAt(d, ctx.pose, a);
                const double m = length(v);
                if (m < 1e-6) continue;
                ctx.batch.arrow(Layer::Front, a, a + v * (len / std::max(m, std::abs(u))), 1.5f, c, 6);
            }
        }
    }

    int speed_, fan_;
};

namespace {
class WindRuntime final : public FieldRuntime {
public:
    WindRuntime(const ItemDef& d, const WindField& t, World& w) : FieldRuntime(d, t, w), t_(t) { edited(d); }
    void edited(const ItemDef& d) override { def_ = std::make_shared<ItemDef>(d); }

    void applyField(World& w) override
    {
        const float rho = float(w.settings().airDensity);
        const float h = w.dt();
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            if (!affects(w, i)) continue;
            const BodyRec& b = w.body(i);
            const Vec2 u = t_.velocityAt(*def_, pose_, {b.com.x, b.com.y});
            const b2Vec2 rel = b2Sub({float(u.x), float(u.y)}, b.v);
            const float speed = b2Length(rel);
            if (speed < 1e-6f) continue;
            const b2Vec2 dir = b2MulSV(1.0f / speed, rel);
            const float width = float(w.silhouetteWidth(i, {-dir.y, dir.x}));
            const float f = std::min(0.5f * rho * b.dragCoefficient * width * speed * speed, b.mass * speed / h);
            w.addFieldForce(i, ForceSource::Wind, b2MulSV(f, dir));
        }
    }

private:
    const WindField& t_;
};
} // namespace

std::unique_ptr<ItemRuntime> WindField::createRuntime(const ItemDef& d, World& w, const RestoreState*) const
{
    return std::make_unique<WindRuntime>(d, *this, w);
}

KINETIC_DEFINE_ITEM(WindField)

} // namespace kn
