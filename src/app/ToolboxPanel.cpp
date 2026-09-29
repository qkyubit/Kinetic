#include "ToolboxPanel.h"

#include "Icons.h"

#include <QDrag>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace kn {

namespace {
const ItemType* typeOf(const QTreeWidgetItem* it)
{
    return it ? static_cast<const ItemType*>(it->data(0, Qt::UserRole).value<const void*>()) : nullptr;
}

class ToolboxTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    void startDrag(Qt::DropActions) override
    {
        const ItemType* t = typeOf(currentItem());
        if (!t) return;
        auto* mime = new QMimeData;
        mime->setData(QLatin1String(ToolboxPanel::mimeType()), t->typeId().toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(itemIcon(t).pixmap(32, 32));
        drag->exec(Qt::CopyAction);
    }
};
} // namespace

ToolboxPanel::ToolboxPanel(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search items…"));
    search_->setClearButtonEnabled(true);
    lay->addWidget(search_);
    tree_ = new ToolboxTree(this);
    tree_->setHeaderHidden(true);
    tree_->setDragEnabled(true);
    tree_->setDragDropMode(QAbstractItemView::DragOnly);
    tree_->setIconSize(QSize(22, 22));
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(true);
    lay->addWidget(tree_);
    populate();

    connect(search_, &QLineEdit::textChanged, this, &ToolboxPanel::applyFilter);
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) {
        if (const ItemType* t = typeOf(it)) emit activated(t);
    });
}

void ToolboxPanel::populate()
{
    static const QStringList order = {tr("Bodies"),      tr("Connectors"), tr("Mechanisms"), tr("Fields"),
                                      tr("Fluids"),      tr("Optics"),     tr("Analysis"),   tr("Equations")};
    QHash<QString, QTreeWidgetItem*> cats;
    auto category = [&](const QString& name) {
        if (auto* c = cats.value(name)) return c;
        auto* c = new QTreeWidgetItem;
        c->setText(0, name);
        c->setFlags(Qt::ItemIsEnabled);
        QFont f = c->font(0);
        f.setBold(true);
        c->setFont(0, f);
        cats.insert(name, c);
        return c;
    };
    for (const QString& c : order) category(c);
    for (const ItemType* t : ItemRegistry::instance().types()) {
        const ItemMeta& m = t->meta();
        if (!m.toolbox) continue;
        auto* it = new QTreeWidgetItem(category(m.category));
        it->setText(0, m.name);
        it->setIcon(0, itemIcon(t));
        it->setToolTip(0, m.description);
        it->setData(0, Qt::UserRole, QVariant::fromValue<const void*>(t));
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    }
    QStringList names = order;
    for (auto it = cats.begin(); it != cats.end(); ++it)
        if (!names.contains(it.key())) names << it.key();
    for (const QString& n : names) {
        QTreeWidgetItem* c = cats.value(n);
        if (c && c->childCount() > 0)
            tree_->addTopLevelItem(c);
        else
            delete c;
    }
    tree_->expandAll();
}

void ToolboxPanel::applyFilter(const QString& text)
{
    const QString q = text.trimmed();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* cat = tree_->topLevelItem(i);
        int visible = 0;
        for (int j = 0; j < cat->childCount(); ++j) {
            QTreeWidgetItem* it = cat->child(j);
            const ItemType* t = typeOf(it);
            bool match = q.isEmpty();
            if (!match && t) {
                const ItemMeta& m = t->meta();
                match = m.name.contains(q, Qt::CaseInsensitive) || m.category.contains(q, Qt::CaseInsensitive) ||
                        m.description.contains(q, Qt::CaseInsensitive);
                for (const QString& tag : m.tags) match = match || tag.contains(q, Qt::CaseInsensitive);
            }
            it->setHidden(!match);
            visible += match ? 1 : 0;
        }
        cat->setHidden(visible == 0);
        cat->setExpanded(true);
    }
}

} // namespace kn
