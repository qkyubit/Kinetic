#include "Document.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <set>

namespace kn {

// --- undo commands -------------------------------------------------------------------------

namespace {

class AddItemsCommand : public QUndoCommand {
public:
    AddItemsCommand(Document* doc, std::vector<ItemDef> defs, const QString& text)
        : QUndoCommand(text), doc_(doc), defs_(std::move(defs))
    {
        int base = int(doc_->scene().items.size());
        for (size_t i = 0; i < defs_.size(); ++i) indices_.push_back(base + int(i));
    }
    void redo() override
    {
        for (size_t i = 0; i < defs_.size(); ++i) doc_->insertItemAt(indices_[i], defs_[i]);
    }
    void undo() override
    {
        for (auto it = defs_.rbegin(); it != defs_.rend(); ++it) doc_->removeItem(it->id);
    }

private:
    Document* doc_;
    std::vector<ItemDef> defs_;
    std::vector<int> indices_;
};

class RemoveItemsCommand : public QUndoCommand {
public:
    RemoveItemsCommand(Document* doc, const QList<ItemId>& ids, const QString& text) : QUndoCommand(text), doc_(doc)
    {
        for (ItemId id : ids) {
            const int idx = doc_->indexOf(id);
            if (idx >= 0) removed_.push_back({idx, *doc_->item(id)});
        }
        std::sort(removed_.begin(), removed_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    void redo() override
    {
        for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) doc_->removeItem(it->second.id);
    }
    void undo() override
    {
        for (const auto& [idx, def] : removed_) doc_->insertItemAt(idx, def);
    }
    bool empty() const { return removed_.empty(); }

private:
    Document* doc_;
    std::vector<std::pair<int, ItemDef>> removed_;
};

class OrderCommand : public QUndoCommand {
public:
    OrderCommand(Document* doc, std::vector<std::pair<ItemId, ItemId>> before,
                 std::vector<std::pair<ItemId, ItemId>> after, const QString& text)
        : QUndoCommand(text), doc_(doc), before_(std::move(before)), after_(std::move(after))
    {
    }
    void redo() override { doc_->setOrder(after_); }
    void undo() override { doc_->setOrder(before_); }

private:
    Document* doc_;
    std::vector<std::pair<ItemId, ItemId>> before_, after_;
};

class WorldCommand : public QUndoCommand {
public:
    WorldCommand(Document* doc, ItemDef before, ItemDef after, bool continuous)
        : QUndoCommand(QObject::tr("Edit world settings")), doc_(doc), before_(std::move(before)),
          after_(std::move(after)), continuous_(continuous), session_(doc->mergeSession())
    {
    }
    int id() const override { return 2; }
    bool mergeWith(const QUndoCommand* other) override
    {
        auto* o = static_cast<const WorldCommand*>(other);
        if (!continuous_ || !o->continuous_ || o->session_ != session_) return false;
        after_ = o->after_;
        return true;
    }
    void redo() override { doc_->replaceWorld(after_); }
    void undo() override { doc_->replaceWorld(before_); }

private:
    Document* doc_;
    ItemDef before_, after_;
    bool continuous_;
    int session_;
};

} // namespace

class EditItemsCommand : public QUndoCommand {
public:
    EditItemsCommand(Document* doc, std::vector<ItemDef> before, std::vector<ItemDef> after, const QString& text,
                     bool continuous)
        : QUndoCommand(text), doc_(doc), before_(std::move(before)), after_(std::move(after)),
          continuous_(continuous), session_(doc->mergeSession())
    {
    }
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand* other) override
    {
        auto* o = static_cast<const EditItemsCommand*>(other);
        if (!continuous_ || !o->continuous_ || o->session_ != session_ || o->after_.size() != after_.size())
            return false;
        for (size_t i = 0; i < after_.size(); ++i)
            if (after_[i].id != o->after_[i].id) return false;
        after_ = o->after_;
        return true;
    }
    void redo() override
    {
        for (const ItemDef& d : after_) doc_->replaceItem(d);
    }
    void undo() override
    {
        for (const ItemDef& d : before_) doc_->replaceItem(d);
    }

private:
    Document* doc_;
    std::vector<ItemDef> before_, after_;
    bool continuous_;
    int session_;
};

// --- Document ------------------------------------------------------------------------------------

Document::Document(QObject* parent) : QObject(parent)
{
    scene_ = SceneData::empty();
    undo_.setUndoLimit(10000);
}

void Document::reindex()
{
    index_.clear();
    for (size_t i = 0; i < scene_.items.size(); ++i) index_[scene_.items[i].id] = int(i);
}

const ItemDef* Document::item(ItemId id) const
{
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &scene_.items[size_t(it->second)];
}

int Document::indexOf(ItemId id) const
{
    const auto it = index_.find(id);
    return it == index_.end() ? -1 : it->second;
}

void Document::reset(SceneData scene)
{
    undo_.clear();
    scene_ = std::move(scene);
    reindex();
    nextId_ = scene_.maxId() + 1;
    selection_.clear();
    undo_.setClean();
    emit sceneReset();
    emit selectionChanged();
}

uint64_t Document::diffMask(const ItemDef& a, const ItemDef& b)
{
    uint64_t m = 0;
    const size_t n = std::min(a.props.size(), b.props.size());
    for (size_t i = 0; i < n; ++i)
        if (!(a.props[i] == b.props[i])) m |= i >= 63 ? (1ull << 63) : (1ull << i);
    return m;
}

void Document::insertItemAt(int index, const ItemDef& def)
{
    index = std::clamp(index, 0, int(scene_.items.size()));
    scene_.items.insert(scene_.items.begin() + index, def);
    reindex();
    nextId_ = std::max(nextId_, def.id + 1);
    emit itemAdded(def.id);
}

void Document::removeItem(ItemId id)
{
    const int idx = indexOf(id);
    if (idx < 0) return;
    scene_.items.erase(scene_.items.begin() + idx);
    reindex();
    if (selection_.removeAll(id) > 0) emit selectionChanged();
    emit itemRemoved(id);
}

void Document::replaceItem(const ItemDef& def)
{
    const int idx = indexOf(def.id);
    if (idx < 0) return;
    ItemDef& cur = scene_.items[size_t(idx)];
    const uint64_t mask = diffMask(cur, def);
    const bool meta = cur.name != def.name || cur.visible != def.visible || cur.locked != def.locked ||
                      cur.parent != def.parent;
    cur = def;
    emit itemChanged(def.id, mask, meta);
}

void Document::replaceWorld(const ItemDef& def)
{
    const uint64_t mask = diffMask(scene_.world, def);
    scene_.world = def;
    emit worldChanged(mask);
}

std::vector<std::pair<ItemId, ItemId>> Document::order() const
{
    std::vector<std::pair<ItemId, ItemId>> o;
    for (const ItemDef& d : scene_.items) o.push_back({d.id, d.parent});
    return o;
}

void Document::setOrder(const std::vector<std::pair<ItemId, ItemId>>& ord)
{
    std::vector<ItemDef> items;
    items.reserve(ord.size());
    for (const auto& [id, parent] : ord) {
        const ItemDef* d = item(id);
        if (!d) continue;
        items.push_back(*d);
        items.back().parent = parent;
    }
    if (items.size() != scene_.items.size()) return;
    scene_.items = std::move(items);
    reindex();
    emit orderChanged();
}

void Document::addItems(std::vector<ItemDef> defs, const QString& text)
{
    if (defs.empty()) return;
    QList<ItemId> ids;
    for (const ItemDef& d : defs) ids << d.id;
    undo_.push(new AddItemsCommand(this, std::move(defs), text));
    setSelection(ids);
}

QList<ItemId> Document::children(ItemId parent) const
{
    QList<ItemId> out;
    for (const ItemDef& d : scene_.items)
        if (d.parent == parent) out << d.id;
    return out;
}

QList<ItemId> Document::withDescendants(const QList<ItemId>& ids) const
{
    std::set<ItemId> set(ids.begin(), ids.end());
    bool grew = true;
    while (grew) {
        grew = false;
        for (const ItemDef& d : scene_.items)
            if (d.parent && set.count(d.parent) && set.insert(d.id).second) grew = true;
    }
    QList<ItemId> out;
    for (const ItemDef& d : scene_.items)
        if (set.count(d.id)) out << d.id;
    return out;
}

bool Document::effectiveVisible(const ItemDef& d) const
{
    const ItemDef* p = &d;
    for (int guard = 0; p && guard < 1000; ++guard) {
        if (!p->visible) return false;
        p = p->parent ? item(p->parent) : nullptr;
    }
    return true;
}

bool Document::effectiveLocked(const ItemDef& d) const
{
    const ItemDef* p = &d;
    for (int guard = 0; p && guard < 1000; ++guard) {
        if (p->locked) return true;
        p = p->parent ? item(p->parent) : nullptr;
    }
    return false;
}

void Document::deleteItems(const QList<ItemId>& ids)
{
    QList<ItemId> all = withDescendants(ids);
    std::set<ItemId> set(all.begin(), all.end());
    // Items that reference a deleted item (connectors, attachments) go too.
    bool grew = true;
    while (grew) {
        grew = false;
        for (const ItemDef& d : scene_.items) {
            if (set.count(d.id)) continue;
            for (ItemId r : d.type->references(d)) {
                if (set.count(r)) {
                    set.insert(d.id);
                    grew = true;
                    break;
                }
            }
        }
    }
    all = withDescendants(QList<ItemId>(set.begin(), set.end()));
    if (all.isEmpty()) return;
    auto* cmd = new RemoveItemsCommand(this, all, all.size() == 1 ? tr("Delete %1").arg(item(all[0])->name)
                                                                  : tr("Delete %1 items").arg(all.size()));
    undo_.push(cmd);
}

void Document::editItems(const std::vector<ItemDef>& after, const QString& text, bool continuous)
{
    std::vector<ItemDef> before, changed;
    for (const ItemDef& d : after) {
        const ItemDef* cur = item(d.id);
        if (!cur) continue;
        if (diffMask(*cur, d) == 0 && cur->name == d.name && cur->visible == d.visible && cur->locked == d.locked &&
            cur->parent == d.parent)
            continue;
        before.push_back(*cur);
        changed.push_back(d);
    }
    if (changed.empty()) return;
    undo_.push(new EditItemsCommand(this, std::move(before), std::move(changed), text, continuous));
}

void Document::setProperty(const QList<ItemId>& ids, int index, const PropValue& value, bool continuous)
{
    std::vector<ItemDef> after;
    QString label;
    for (ItemId id : ids) {
        const ItemDef* cur = item(id);
        if (!cur || index < 0 || index >= cur->type->schema().size()) continue;
        ItemDef d = *cur;
        const PropDef& pd = d.type->schema()[index];
        d.set(index, clampValue(pd, value));
        d.type->propertyChanged(d, index);
        label = pd.label;
        after.push_back(std::move(d));
    }
    editItems(after, tr("Set %1").arg(label), continuous);
}

void Document::setWorldProperty(int index, const PropValue& value, bool continuous)
{
    ItemDef d = scene_.world;
    if (index < 0 || index >= d.type->schema().size()) return;
    d.set(index, clampValue(d.type->schema()[index], value));
    d.type->propertyChanged(d, index);
    if (diffMask(d, scene_.world) == 0) return;
    undo_.push(new WorldCommand(this, scene_.world, d, continuous));
}

void Document::rename(ItemId id, const QString& name)
{
    const ItemDef* cur = item(id);
    if (!cur || name.trimmed().isEmpty() || cur->name == name) return;
    ItemDef d = *cur;
    d.name = name.trimmed();
    editItems({d}, tr("Rename to %1").arg(d.name));
}

void Document::setVisible(ItemId id, bool visible)
{
    const ItemDef* cur = item(id);
    if (!cur || cur->visible == visible) return;
    ItemDef d = *cur;
    d.visible = visible;
    editItems({d}, visible ? tr("Show %1").arg(d.name) : tr("Hide %1").arg(d.name));
}

void Document::setLocked(ItemId id, bool locked)
{
    const ItemDef* cur = item(id);
    if (!cur || cur->locked == locked) return;
    ItemDef d = *cur;
    d.locked = locked;
    editItems({d}, locked ? tr("Lock %1").arg(d.name) : tr("Unlock %1").arg(d.name));
}

void Document::reparent(const QList<ItemId>& ids, ItemId parent, ItemId beforeId)
{
    // Refuse to move a group into itself or its descendants.
    const QList<ItemId> desc = withDescendants(ids);
    if (parent && desc.contains(parent)) return;
    auto before = order();
    std::vector<std::pair<ItemId, ItemId>> moving, rest;
    for (const auto& e : before) {
        if (ids.contains(e.first))
            moving.push_back({e.first, parent});
        else
            rest.push_back(e);
    }
    if (moving.empty()) return;
    auto pos = rest.end();
    if (beforeId) pos = std::find_if(rest.begin(), rest.end(), [&](const auto& e) { return e.first == beforeId; });
    rest.insert(pos, moving.begin(), moving.end());
    if (rest == before) return;
    undo_.push(new OrderCommand(this, std::move(before), std::move(rest), tr("Move items")));
}

ItemId Document::groupItems(const QList<ItemId>& ids)
{
    if (ids.isEmpty()) return 0;
    const ItemType* gt = ItemRegistry::instance().find(QStringLiteral("group"));
    ItemDef g = gt->makeDefault(allocateId(), {});
    const ItemDef* first = item(ids.first());
    g.parent = first ? first->parent : 0;
    const ItemId gid = g.id;
    undo_.beginMacro(tr("Group %1 items").arg(ids.size()));
    undo_.push(new AddItemsCommand(this, {g}, tr("Add group")));
    reparent(ids, gid);
    undo_.endMacro();
    setSelection({gid});
    return gid;
}

void Document::ungroup(ItemId groupId)
{
    const ItemDef* g = item(groupId);
    if (!g || g->type->kind() != ItemKind::Group) return;
    const QList<ItemId> kids = children(groupId);
    undo_.beginMacro(tr("Ungroup %1").arg(g->name));
    reparent(kids, g->parent, groupId);
    undo_.push(new RemoveItemsCommand(this, {groupId}, tr("Remove group")));
    undo_.endMacro();
    setSelection(kids);
}

void Document::setSelection(const QList<ItemId>& ids)
{
    QList<ItemId> clean;
    for (ItemId id : ids)
        if (item(id) && !clean.contains(id)) clean << id;
    if (clean == selection_) return;
    selection_ = clean;
    emit selectionChanged();
}

QByteArray Document::serializeItems(const QList<ItemId>& ids) const
{
    QJsonArray arr;
    for (ItemId id : withDescendants(ids))
        if (const ItemDef* d = item(id)) arr.append(itemToJson(*d));
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("kinetic-items"));
    root.insert(QStringLiteral("schemaVersion"), kSceneSchemaVersion);
    root.insert(QStringLiteral("items"), arr);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::vector<ItemDef> Document::deserializeItems(const QByteArray& json, Vec2 offset, QString* error)
{
    std::vector<ItemDef> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("kinetic-items")) {
        if (error) *error = tr("The clipboard does not contain Kinetic items.");
        return out;
    }
    std::unordered_map<ItemId, ItemId> remap;
    for (const QJsonValue& v : root.value(QStringLiteral("items")).toArray()) {
        ItemDef d;
        QString err;
        if (!itemFromJson(v.toObject(), d, &err, nullptr)) continue;
        const ItemId newId = allocateId();
        remap[d.id] = newId;
        d.id = newId;
        out.push_back(std::move(d));
    }
    auto mapId = [&](ItemId old) -> ItemId {
        const auto it = remap.find(old);
        if (it != remap.end()) return it->second;
        return item(old) ? old : 0;
    };
    for (ItemDef& d : out) {
        d.parent = d.parent ? mapId(d.parent) : 0;
        const auto& defs = d.type->schema().defs();
        for (size_t i = 0; i < defs.size(); ++i)
            if (defs[i].type == PropType::ItemRef && d.ref(int(i))) d.set(int(i), int64_t(mapId(d.ref(int(i)))));
        // Posed items move by the offset; connectors move only their world-anchored ends.
        d.type->translate(d, offset);
    }
    return out;
}

} // namespace kn
