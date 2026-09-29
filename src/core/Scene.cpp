#include "Scene.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <set>

namespace kn {

SceneData SceneData::empty()
{
    SceneData s;
    const ItemType* wt = ItemRegistry::instance().find(QStringLiteral("world"));
    Q_ASSERT(wt);
    s.world = wt->makeDefault(0, {});
    s.world.name = QStringLiteral("World");
    return s;
}

ItemId SceneData::maxId() const
{
    ItemId m = 0;
    for (const ItemDef& d : items) m = std::max(m, d.id);
    return m;
}

int SceneData::indexOf(ItemId id) const
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == id) return int(i);
    return -1;
}

const ItemDef* SceneData::find(ItemId id) const
{
    const int i = indexOf(id);
    return i >= 0 ? &items[size_t(i)] : nullptr;
}

ItemDef* SceneData::find(ItemId id)
{
    const int i = indexOf(id);
    return i >= 0 ? &items[size_t(i)] : nullptr;
}

QJsonObject itemToJson(const ItemDef& def)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), double(def.id));
    o.insert(QStringLiteral("type"), def.type->typeId());
    o.insert(QStringLiteral("name"), def.name);
    if (def.parent) o.insert(QStringLiteral("parent"), double(def.parent));
    if (!def.visible) o.insert(QStringLiteral("visible"), false);
    if (def.locked) o.insert(QStringLiteral("locked"), true);
    o.insert(QStringLiteral("props"), def.type->toJson(def));
    return o;
}

bool itemFromJson(const QJsonObject& o, ItemDef& out, QString* error, QStringList* warnings)
{
    const QString typeId = o.value(QStringLiteral("type")).toString();
    const ItemType* t = ItemRegistry::instance().find(typeId);
    if (!t) {
        if (error) *error = QStringLiteral("unknown item type '%1'").arg(typeId);
        return false;
    }
    const double idv = o.value(QStringLiteral("id")).toDouble(-1);
    if (!(idv >= 0 && idv <= 4.0e9) || idv != std::floor(idv)) {
        if (error) *error = QStringLiteral("item of type '%1' has an invalid id").arg(typeId);
        return false;
    }
    out = t->makeDefault(ItemId(idv), {});
    out.name = o.value(QStringLiteral("name")).toString(out.name);
    out.parent = ItemId(std::max(0.0, o.value(QStringLiteral("parent")).toDouble(0)));
    out.visible = o.value(QStringLiteral("visible")).toBool(true);
    out.locked = o.value(QStringLiteral("locked")).toBool(false);
    t->fromJson(out, o.value(QStringLiteral("props")).toObject(), warnings);
    return true;
}

QJsonObject sceneToJson(const SceneData& scene)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("kinetic-scene"));
    root.insert(QStringLiteral("schemaVersion"), kSceneSchemaVersion);
    root.insert(QStringLiteral("world"), scene.world.type->toJson(scene.world));
    QJsonObject view;
    view.insert(QStringLiteral("center"), QJsonArray{scene.view.center.x, scene.view.center.y});
    view.insert(QStringLiteral("pixelsPerMetre"), scene.view.pixelsPerMetre);
    root.insert(QStringLiteral("view"), view);
    QJsonArray items;
    for (const ItemDef& d : scene.items) items.append(itemToJson(d));
    root.insert(QStringLiteral("items"), items);
    return root;
}

QByteArray sceneToBytes(const SceneData& scene)
{
    return QJsonDocument(sceneToJson(scene)).toJson(QJsonDocument::Indented);
}

// --- migrations -----------------------------------------------------------------
//
// Version 1 was the draft format used during early development:
//   { "format": "kinetic-scene", "version": 1, "settings": {...},
//     "items": [ { "id", "kind": "box", "name", "props": { "rotation": <degrees>, ... } } ] }
// Version 2 renamed "version" -> "schemaVersion", "settings" -> "world",
// "kind" -> "type" (namespaced ids such as "body.box") and stores angles in
// radians under "angle".

