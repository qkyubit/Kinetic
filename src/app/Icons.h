// Themed icons (Breeze on Plasma) with bundled SVG fallbacks.
#pragma once

#include "core/Item.h"

#include <QIcon>

namespace kn {

inline QIcon themedIcon(const QString& name, const QString& fallback)
{
    if (QIcon::hasThemeIcon(name)) return QIcon::fromTheme(name);
    return QIcon(fallback);
}

inline QIcon itemIcon(const ItemType* t)
{
    return themedIcon(t->meta().icon, t->meta().fallbackIcon);
}

} // namespace kn
