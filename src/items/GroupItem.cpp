// Folder in the Explorer tree. Groups have no physics; transforms applied to a
// group apply to its descendants.
#include "core/Item.h"

namespace kn {

class GroupItem final : public ItemType {
public:
    GroupItem()
    {
        meta_ = {QStringLiteral("group"), QStringLiteral("Group"), QStringLiteral("Organise"), QStringLiteral("folder"),
                 QStringLiteral(":/icons/group.svg"), {QStringLiteral("folder")},
                 QStringLiteral("Groups items in the Explorer."), ItemKind::Group, CreateMode::Place, false};
    }
    const ItemMeta& meta() const override { return meta_; }
    const PropertySchema& schema() const override { return schema_; }

private:
    ItemMeta meta_;
    PropertySchema schema_;
};

KINETIC_DEFINE_ITEM(GroupItem)

} // namespace kn
