// Categorized, searchable list of item types. Items can be dragged into the
// viewport, or activated (double-click / Enter) to place at the view centre.
#pragma once

#include "core/Item.h"

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace kn {

class ToolboxPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolboxPanel(QWidget* parent = nullptr);
    static const char* mimeType() { return "application/x-kinetic-itemtype"; }

signals:
    void activated(const kn::ItemType* type);

private:
    void populate();
    void applyFilter(const QString& text);

    QLineEdit* search_;
    QTreeWidget* tree_;
};

} // namespace kn
