#include "ExplorerPanel.h"

#include "Icons.h"

#include <QHeaderView>
#include <QIODevice>
#include <QMimeData>
#include <QTreeView>
#include <QVBoxLayout>

namespace kn {

namespace {
constexpr const char* kIdsMime = "application/x-kinetic-ids";
}

ExplorerModel::ExplorerModel(Document* doc, QObject* parent) : QAbstractItemModel(parent), doc_(doc)
{
    rebuild();
    auto reset = [this] {
        beginResetModel();
        rebuild();
        endResetModel();
    };
    connect(doc_, &Document::itemAdded, this, reset);
    connect(doc_, &Document::itemRemoved, this, reset);
    connect(doc_, &Document::orderChanged, this, reset);
    connect(doc_, &Document::sceneReset, this, reset);
    connect(doc_, &Document::itemChanged, this, [this, reset](ItemId id, quint64, bool meta) {
        if (!meta) return;
        const ItemDef* d = doc_->item(id);
        // Parent changes alter the tree shape; other meta changes are row updates.
        const auto rit = row_.find(id);
        const ItemId shownParent = [&] {
            for (const auto& [p, kids] : children_)
                if (std::find(kids.begin(), kids.end(), id) != kids.end()) return p;
            return ItemId(0);
        }();
        if (!d || rit == row_.end() || shownParent != d->parent) {
            reset();
            return;
        }
        emit dataChanged(indexOf(id, 0), indexOf(id, ColumnCount - 1));
    });
}

void ExplorerModel::rebuild()
{
    children_.clear();
    row_.clear();
    for (const ItemDef& d : doc_->scene().items) {
        const ItemId p = (d.parent && doc_->item(d.parent)) ? d.parent : 0;
        auto& v = children_[p];
        row_[d.id] = int(v.size());
        v.push_back(d.id);
    }
}

QModelIndex ExplorerModel::index(int row, int column, const QModelIndex& parent) const
{
    const auto it = children_.find(idOf(parent));
    if (it == children_.end() || row < 0 || row >= int(it->second.size())) return {};
    return createIndex(row, column, quintptr(it->second[size_t(row)]));
}

QModelIndex ExplorerModel::indexOf(ItemId id, int column) const
{
    const auto it = row_.find(id);
    if (it == row_.end()) return {};
    return createIndex(it->second, column, quintptr(id));
}

QModelIndex ExplorerModel::parent(const QModelIndex& child) const
{
    const ItemDef* d = doc_->item(idOf(child));
    if (!d || !d->parent || !doc_->item(d->parent)) return {};
    return indexOf(d->parent, 0);
}

int ExplorerModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0) return 0;
    const auto it = children_.find(idOf(parent));
    return it == children_.end() ? 0 : int(it->second.size());
}

QVariant ExplorerModel::data(const QModelIndex& index, int role) const
{
    const ItemDef* d = doc_->item(idOf(index));
    if (!d) return {};
    switch (index.column()) {
    case Name:
        if (role == Qt::DisplayRole || role == Qt::EditRole) return d->name;
        if (role == Qt::DecorationRole) return itemIcon(d->type);
        if (role == Qt::ToolTipRole) return QStringLiteral("%1 (id %2)").arg(d->type->meta().name).arg(d->id);
        if (role == Qt::ForegroundRole && !doc_->effectiveVisible(*d)) return QVariant(); // style decides
        break;
    case Visible:
        if (role == Qt::DecorationRole)
            return d->visible ? themedIcon(QStringLiteral("visibility"), QStringLiteral(":/icons/visible.svg"))
                              : themedIcon(QStringLiteral("hint"), QStringLiteral(":/icons/hidden.svg"));
        if (role == Qt::ToolTipRole) return d->visible ? tr("Visible (click to hide)") : tr("Hidden (click to show)");
        break;
    case Locked:
        if (role == Qt::DecorationRole)
            return d->locked ? themedIcon(QStringLiteral("object-locked"), QStringLiteral(":/icons/locked.svg"))
                             : themedIcon(QStringLiteral("object-unlocked"), QStringLiteral(":/icons/unlocked.svg"));
        if (role == Qt::ToolTipRole)
            return d->locked ? tr("Locked in the viewport (click to unlock)") : tr("Unlocked (click to lock)");
        break;
    default: break;
    }
    return {};
}

bool ExplorerModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (index.column() != Name || role != Qt::EditRole) return false;
    doc_->rename(idOf(index), value.toString());
    return true;
}

