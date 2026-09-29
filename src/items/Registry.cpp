// Builds the item registry from ItemList.h.
#include "core/Item.h"

namespace kn {

#define KINETIC_ITEM(Class) const ItemType* kineticItemFactory_##Class();
#include "ItemList.h"
#undef KINETIC_ITEM

ItemRegistry::ItemRegistry()
{
#define KINETIC_ITEM(Class) add(kineticItemFactory_##Class());
#include "ItemList.h"
#undef KINETIC_ITEM
}

} // namespace kn
