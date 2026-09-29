// Flat mirror: reflects on both faces.
#include "OpticsItem.h"

namespace kn {

class FlatMirror final : public OpticsType {
public:
    FlatMirror()
        : OpticsType({QStringLiteral("optics.mirror"), QStringLiteral("Flat mirror"), QStringLiteral("Optics"),
                      QStringLiteral("object-flip-horizontal"), QStringLiteral(":/icons/optics.svg"),
                      {QStringLiteral("reflector"), QStringLiteral("plane mirror")},
                      QStringLiteral("A plane mirror along the item's y axis.")})
    {
        schema_.beginGroup(QStringLiteral("Mirror"));
        len_ = schema_.size();
        schema_.num(QStringLiteral("length"), QStringLiteral("Length"), unit::m, 0.01, 1000, 1.0, 3).log();
        refl_ = schema_.size();
        schema_.num(QStringLiteral("reflectance"), QStringLiteral("Reflectance"), unit::none, 0, 1, 1.0, 3);
        addColour(Color::fromRgb(0x95a5a6));
    }

    void outline(const ItemDef& d, PointList& out, bool& closed) const override
    {
        const double h = 0.5 * d.d(len_);
        out = {{0, -h}, {0, h}};
        closed = false;
    }
    bool scale(ItemDef& d, double, double sy) const override
    {
        d.set(len_, clampValue(schema_[len_], d.d(len_) * std::abs(sy)));
        return true;
    }

    void addOptics(const ItemDef& d, const Pose& pose, optics::Scene& scene) const override
    {
        const double h = 0.5 * d.d(len_);
        scene.addSegment(optics::SurfaceKind::Mirror, pose.apply({0, -h}), pose.apply({0, h}));
        scene.surfaces.back().reflectance = d.d(refl_);
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const double h = 0.5 * d.d(len_);
        const Color c = ctx.selected ? ctx.theme.highlight : d.c(color_);
        ctx.batch.line(Layer::Body, ctx.pose.apply({0, -h}), ctx.pose.apply({0, h}), 3.5f, c);
        const double step = std::max(ctx.px(8), h / 40);
        for (double y = -h + step; y < h; y += step)
            ctx.batch.line(Layer::Body, ctx.pose.apply({0, y}), ctx.pose.apply({-step, y - step}), 1.0f, c.withAlpha(150));
    }

    int len_, refl_;
};

KINETIC_DEFINE_ITEM(FlatMirror)

} // namespace kn
