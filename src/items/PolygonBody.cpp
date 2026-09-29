// Regular convex polygon (3 to 8 sides).
#include "BodyItem.h"

#include "core/Geometry.h"

namespace kn {

class PolygonBody final : public BodyItemType {
public:
    PolygonBody()
        : BodyItemType({QStringLiteral("body.polygon"), QStringLiteral("Regular polygon"), QStringLiteral("Bodies"),
                        QStringLiteral("draw-polygon"), QStringLiteral(":/icons/body.svg"),
                        {QStringLiteral("triangle"), QStringLiteral("hexagon"), QStringLiteral("pentagon"),
                         QStringLiteral("convex")},
                        QStringLiteral("A regular convex polygon.")})
    {
        addTransform();
        sides_ = schema_.size();
        schema_.integer(QStringLiteral("sides"), QStringLiteral("Sides"), 3, 8, 6).rebuild();
        radius_ = schema_.size();
        schema_.num(QStringLiteral("radius"), QStringLiteral("Circumradius"), unit::m, 0.01, 100, 0.5).log().rebuild();
        addBodyProperties(2, 2, Color::fromRgb(0x9b59b6));
    }

    void shapes(const ItemDef& def, ShapeList& out) const override
    {
        out.clear();
        const int n = int(def.i(sides_));
        const PointList p = geom::regularPolygon(n, def.d(radius_), kPi / 2 - kPi / n);
        out.push_back(Shape::polygon(p.data(), int(p.size())));
    }

    bool scale(ItemDef& def, double sx, double sy) const override
    {
        def.set(radius_, clampValue(schema_[radius_], def.d(radius_) * std::max(std::abs(sx), std::abs(sy))));
        propertyChanged(def, radius_);
        return true;
    }

private:
    int sides_ = -1;
    int radius_ = -1;
};

KINETIC_DEFINE_ITEM(PolygonBody)

} // namespace kn