static bool migrate1to2(QJsonObject& root, QString* error)
{
    Q_UNUSED(error);
    root.remove(QStringLiteral("version"));
    if (root.contains(QStringLiteral("settings"))) {
        root.insert(QStringLiteral("world"), root.value(QStringLiteral("settings")));
        root.remove(QStringLiteral("settings"));
    }
    static const QHash<QString, QString> kindMap = {
        {QStringLiteral("box"), QStringLiteral("body.box")},
        {QStringLiteral("circle"), QStringLiteral("body.circle")},
        {QStringLiteral("polygon"), QStringLiteral("body.polygon")},
        {QStringLiteral("capsule"), QStringLiteral("body.capsule")},
        {QStringLiteral("freehand"), QStringLiteral("body.freehand")},
        {QStringLiteral("ground"), QStringLiteral("body.ground")},
        {QStringLiteral("wall"), QStringLiteral("body.wall")},
        {QStringLiteral("group"), QStringLiteral("group")},
    };
    QJsonArray items = root.value(QStringLiteral("items")).toArray();
    for (qsizetype i = 0; i < items.size(); ++i) {
        QJsonObject it = items[i].toObject();
        if (it.contains(QStringLiteral("kind"))) {
            const QString k = it.value(QStringLiteral("kind")).toString();
            it.insert(QStringLiteral("type"), kindMap.value(k, k));
            it.remove(QStringLiteral("kind"));
        }
        QJsonObject props = it.value(QStringLiteral("props")).toObject();
        if (props.contains(QStringLiteral("rotation"))) {
            props.insert(QStringLiteral("angle"), props.value(QStringLiteral("rotation")).toDouble() / kDegPerRad);
            props.remove(QStringLiteral("rotation"));
        }
        it.insert(QStringLiteral("props"), props);
        items[i] = it;
    }
    root.insert(QStringLiteral("items"), items);
    return true;
}

static bool migrateOneStep(QJsonObject& root, int from, QString* error)
{
    switch (from) {
    case 1: return migrate1to2(root, error);
    default:
        if (error) *error = QStringLiteral("no migration from schema version %1").arg(from);
        return false;
    }
}

bool migrateScene(QJsonObject& root, int version, QString* error)
{
    while (version < kSceneSchemaVersion) {
        if (!migrateOneStep(root, version, error)) return false;
        ++version;
        root.insert(QStringLiteral("schemaVersion"), version);
    }
    return true;
}

SceneLoadResult sceneFromJson(QJsonObject root)
{
    SceneLoadResult r;
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("kinetic-scene")) {
        r.error = QStringLiteral("not a Kinetic scene file (missing \"format\": \"kinetic-scene\")");
        return r;
    }
    int version = root.value(QStringLiteral("schemaVersion")).toInt(0);
    if (version == 0) version = root.value(QStringLiteral("version")).toInt(0);
    r.fileVersion = version;
    if (version <= 0) {
        r.error = QStringLiteral("missing schema version");
        return r;
    }
    if (version > kSceneSchemaVersion) {
        r.error = QStringLiteral("the file uses schema version %1, but this build of Kinetic only understands up to "
                                 "version %2; please update Kinetic")
                      .arg(version)
                      .arg(kSceneSchemaVersion);
        return r;
    }
    if (!migrateScene(root, version, &r.error)) return r;

    r.scene = SceneData::empty();
    r.scene.world.type->fromJson(r.scene.world, root.value(QStringLiteral("world")).toObject(), &r.warnings);

    const QJsonObject view = root.value(QStringLiteral("view")).toObject();
    const QJsonArray c = view.value(QStringLiteral("center")).toArray();
    if (c.size() == 2) r.scene.view.center = {c[0].toDouble(), c[1].toDouble()};
    r.scene.view.pixelsPerMetre = std::clamp(view.value(QStringLiteral("pixelsPerMetre")).toDouble(50.0), 0.5, 5000.0);

    std::set<ItemId> ids;
    for (const QJsonValue& v : root.value(QStringLiteral("items")).toArray()) {
        ItemDef d;
        QString err;
        if (!itemFromJson(v.toObject(), d, &err, &r.warnings)) {
            r.warnings << err + QStringLiteral(" (item skipped)");
            continue;
        }
        if (d.id == 0 || !ids.insert(d.id).second) {
            r.warnings << QStringLiteral("duplicate or zero item id %1 (item skipped)").arg(d.id);
            continue;
        }
        r.scene.items.push_back(std::move(d));
    }
    // Drop dangling parents.
    for (ItemDef& d : r.scene.items)
        if (d.parent && !ids.count(d.parent)) d.parent = 0;
    r.ok = true;
    return r;
}

SceneLoadResult sceneFromBytes(const QByteArray& bytes)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError) {
        SceneLoadResult r;
        r.error = QStringLiteral("JSON error at offset %1: %2").arg(pe.offset).arg(pe.errorString());
        return r;
    }
    if (!doc.isObject()) {
        SceneLoadResult r;
        r.error = QStringLiteral("the file does not contain a JSON object");
        return r;
    }
    return sceneFromJson(doc.object());
}

} // namespace kn
