// Capsule (stadium): a rectangle with semicircular ends.
#include "BodyItem.h"

namespace kn {

class CapsuleBody final : public BodyItemType {
public:
    CapsuleBody()
        : BodyItemType({QStringLiteral("body.capsule"), QStringLiteral("Capsule"), QStringLiteral("Bodies"),
                        QStringLiteral("draw-ellipse"), QStringLiteral(":/icons/body.svg"),
                        {QStringLiteral("pill"), QStringLiteral("stadium"), QStringLiteral("rod")},
                        QStringLiteral("A rectangle with rounded ends.")})
    {
        addTransform();
        length_ = schema_.size();
        schema_.num(QStringLiteral("length"), QStringLiteral("Length (between centres)"), unit::m, 0.01, 1000, 1.0)
            .log()
            .rebuild();
        radius_ = schema_.size();
        schema_.num(QStringLiteral("radius"), QStringLiteral("Radius"), unit::m, 0.01, 100, 0.25).log().rebuild();
        addBodyProperties(3, 2, Color::fromRgb(0x1abc9c));
    }

    void shapes(const ItemDef& def, ShapeList& out) const override
    {
        out.clear();
        const double h = 0.5 * def.d(length_);
        out.push_back(Shape::capsule({-h, 0}, {h, 0}, def.d(radius_)));
    }

    bool scale(ItemDef& def, double sx, double sy) const override
    {
        def.set(length_, clampValue(schema_[length_], def.d(length_) * std::abs(sx)));
        def.set(radius_, clampValue(schema_[radius_], def.d(radius_) * std::abs(sy)));
        propertyChanged(def, length_);
        return true;
    }

private:
    int length_ = -1;
    int radius_ = -1;
};

KINETIC_DEFINE_ITEM(CapsuleBody)

} // namespace kn
