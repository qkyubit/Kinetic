// Light source: a laser (one ray), a parallel beam or a point source, either
// monochromatic or white (sampled across the visible spectrum).
#include "OpticsItem.h"

namespace kn {

class LightSource final : public OpticsType {
public:
    enum Mode : int { Laser, Beam, Point };

    LightSource()
        : OpticsType({QStringLiteral("optics.source"), QStringLiteral("Light source"), QStringLiteral("Optics"),
                      QStringLiteral("flashlight-on"), QStringLiteral(":/icons/optics.svg"),
                      {QStringLiteral("laser"), QStringLiteral("lamp"), QStringLiteral("beam"), QStringLiteral("torch"),
                       QStringLiteral("white light"), QStringLiteral("ray")},
                      QStringLiteral("Emits light rays along its +x axis.")})
    {
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Light"));
        mode_ = s.size();
        s.choice(QStringLiteral("mode"), QStringLiteral("Type"),
                 {QStringLiteral("Laser"), QStringLiteral("Parallel beam"), QStringLiteral("Point source")}, Beam);
        white_ = s.size();
        s.boolean(QStringLiteral("white"), QStringLiteral("White light"), false);
        nm_ = s.size();
        s.num(QStringLiteral("wavelength"), QStringLiteral("Wavelength"), QStringLiteral("nm"), 380, 750, 532, 0);
        samples_ = s.size();
        s.integer(QStringLiteral("samples"), QStringLiteral("Spectral samples (white)"), 3, 32, 12);
        rays_ = s.size();
        s.integer(QStringLiteral("rays"), QStringLiteral("Rays"), 1, 400, 7);
        width_ = s.size();
        s.num(QStringLiteral("beamWidth"), QStringLiteral("Beam width"), unit::m, 0, 100, 0.4, 3);
        spread_ = s.size();
        s.angle(QStringLiteral("spread"), QStringLiteral("Spread (point source)"), 0, 360, 40);
        intensity_ = s.size();
        s.num(QStringLiteral("intensity"), QStringLiteral("Intensity"), unit::none, 0.02, 1, 1, 2);
        addColour(Color::fromRgb(0x2d3436));
    }

    void outline(const ItemDef&, PointList& out, bool& closed) const override
    {
        out = {{-0.3, -0.08}, {0, -0.08}, {0, 0.08}, {-0.3, 0.08}};
        closed = true;
    }

    void addOptics(const ItemDef& d, const Pose& pose, optics::Scene& scene) const override
    {
        const int mode = int(d.i(mode_));
        const int n = mode == Laser ? 1 : int(d.i(rays_));
        const double I = d.d(intensity_);
        auto shoot = [&](Vec2 o, Vec2 dir) {
            if (d.b(white_))
                scene.emitWhite(o, dir, I, int(d.i(samples_)));
            else
                scene.emitRay(o, dir, d.d(nm_), I);
        };
        const Vec2 x = pose.applyVector({1, 0}), y = pose.applyVector({0, 1});
        for (int i = 0; i < n; ++i) {
            const double f = n == 1 ? 0.0 : double(i) / (n - 1) - 0.5;
            if (mode == Point) {
                const double spread = d.d(spread_);
                const bool full = spread >= 2 * kPi - 1e-6;
                const double a = full ? 2 * kPi * i / n : spread * f;
                shoot(pose.p, x * std::cos(a) + y * std::sin(a));
            } else {
                shoot(pose.p + y * (d.d(width_) * f), x);
            }
        }
    }

    void draw(const ItemDef& d, const DrawContext& ctx) const override
    {
        OpticsType::draw(d, ctx);
        float rgb[3] = {1, 1, 1};
        if (!d.b(white_)) optics::wavelengthToRgb(d.d(nm_), rgb);
        const Color c{uint8_t(rgb[0] * 255), uint8_t(rgb[1] * 255), uint8_t(rgb[2] * 255), 255};
        ctx.batch.disc(Layer::Front, ctx.pose.p, 0.06, c, ctx.theme.outline, 1.0f);
        if (d.i(mode_) == Beam) {
            const double hw = 0.5 * d.d(width_);
            ctx.batch.line(Layer::Front, ctx.pose.apply({0, -hw}), ctx.pose.apply({0, hw}), 2.0f, c.withAlpha(160));
        }
    }

    int mode_, white_, nm_, samples_, rays_, width_, spread_, intensity_;
};

KINETIC_DEFINE_ITEM(LightSource)

} // namespace kn
