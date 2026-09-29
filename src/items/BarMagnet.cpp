// Bar magnet: a rigid block carrying a magnetic dipole moment along its +x
// axis (north pole at +x). Magnets attract, repel and align each other through
// the dipole–dipole force and torque (computed in World).
#include "BoxBody.h"

#include "core/RenderBatch.h"
#include "core/World.h"

namespace kn {

class BarMagnet final : public BoxBody {
public:
    BarMagnet()
        : BoxBody({QStringLiteral("body.magnet"), QStringLiteral("Bar magnet"), QStringLiteral("Fields"),
                   QStringLiteral("magnet"), QStringLiteral(":/icons/field.svg"),
                   {QStringLiteral("dipole"), QStringLiteral("compass"), QStringLiteral("north"), QStringLiteral("south")},
                   QStringLiteral("A block with a magnetic dipole moment; north pole at +x.")},
                  Defaults{0.8, 0.2, 1, 2, Color::fromRgb(0xc0392b)})
    {
        schema_.beginGroup(QStringLiteral("Magnet"));
        moment_ = schema_.size();
        schema_.num(QStringLiteral("moment"), QStringLiteral("Dipole moment"), QStringLiteral("A·m²"), -1e8, 1e8, 3e4, 0)
            .tip(QStringLiteral("Magnetic moment along +x. Force between two magnets falls off as 1/r⁴."));
    }

    void customizeBody(const ItemDef& d, World& w, int bi) const override
    {
        w.body(bi).magMoment = {float(d.d(moment_)), 0.0f};
    }
    bool applyExtraEdit(const ItemDef& d, int index, World& w, int bi) const override
    {
        if (index != moment_) return BoxBody::applyExtraEdit(d, index, w, bi);
        customizeBody(d, w, bi);
        w.wakeAll();
        return true;
    }

    bool addField(const ItemDef& d, const Pose& pose, FieldKind k, Vec2 p, Vec2& inout) const override
    {
        if (k != FieldKind::Magnetic) return BoxBody::addField(d, pose, k, p, inout);
        const Vec2 m = pose.applyVector({d.d(moment_), 0});
        const Vec2 r = p - pose.p;
        const double len = length(r);
        if (len < 1e-9) return false;
        const Vec2 u = r / len;
        const double d2 = len * len + 1e-4, dd = std::sqrt(d2);
        inout += (u * (3.0 * dot(m, u)) - m) * (1e-7 / (d2 * dd));
        return false;
    }

    void drawExtras(const ItemDef& d, const DrawContext& ctx) const override
    {
        // South half blue, north half red, with pole letters.
        const double hw = 0.5 * d.d(width_), hh = 0.5 * d.d(height_);
        const bool flip = d.d(moment_) < 0;
        const Vec2 s[4] = {ctx.pose.apply({-hw, -hh}), ctx.pose.apply({0, -hh}), ctx.pose.apply({0, hh}),
                           ctx.pose.apply({-hw, hh})};
        ctx.batch.convexFill(Layer::Body, s, 4, flip ? Color::fromRgb(0xc0392b, 255) : Color::fromRgb(0x2e86de, 255));
        const Vec2 n[4] = {ctx.pose.apply({0, -hh}), ctx.pose.apply({hw, -hh}), ctx.pose.apply({hw, hh}),
                           ctx.pose.apply({0, hh})};
        ctx.batch.convexFill(Layer::Body, n, 4, flip ? Color::fromRgb(0x2e86de, 255) : Color::fromRgb(0xc0392b, 255));
        static const QString N = QStringLiteral("N"), S = QStringLiteral("S");
        const Color white{255, 255, 255, 255};
        ctx.batch.label(ctx.pose.apply({hw * 0.5, 0}), flip ? S : N, white, 0, 0);
        ctx.batch.label(ctx.pose.apply({-hw * 0.5, 0}), flip ? N : S, white, 0, 0);
    }

private:
    int moment_ = -1;
};

KINETIC_DEFINE_ITEM(BarMagnet)

} // namespace kn
