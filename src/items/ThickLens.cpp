// Glass lens with spherical (or flat, R = 0) surfaces, traced exactly with
// Snell's law, so it shows aberration and dispersion. Optical axis: local x;
// radii follow the lensmaker sign convention (positive: centre on the +x side).
#include "OpticsItem.h"

namespace kn {

class ThickLens final : public OpticsType {
public:
    ThickLens()
        : OpticsType({QStringLiteral("optics.lens"), QStringLiteral("Glass lens"), QStringLiteral("Optics"),
                      QStringLiteral("zoom-original"), QStringLiteral(":/icons/optics.svg"),
                      {QStringLiteral("biconvex"), QStringLiteral("biconcave"), QStringLiteral("plano-convex"),
                       QStringLiteral("meniscus"), QStringLiteral("lensmaker"), QStringLiteral("thick lens")},
                      QStringLiteral("A glass lens with spherical surfaces (R = 0 for flat).")})
    {
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Lens"));
        r1_ = s.size();
        s.num(QStringLiteral("radius1"), QStringLiteral("Front radius R₁"), unit::m, -1000, 1000, 1.5, 3);
        r2_ = s.size();
        s.num(QStringLiteral("radius2"), QStringLiteral("Back radius R₂"), unit::m, -1000, 1000, -1.5, 3);
        t_ = s.size();
        s.num(QStringLiteral("thickness"), QStringLiteral("Centre thickness"), unit::m, 0.001, 100, 0.15, 3);
        ap_ = s.size();
        s.num(QStringLiteral("aperture"), QStringLiteral("Aperture"), unit::m, 0.01, 1000, 1.0, 3)
            .tip(QStringLiteral("Clipped so the surfaces do not cross at the rim."));
        f_ = s.size();
        s.num(QStringLiteral("focalLength"), QStringLiteral("Focal length (587.6 nm)"), unit::m, -1e9, 1e9, 0, 4).readOnly();
        addGlass(1);
        addColour(Color::fromRgb(0x74b9ff, 70));
    }

    // Half aperture limited by the radii and by a non-negative rim thickness.
    double halfAperture(const ItemDef& d) const
    {
        const double R1 = d.d(r1_), R2 = d.d(r2_), t = d.d(t_);
        double h = 0.5 * d.d(ap_);
        if (R1 != 0) h = std::min(h, 0.999 * std::abs(R1));
        if (R2 != 0) h = std::min(h, 0.999 * std::abs(R2));
        auto rim = [&](double y) { return t + sag(R2, y) - sag(R1, y); };
        if (rim(h) < 0) {
            double lo = 0, hi = h;
            for (int i = 0; i < 60; ++i) ((rim(0.5 * (lo + hi)) >= 0) ? lo : hi) = 0.5 * (lo + hi);
            h = lo;
        }
        return h;
    }

    void propertyChanged(ItemDef& d, int index) const override
    {
        OpticsType::propertyChanged(d, index);
        // Thick-lens lensmaker equation.
        const double n = glass(d).index(587.6), t = d.d(t_);
        const double c1 = d.d(r1_) != 0 ? 1.0 / d.d(r1_) : 0.0, c2 = d.d(r2_) != 0 ? 1.0 / d.d(r2_) : 0.0;
        const double P = (n - 1) * (c1 - c2 + (n - 1) * t * c1 * c2 / n);
        d.set(f_, P != 0 ? 1.0 / P : 0.0);
    }

    void outline(const ItemDef& d, PointList& out, bool& closed) const override
    {
        out.clear();
        const double h = halfAperture(d), t = d.d(t_);
        sampleSphericalSurface(out, -0.5 * t, d.d(r1_), h, true);
        sampleSphericalSurface(out, 0.5 * t, d.d(r2_), h, false);
        closed = true;
    }

    void addOptics(const ItemDef& d, const Pose& pose, optics::Scene& scene) const override
    {
        const double h = halfAperture(d), t = d.d(t_), R1 = d.d(r1_), R2 = d.d(r2_);
        const int e = scene.addElement(glass(d));
        addSphericalSurface(scene, optics::SurfaceKind::Refract, pose, -0.5 * t, R1, h, e);
        addSphericalSurface(scene, optics::SurfaceKind::Refract, pose, 0.5 * t, R2, h, e);
        const double x1 = -0.5 * t + sag(R1, h), x2 = 0.5 * t + sag(R2, h);
        if (x2 - x1 > 1e-9) {
            scene.addSegment(optics::SurfaceKind::Refract, pose.apply({x1, h}), pose.apply({x2, h}), e);
            scene.addSegment(optics::SurfaceKind::Refract, pose.apply({x1, -h}), pose.apply({x2, -h}), e);
        }
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        OpticsType::draw(d, ctx);
        if (!ctx.selected) return;
        const double f = d.d(f_), t = d.d(t_);
        if (f == 0 || std::abs(f) > 1e4) return;
        for (const double x : {-f, f})
            ctx.batch.disc(Layer::Overlay, ctx.pose.apply({x + (x > 0 ? 0.5 : -0.5) * t, 0}), ctx.px(3),
                           ctx.theme.highlight, ctx.theme.highlight);
    }

    int r1_, r2_, t_, ap_, f_;
};

KINETIC_DEFINE_ITEM(ThickLens)

} // namespace kn
