// The editable scene (UI thread). Every modification goes through QUndoStack
// commands; signals tell the viewport, panels and simulation bridge what changed.
#pragma once

#include "core/Scene.h"

#include <QObject>
#include <QUndoStack>

#include <unordered_map>

namespace kn {

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    const SceneData& scene() const { return scene_; }
    const ItemDef& world() const { return scene_.world; }
    const ItemDef* item(ItemId id) const;
    int indexOf(ItemId id) const;
    QUndoStack* undoStack() { return &undo_; }
    ItemId allocateId() { return nextId_++; }

    QString filePath() const { return filePath_; }
    void setFilePath(const QString& p) { filePath_ = p; }
    ViewState& view() { return scene_.view; }

    // Replaces the whole scene (new/open). Clears undo history.
    void reset(SceneData scene);

    // --- undoable operations ----------------------------------------------------------
    void addItems(std::vector<ItemDef> defs, const QString& text);
    // Deletes items plus their descendants and items that reference them.
    void deleteItems(const QList<ItemId>& ids);
    // Generic edit: `after` replaces the items with the same ids. When
    // `continuous` is set, consecutive edits of the same items merge into one
    // undo step until endContinuousEdit() is called.
    void editItems(const std::vector<ItemDef>& after, const QString& text, bool continuous = false);
    void setProperty(const QList<ItemId>& ids, int index, const PropValue& value, bool continuous = false);
    void setWorldProperty(int index, const PropValue& value, bool continuous = false);
    void endContinuousEdit() { ++mergeSession_; }
    void rename(ItemId id, const QString& name);
    void setVisible(ItemId id, bool visible);
    void setLocked(ItemId id, bool locked);
    // Moves items under `parent` (0 = root), before `beforeId` (0 = at end).
    void reparent(const QList<ItemId>& ids, ItemId parent, ItemId beforeId = 0);
    ItemId groupItems(const QList<ItemId>& ids);
    void ungroup(ItemId groupId);

    // Items and their descendants (depth-first, document order).
    QList<ItemId> withDescendants(const QList<ItemId>& ids) const;
    bool effectiveVisible(const ItemDef& d) const;
    bool effectiveLocked(const ItemDef& d) const;
    QList<ItemId> children(ItemId parent) const;

    // --- selection (not undoable) --------------------------------------------------------
    const QList<ItemId>& selection() const { return selection_; }
    void setSelection(const QList<ItemId>& ids);
    bool isSelected(ItemId id) const { return selection_.contains(id); }

    // --- clipboard helpers --------------------------------------------------------------------
    QByteArray serializeItems(const QList<ItemId>& ids) const;
    // Parses items, assigns fresh ids, remaps references and offsets positions.
    std::vector<ItemDef> deserializeItems(const QByteArray& json, Vec2 offset, QString* error);

    // --- primitives used by undo commands (not undoable themselves) -----------------------
    void insertItemAt(int index, const ItemDef& def);
    void removeItem(ItemId id);
    void replaceItem(const ItemDef& def);
    void replaceWorld(const ItemDef& def);
    void setOrder(const std::vector<std::pair<ItemId, ItemId>>& orderAndParents);
    std::vector<std::pair<ItemId, ItemId>> order() const;
    int mergeSession() const { return mergeSession_; }

signals:
    void itemAdded(kn::ItemId id);
    void itemRemoved(kn::ItemId id);
    // mask: changed property bits (bit 63 = index >= 63); meta: name/visible/locked/parent.
    void itemChanged(kn::ItemId id, quint64 mask, bool meta);
    void worldChanged(quint64 mask);
    void orderChanged();
    void sceneReset();
    void selectionChanged();

private:
    void reindex();
    static uint64_t diffMask(const ItemDef& a, const ItemDef& b);

    SceneData scene_;
    std::unordered_map<ItemId, int> index_;
    QUndoStack undo_;
    QList<ItemId> selection_;
    QString filePath_;
    ItemId nextId_ = 1;
    int mergeSession_ = 0;
    friend class EditItemsCommand;
};

} // namespace kn
