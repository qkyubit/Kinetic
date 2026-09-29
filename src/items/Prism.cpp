// Triangular prism of a dispersive glass (isosceles, apex up in local space).
#include "OpticsItem.h"

namespace kn {

class Prism final : public OpticsType {
public:
    Prism()
        : OpticsType({QStringLiteral("optics.prism"), QStringLiteral("Prism"), QStringLiteral("Optics"),
                      QStringLiteral("draw-triangle"), QStringLiteral(":/icons/optics.svg"),
                      {QStringLiteral("dispersion"), QStringLiteral("rainbow"), QStringLiteral("spectrum"),
                       QStringLiteral("triangle")},
                      QStringLiteral("Triangular glass prism: splits white light into a spectrum.")})
    {
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Prism"));
        apex_ = s.size();
        s.angle(QStringLiteral("apexAngle"), QStringLiteral("Apex angle"), 5, 170, 60);
        side_ = s.size();
        s.num(QStringLiteral("side"), QStringLiteral("Side length"), unit::m, 0.01, 1000, 1.2, 3).log();
        addGlass(3);
        addColour(Color::fromRgb(0xa29bfe, 70));
    }

    void outline(const ItemDef& d, PointList& out, bool& closed) const override
    {
        const double A = d.d(apex_), L = d.d(side_);
        const double hb = L * std::sin(0.5 * A), H = L * std::cos(0.5 * A);
        out = {{-hb, -H / 3}, {hb, -H / 3}, {0, 2 * H / 3}}; // centroid at the origin
        closed = true;
    }
    bool scale(ItemDef& d, double sx, double sy) const override
    {
        d.set(side_, clampValue(schema_[side_], d.d(side_) * std::sqrt(std::abs(sx * sy))));
        return true;
    }

    void addOptics(const ItemDef& d, const Pose& pose, optics::Scene& scene) const override
    {
        addPolygonElement(d, pose, scene);
    }

    int apex_, side_;
};

KINETIC_DEFINE_ITEM(Prism)

} // namespace kn