Qt::ItemFlags ExplorerModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::ItemIsDropEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    if (index.column() == Name) f |= Qt::ItemIsEditable;
    const ItemDef* d = doc_->item(idOf(index));
    if (d && d->type->kind() == ItemKind::Group) f |= Qt::ItemIsDropEnabled;
    return f;
}

QVariant ExplorerModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case Name: return tr("Name");
    case Visible: return QString();
    case Locked: return QString();
    default: return {};
    }
}

QStringList ExplorerModel::mimeTypes() const { return {QLatin1String(kIdsMime)}; }

QMimeData* ExplorerModel::mimeData(const QModelIndexList& indexes) const
{
    QByteArray bytes;
    QList<ItemId> seen;
    for (const QModelIndex& i : indexes) {
        const ItemId id = idOf(i);
        if (id && !seen.contains(id)) {
            seen << id;
            bytes += QByteArray::number(id) + ' ';
        }
    }
    auto* m = new QMimeData;
    m->setData(QLatin1String(kIdsMime), bytes);
    return m;
}

bool ExplorerModel::dropMimeData(const QMimeData* data, Qt::DropAction, int row, int, const QModelIndex& parent)
{
    QList<ItemId> ids;
    for (const QByteArray& b : data->data(QLatin1String(kIdsMime)).split(' '))
        if (!b.isEmpty()) ids << ItemId(b.toULong());
    if (ids.isEmpty()) return false;
    const ItemId parentId = idOf(parent);
    ItemId before = 0;
    const auto it = children_.find(parentId);
    if (row >= 0 && it != children_.end() && row < int(it->second.size())) before = it->second[size_t(row)];
    // The model resets itself from the document signals; tell the view nothing moved here.
    QMetaObject::invokeMethod(
        this, [this, ids, parentId, before] { doc_->reparent(ids, parentId, before); }, Qt::QueuedConnection);
    return false;
}

// --- panel ------------------------------------------------------------------------------------

ExplorerPanel::ExplorerPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    model_ = new ExplorerModel(doc_, this);
    view_ = new QTreeView(this);
    view_->setModel(model_);
    view_->setHeaderHidden(true);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setDragDropMode(QAbstractItemView::InternalMove);
    view_->setDefaultDropAction(Qt::MoveAction);
    view_->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    view_->setUniformRowHeights(true);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->header()->setStretchLastSection(false);
    view_->header()->setSectionResizeMode(ExplorerModel::Name, QHeaderView::Stretch);
    view_->header()->setSectionResizeMode(ExplorerModel::Visible, QHeaderView::Fixed);
    view_->header()->setSectionResizeMode(ExplorerModel::Locked, QHeaderView::Fixed);
    view_->header()->resizeSection(ExplorerModel::Visible, 28);
    view_->header()->resizeSection(ExplorerModel::Locked, 28);
    lay->addWidget(view_);

    connect(view_, &QTreeView::clicked, this, [this](const QModelIndex& i) {
        const ItemDef* d = doc_->item(model_->idOf(i));
        if (!d) return;
        if (i.column() == ExplorerModel::Visible) doc_->setVisible(d->id, !d->visible);
        if (i.column() == ExplorerModel::Locked) doc_->setLocked(d->id, !d->locked);
    });
    connect(view_, &QTreeView::customContextMenuRequested, this,
            [this](const QPoint& p) { emit contextMenuRequested(view_->viewport()->mapToGlobal(p)); });
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ExplorerPanel::syncToDocument);
    connect(doc_, &Document::selectionChanged, this, &ExplorerPanel::syncFromDocument);
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        view_->expandAll();
        syncFromDocument();
    });
    view_->expandAll();
}

void ExplorerPanel::syncToDocument()
{
    if (syncing_) return;
    QList<ItemId> ids;
    for (const QModelIndex& i : view_->selectionModel()->selectedRows(0)) ids << model_->idOf(i);
    syncing_ = true;
    doc_->setSelection(ids);
    syncing_ = false;
}

void ExplorerPanel::syncFromDocument()
{
    if (syncing_) return;
    syncing_ = true;
    QItemSelection sel;
    for (ItemId id : doc_->selection()) {
        const QModelIndex a = model_->indexOf(id, 0);
        if (a.isValid()) sel.select(a, model_->indexOf(id, ExplorerModel::ColumnCount - 1));
    }
    view_->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    if (!doc_->selection().isEmpty()) view_->scrollTo(model_->indexOf(doc_->selection().last()));
    syncing_ = false;
}

} // namespace kn
