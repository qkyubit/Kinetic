#include "BoxBody.h"

namespace kn {

BoxBody::BoxBody()
    : BoxBody({QStringLiteral("body.box"), QStringLiteral("Box"), QStringLiteral("Bodies"),
               QStringLiteral("draw-rectangle"), QStringLiteral(":/icons/body.svg"),
               {QStringLiteral("rectangle"), QStringLiteral("crate"), QStringLiteral("block"), QStringLiteral("square")},
               QStringLiteral("A rectangular block.")},
              Defaults{})
{
}

BoxBody::BoxBody(ItemMeta meta, const Defaults& d) : BodyItemType(std::move(meta))
{
    addTransform();
    width_ = schema_.size();
    schema_.num(QStringLiteral("width"), QStringLiteral("Width"), unit::m, 0.01, 1000, d.width).log().rebuild();
    height_ = schema_.size();
    schema_.num(QStringLiteral("height"), QStringLiteral("Height"), unit::m, 0.01, 1000, d.height).log().rebuild();
    addBodyProperties(d.material, d.bodyType, d.color);
}

void BoxBody::shapes(const ItemDef& def, ShapeList& out) const
{
    out.clear();
    out.push_back(Shape::box(0.5 * def.d(width_), 0.5 * def.d(height_)));
}

bool BoxBody::scale(ItemDef& def, double sx, double sy) const
{
    def.set(width_, clampValue(schema_[width_], def.d(width_) * std::abs(sx)));
    def.set(height_, clampValue(schema_[height_], def.d(height_) * std::abs(sy)));
    propertyChanged(def, width_);
    return true;
}

KINETIC_DEFINE_ITEM(BoxBody)

} // namespace kn
