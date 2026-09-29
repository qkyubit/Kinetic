// Spherical mirror. Positive radius: concave towards +x (focal point at R/2 on
// the +x side); negative: convex.
#include "OpticsItem.h"

namespace kn {

class CurvedMirror final : public OpticsType {
public:
    CurvedMirror()
        : OpticsType({QStringLiteral("optics.curvedMirror"), QStringLiteral("Curved mirror"), QStringLiteral("Optics"),
                      QStringLiteral("draw-arc"), QStringLiteral(":/icons/optics.svg"),
                      {QStringLiteral("concave"), QStringLiteral("convex"), QStringLiteral("spherical mirror"),
                       QStringLiteral("focus")},
                      QStringLiteral("Spherical mirror; positive radius is concave towards +x.")})
    {
        schema_.beginGroup(QStringLiteral("Mirror"));
        r_ = schema_.size();
        schema_.num(QStringLiteral("radius"), QStringLiteral("Radius of curvature"), unit::m, -1000, 1000, 3.0, 3)
            .tip(QStringLiteral("Focal length is R/2."));
        ap_ = schema_.size();
        schema_.num(QStringLiteral("aperture"), QStringLiteral("Aperture"), unit::m, 0.01, 1000, 1.5, 3);
        refl_ = schema_.size();
        schema_.num(QStringLiteral("reflectance"), QStringLiteral("Reflectance"), unit::none, 0, 1, 1.0, 3);
        addColour(Color::fromRgb(0x95a5a6));
    }

    double halfAperture(const ItemDef& d) const
    {
        const double R = d.d(r_);
        return std::min(0.5 * d.d(ap_), R != 0 ? 0.999 * std::abs(R) : 1e9);
    }

    void outline(const ItemDef& d, PointList& out, bool& closed) const override
    {
        out.clear();
        sampleSphericalSurface(out, 0.0, d.d(r_), halfAperture(d), false, 32);
        closed = false;
    }

    void addOptics(const ItemDef& d, const Pose& pose, optics::Scene& scene) const override
    {
        addSphericalSurface(scene, optics::SurfaceKind::Mirror, pose, 0.0, d.d(r_), halfAperture(d), -1);
        scene.surfaces.back().reflectance = d.d(refl_);
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        thread_local PointList pts, world;
        bool closed;
        outline(d, pts, closed);
        world.resize(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) world[i] = ctx.pose.apply(pts[i]);
        const Color c = ctx.selected ? ctx.theme.highlight : d.c(color_);
        ctx.batch.polyline(Layer::Body, world.data(), int(world.size()), false, 3.5f, c);
        for (size_t i = 2; i + 2 < pts.size(); i += 2)
            ctx.batch.line(Layer::Body, world[i], ctx.pose.apply(pts[i] + Vec2{-ctx.px(7), -ctx.px(7)}), 1.0f, c.withAlpha(150));
        if (ctx.selected && d.d(r_) != 0) // focal point
            ctx.batch.disc(Layer::Overlay, ctx.pose.apply({0.5 * d.d(r_), 0}), ctx.px(3), ctx.theme.highlight, ctx.theme.highlight);
    }

    int r_, ap_, refl_;
};

KINETIC_DEFINE_ITEM(CurvedMirror)

} // namespace kn
