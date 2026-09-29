// Scene document data and its versioned JSON format.
#pragma once

#include "Item.h"

#include <QByteArray>

namespace kn {

// Current scene file schema version. Bump when the format changes and add a
// migration step in Scene.cpp (migrateOneStep).
constexpr int kSceneSchemaVersion = 2;

struct ViewState {
    Vec2 center{0.0, 4.0};
    double pixelsPerMetre = 50.0;
};

struct SceneData {
    ItemDef world;              // WorldSettingsItem definition
    std::vector<ItemDef> items; // creation order
    ViewState view;

    static SceneData empty();
    ItemId maxId() const;
    const ItemDef* find(ItemId id) const;
    ItemDef* find(ItemId id);
    int indexOf(ItemId id) const;
};

struct SceneLoadResult {
    bool ok = false;
    QString error;
    QStringList warnings;
    int fileVersion = 0;
    SceneData scene;
};

QJsonObject itemToJson(const ItemDef& def);
// Returns false on a fatal error (unknown type, bad id); warnings collect
// recoverable problems.
bool itemFromJson(const QJsonObject& o, ItemDef& out, QString* error, QStringList* warnings);

QJsonObject sceneToJson(const SceneData& scene);
QByteArray sceneToBytes(const SceneData& scene);
SceneLoadResult sceneFromJson(QJsonObject root);
SceneLoadResult sceneFromBytes(const QByteArray& bytes);

// Upgrades `root` from `version` to kSceneSchemaVersion in place.
bool migrateScene(QJsonObject& root, int version, QString* error);

} // namespace kn
