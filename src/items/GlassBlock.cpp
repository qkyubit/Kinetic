// Rectangular glass block (refraction, total internal reflection).
#include "OpticsItem.h"

namespace kn {

class GlassBlock final : public OpticsType {
public:
    GlassBlock()
        : OpticsType({QStringLiteral("optics.block"), QStringLiteral("Glass block"), QStringLiteral("Optics"),
                      QStringLiteral("draw-rectangle"), QStringLiteral(":/icons/optics.svg"),
                      {QStringLiteral("slab"), QStringLiteral("total internal reflection"), QStringLiteral("window")},
                      QStringLiteral("A rectangular block of glass.")})
    {
        auto& s = schema_;
        s.beginGroup(QStringLiteral("Block"));
        w_ = s.size();
        s.num(QStringLiteral("width"), QStringLiteral("Width"), unit::m, 0.01, 1000, 1.0, 3).log();
        h_ = s.size();
        s.num(QStringLiteral("height"), QStringLiteral("Height"), unit::m, 0.01, 1000, 0.6, 3).log();
        addGlass(1);
        addColour(Color::fromRgb(0x81ecec, 70));
    }

    void outline(const ItemDef& d, PointList& out, bool& closed) const override
    {
        const double hw = 0.5 * d.d(w_), hh = 0.5 * d.d(h_);
        out = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        closed = true;
    }
    bool scale(ItemDef& d, double sx, double sy) const override
    {
        d.set(w_, clampValue(schema_[w_], d.d(w_) * std::abs(sx)));
        d.set(h_, clampValue(schema_[h_], d.d(h_) * std::abs(sy)));
        return true;
    }

    void addOptics(const ItemDef& d, const Pose& pose, optics::Scene& scene) const override
    {
        addPolygonElement(d, pose, scene);
    }

    int w_, h_;
};

KINETIC_DEFINE_ITEM(GlassBlock)

} // namespace kn
