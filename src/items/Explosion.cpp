// Explosion: at a set time, every dynamic body within the radius receives an
// outward impulse that falls off linearly from the centre to the edge.
#include "core/Item.h"
#include "core/RenderBatch.h"
#include "core/Runtime.h"
#include "core/World.h"

namespace kn {

class Explosion final : public ItemType {
public:
    Explosion()
    {
        meta_ = {QStringLiteral("field.explosion"), QStringLiteral("Explosion"), QStringLiteral("Fields"),
                 QStringLiteral("games-hint"), QStringLiteral(":/icons/field.svg"),
                 {QStringLiteral("blast"), QStringLiteral("impulse"), QStringLiteral("bomb"), QStringLiteral("burst")},
                 QStringLiteral("Pushes bodies outwards once, at the set time."), ItemKind::Field};
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Transform"));
        s.vec(QStringLiteral("position"), QStringLiteral("Position"), unit::m, -10000, 10000, {0, 0});
        s.beginGroup(QStringLiteral("Explosion"));
        time_ = s.size();
        s.num(QStringLiteral("time"), QStringLiteral("Detonation time"), unit::s, 0, 1e6, 1, 3);
        radius_ = s.size();
        s.num(QStringLiteral("radius"), QStringLiteral("Radius"), unit::m, 0.05, 1000, 3, 3).log();
        impulse_ = s.size();
        s.num(QStringLiteral("impulse"), QStringLiteral("Impulse at centre"), unit::Ns, 0, 1e8, 2000, 1)
            .log()
            .tip(QStringLiteral("Impulse given to a body at the centre; falls linearly to zero at the radius."));
        s.beginGroup(QStringLiteral("Appearance"));
        color_ = s.size();
        s.colour(QStringLiteral("color"), QStringLiteral("Colour"), Color::fromRgb(0xd63031));
    }
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }

    void shapes(const ItemDef&, ShapeList& out) const override
    {
        out.clear();
        out.push_back(Shape::circle({0, 0}, 0.25));
    }
    bool scale(ItemDef& d, double sx, double sy) const override
    {
        d.set(radius_, clampValue(schema_[radius_], d.d(radius_) * std::sqrt(std::abs(sx * sy))));
        return true;
    }
    QList<Channel> channels() const override { return {{4, QStringLiteral("Detonated"), unit::none}}; }

    std::unique_ptr<ItemRuntime> createRuntime(const ItemDef& d, World& w, const RestoreState*) const override;

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const Color c = ctx.selected ? ctx.theme.highlight : d.c(color_);
        const bool fired = ctx.snap && ctx.snap->v[4] > 0.5f;
        const double r = d.d(radius_);
        if (fired) {
            // Expanding shock ring for half a second after detonation.
            const double age = ctx.time - d.d(time_);
            if (age >= 0 && age < 0.5) {
                const double k = age / 0.5;
                ctx.batch.circleOutline(Layer::Front, ctx.pose.p, r * k, c.withAlpha(uint8_t(255 * (1 - k))), 3.0f);
            }
            ctx.batch.circleOutline(Layer::Front, ctx.pose.p, ctx.px(6), c.withAlpha(90), 1.0f);
            return;
        }
        const int n = 40;
        for (int i = 0; i < n; i += 2) {
            const double a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
            ctx.batch.line(Layer::Front, ctx.pose.p + Vec2{std::cos(a0), std::sin(a0)} * r,
                           ctx.pose.p + Vec2{std::cos(a1), std::sin(a1)} * r, 1.0f, c.withAlpha(150));
        }
        const double s = ctx.px(12);
        for (int i = 0; i < 8; ++i) {
            const double a = i * kPi / 4;
            const double len = (i % 2 ? 0.5 : 1.0) * s;
            ctx.batch.line(Layer::Front, ctx.pose.p, ctx.pose.p + Vec2{std::cos(a), std::sin(a)} * len, 2.5f, c);
        }
        ctx.batch.valueLabel(ctx.pose.p, QStringLiteral("t = "), d.d(time_), QStringLiteral(" s"), 2, ctx.theme.text, 14, 14);
    }

    int time_, radius_, impulse_, color_;

private:
    ItemMeta meta_;
    PropertySchema schema_;
};

namespace {
class ExplosionRuntime final : public ItemRuntime {
public:
    ExplosionRuntime(const ItemDef& d, const Explosion& t) : ItemRuntime(d.id), t_(t) { read(d); }

    void read(const ItemDef& d)
    {
        c_ = t_.pose(d).p;
        t0_ = d.d(t_.time_);
        r_ = d.d(t_.radius_);
        j_ = d.d(t_.impulse_);
    }
    bool applyEdit(const ItemDef& d, uint64_t, World&) override
    {
        read(d);
        return true;
    }

    void preStep(World& w) override
    {
        // Fires in the first step that starts at or after the detonation time.
        if (fired_ || w.time() + 1e-9 < t0_) return;
        fired_ = true;
        const float h = w.dt();
        for (int i = 0; i < w.bodyCapacity(); ++i) {
            const BodyRec& b = w.body(i);
            if (!b.alive || b.type != b2_dynamicBody) continue;
            const Vec2 r = Vec2{b.com.x, b.com.y} - c_;
            const double dist = length(r);
            if (dist >= r_) continue;
            const Vec2 u = dist > 1e-9 ? r / dist : Vec2{0, 1};
            const double j = j_ * (1.0 - dist / r_);
            // A force of J/h held for one step delivers the impulse J.
            w.addForceAtCenter(i, ForceSource::Explosion, {float(u.x * j / h), float(u.y * j / h)}, true);
        }
    }

    bool snapshot(const World&, ItemSnap& s) const override
    {
        s.v[4] = fired_ ? 1.0f : 0.0f;
        return true;
    }
    void saveState(std::vector<double>& out) const override
    {
        out.clear();
        out.push_back(fired_ ? 1.0 : 0.0);
    }
    void loadState(const std::vector<double>& s) override
    {
        if (!s.empty()) fired_ = s[0] != 0.0;
    }

private:
    const Explosion& t_;
    Vec2 c_;
    double t0_ = 0, r_ = 1, j_ = 0;
    bool fired_ = false;
};
} // namespace

std::unique_ptr<ItemRuntime> Explosion::createRuntime(const ItemDef& d, World&, const RestoreState*) const
{
    return std::make_unique<ExplosionRuntime>(d, *this);
}

KINETIC_DEFINE_ITEM(Explosion)

} // namespace kn
