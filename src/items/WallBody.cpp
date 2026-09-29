// Static vertical wall.
#include "BoxBody.h"

namespace kn {

class WallBody final : public BoxBody {
public:
    WallBody()
        : BoxBody({QStringLiteral("body.wall"), QStringLiteral("Wall"), QStringLiteral("Bodies"),
                   QStringLiteral("distribute-horizontal"), QStringLiteral(":/icons/body.svg"),
                   {QStringLiteral("static"), QStringLiteral("barrier"), QStringLiteral("side")},
                   QStringLiteral("A static vertical wall.")},
                  Defaults{1.0, 10.0, 6, 0, Color::fromRgb(0x95a5a6)})
    {
    }
};

KINETIC_DEFINE_ITEM(WallBody)

} // namespace kn
