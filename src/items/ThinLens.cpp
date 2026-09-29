// Ideal thin lens: every ray is bent so that parallel rays meet in the focal
// plane (slope' = slope − h/f), with no aberration or dispersion.
#include "OpticsItem.h"

namespace kn {

class ThinLens final : public OpticsType {
public:
    ThinLens()
        : OpticsType({QStringLiteral("optics.thinLens"), QStringLiteral("Ideal lens"), QStringLiteral("Optics"),
                      QStringLiteral("zoom-in"), QStringLiteral(":/icons/optics.svg"),
                      {QStringLiteral("thin lens"), QStringLiteral("converging"), QStringLiteral("diverging"),
                       QStringLiteral("focal length")},
                      QStringLiteral("Aberration-free thin lens; positive focal length converges.")})
    {
        schema_.beginGroup(QStringLiteral("Lens"));
        f_ = schema_.size();
        schema_.num(QStringLiteral("focalLength"), QStringLiteral("Focal length"), unit::m, -1000, 1000, 1.5, 3);
        ap_ = schema_.size();
        schema_.num(QStringLiteral("aperture"), QStringLiteral("Aperture"), unit::m, 0.01, 1000, 1.2, 3);
        addColour(Color::fromRgb(0x0984e3));
    }

    void outline(const ItemDef& d, PointList& out, bool& closed) const override
    {
        const double h = 0.5 * d.d(ap_);
        out = {{0, -h}, {0, h}};
        closed = false;
    }

    void addOptics(const ItemDef& d, const Pose& pose, optics::Scene& scene) const override
    {
        if (d.d(f_) == 0.0) return;
        const double h = 0.5 * d.d(ap_);
        scene.addSegment(optics::SurfaceKind::ThinLens, pose.apply({0, -h}), pose.apply({0, h}));
        scene.surfaces.back().focal = d.d(f_);
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        const double h = 0.5 * d.d(ap_), a = ctx.px(8);
        const Color c = ctx.selected ? ctx.theme.highlight : d.c(color_);
        const Vec2 top = ctx.pose.apply({0, h}), bottom = ctx.pose.apply({0, -h});
        ctx.batch.line(Layer::Body, bottom, top, 2.5f, c);
        // Arrowheads out (converging) or in (diverging).
        const double s = d.d(f_) >= 0 ? 1.0 : -1.0;
        for (const double e : {h, -h}) {
            const double sy = e > 0 ? 1.0 : -1.0;
            const Vec2 tip = ctx.pose.apply({0, e});
            ctx.batch.line(Layer::Body, tip, ctx.pose.apply({-a, e - sy * s * a}), 2.0f, c);
            ctx.batch.line(Layer::Body, tip, ctx.pose.apply({a, e - sy * s * a}), 2.0f, c);
        }
        const double f = d.d(f_);
        for (const double x : {-f, f}) ctx.batch.disc(Layer::Front, ctx.pose.apply({x, 0}), ctx.px(3), c, c);
    }

    int f_, ap_;
};

KINETIC_DEFINE_ITEM(ThinLens)

} // namespace kn
