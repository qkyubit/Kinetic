// Scene tree: grouping, rename, visibility and lock toggles, drag to reparent.
#pragma once

#include "Document.h"

#include <QAbstractItemModel>
#include <QWidget>

class QTreeView;

namespace kn {

class ExplorerModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column { Name, Visible, Locked, ColumnCount };
    explicit ExplorerModel(Document* doc, QObject* parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColumnCount; }
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

    ItemId idOf(const QModelIndex& i) const { return i.isValid() ? ItemId(i.internalId()) : 0; }
    QModelIndex indexOf(ItemId id, int column = 0) const;

private:
    void rebuild();
    Document* doc_;
    std::unordered_map<ItemId, std::vector<ItemId>> children_;
    std::unordered_map<ItemId, int> row_;
};

class ExplorerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ExplorerPanel(Document* doc, QWidget* parent = nullptr);

signals:
    void contextMenuRequested(QPoint global);

private:
    void syncFromDocument();
    void syncToDocument();

    Document* doc_;
    ExplorerModel* model_;
    QTreeView* view_;
    bool syncing_ = false;
};

} // namespace kn
