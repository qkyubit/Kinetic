// Absorber / screen: stops every ray that reaches it.
#include "OpticsItem.h"

namespace kn {

class Absorber final : public OpticsType {
public:
    Absorber()
        : OpticsType({QStringLiteral("optics.absorber"), QStringLiteral("Screen"), QStringLiteral("Optics"),
                      QStringLiteral("view-media-screensaver"), QStringLiteral(":/icons/optics.svg"),
                      {QStringLiteral("absorber"), QStringLiteral("blocker"), QStringLiteral("wall"), QStringLiteral("stop")},
                      QStringLiteral("Stops light; place it to catch a spectrum or block stray rays.")})
    {
        schema_.beginGroup(QStringLiteral("Screen"));
        len_ = schema_.size();
        schema_.num(QStringLiteral("length"), QStringLiteral("Length"), unit::m, 0.01, 1000, 1.5, 3).log();
        addColour(Color::fromRgb(0x2d3436));
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
        scene.addSegment(optics::SurfaceKind::Absorb, pose.apply({0, -h}), pose.apply({0, h}));
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const double h = 0.5 * d.d(len_);
        const Color c = ctx.selected ? ctx.theme.highlight : d.c(color_);
        ctx.batch.line(Layer::Body, ctx.pose.apply({0, -h}), ctx.pose.apply({0, h}), 5.0f, c);
    }

    int len_;
};

KINETIC_DEFINE_ITEM(Absorber)

} // namespace kn
