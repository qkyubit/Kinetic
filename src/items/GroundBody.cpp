// Static ground slab.
#include "BoxBody.h"

namespace kn {

class GroundBody final : public BoxBody {
public:
    GroundBody()
        : BoxBody({QStringLiteral("body.ground"), QStringLiteral("Ground"), QStringLiteral("Bodies"),
                   QStringLiteral("format-align-vertical-bottom"), QStringLiteral(":/icons/body.svg"),
                   {QStringLiteral("floor"), QStringLiteral("static"), QStringLiteral("terrain")},
                   QStringLiteral("A static ground slab.")},
                  Defaults{40.0, 1.0, 6, 0, Color::fromRgb(0x7f8c8d)})
    {
    }
};

KINETIC_DEFINE_ITEM(GroundBody)

} // namespace kn
