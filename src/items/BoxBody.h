// Rectangular body; also the base of ground, wall, conveyor and trampoline items.
#pragma once

#include "BodyItem.h"

namespace kn {

class BoxBody : public BodyItemType {
public:
    BoxBody();
    void shapes(const ItemDef& def, ShapeList& out) const override;
    bool scale(ItemDef& def, double sx, double sy) const override;

protected:
    struct Defaults {
        double width = 1.0, height = 1.0;
        int material = 2;
        int bodyType = 2;
        Color color = Color::fromRgb(0xf39c1f);
    };
    BoxBody(ItemMeta meta, const Defaults& d);
    int width_ = -1;
    int height_ = -1;
};

} // namespace kn
