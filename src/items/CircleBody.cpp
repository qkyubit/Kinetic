// Solid disc.
#include "BodyItem.h"

namespace kn {

class CircleBody final : public BodyItemType {
public:
    CircleBody()
        : BodyItemType({QStringLiteral("body.circle"), QStringLiteral("Circle"), QStringLiteral("Bodies"),
                        QStringLiteral("draw-circle"), QStringLiteral(":/icons/body.svg"),
                        {QStringLiteral("ball"), QStringLiteral("disc"), QStringLiteral("wheel"), QStringLiteral("sphere")},
                        QStringLiteral("A solid disc.")})
    {
        addTransform();
        radius_ = schema_.size();
        schema_.num(QStringLiteral("radius"), QStringLiteral("Radius"), unit::m, 0.01, 100, 0.5).log().rebuild();
        addBodyProperties(2, 2, Color::fromRgb(0x3daee9));
    }

    void shapes(const ItemDef& def, ShapeList& out) const override
    {
        out.clear();
        out.push_back(Shape::circle({0, 0}, def.d(radius_)));
    }

    bool scale(ItemDef& def, double sx, double sy) const override
    {
        def.set(radius_, clampValue(schema_[radius_], def.d(radius_) * std::max(std::abs(sx), std::abs(sy))));
        propertyChanged(def, radius_);
        return true;
    }

private:
    int radius_ = -1;
};

KINETIC_DEFINE_ITEM(CircleBody)

} // namespace kn
