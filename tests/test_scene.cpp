// Scene save/load round trips, schema versioning and migration.
#include "core/Scene.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace kn;

class TestScene : public QObject {
    Q_OBJECT
private slots:
    void roundTripEveryItemType()
    {
        SceneData s = SceneData::empty();
        ItemId id = 1;
        for (const ItemType* t : ItemRegistry::instance().types()) {
            if (t->kind() == ItemKind::World) continue;
            ItemDef d = t->makeDefault(id++, {1.25, -3.5});
            // Perturb every double so non-default values are exercised.
            const auto& defs = t->schema().defs();
            for (size_t i = 0; i < defs.size(); ++i)
                if (defs[i].type == PropType::Double && !defs[i].has(PF_Transient))
                    d.props[i] = clampValue(defs[i], defs[i].min + (defs[i].max - defs[i].min) * 0.3141592653589793);
            t->propertyChanged(d, -1);
            s.items.push_back(d);
        }
        s.view.center = {3.25, -1.0 / 3.0};
        s.view.pixelsPerMetre = 73.5;
        const QByteArray bytes = sceneToBytes(s);
        const SceneLoadResult r = sceneFromBytes(bytes);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join('\n')));
        QCOMPARE(r.scene.items.size(), s.items.size());
        for (size_t i = 0; i < s.items.size(); ++i) {
            QCOMPARE(r.scene.items[i].id, s.items[i].id);
            QCOMPARE(r.scene.items[i].type, s.items[i].type);
            QVERIFY2(r.scene.items[i].props == s.items[i].props, qPrintable(s.items[i].type->typeId()));
        }
        QCOMPARE(r.scene.view.center, s.view.center);
        QCOMPARE(sceneToBytes(r.scene), bytes);
    }

    void humanReadable()
    {
        SceneData s = SceneData::empty();
        s.items.push_back(ItemRegistry::instance().find("body.box")->makeDefault(1, {0, 2}));
        const QByteArray bytes = sceneToBytes(s);
        QVERIFY(bytes.contains("\"schemaVersion\": 2"));
        QVERIFY(bytes.contains("\"type\": \"body.box\""));
        QVERIFY(bytes.contains("\"position\""));
        QVERIFY(bytes.contains('\n')); // indented
    }

    void migrateVersion1()
    {
        const QByteArray v1 = R"({
            "format": "kinetic-scene", "version": 1,
            "settings": { "gravity": [0, -3.7] },
            "items": [ { "id": 7, "kind": "box", "name": "Crate",
                         "props": { "position": [1, 2], "rotation": 90, "width": 2 } } ]
        })";
        const SceneLoadResult r = sceneFromBytes(v1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.fileVersion, 1);
        QCOMPARE(int(r.scene.items.size()), 1);
        const ItemDef& d = r.scene.items[0];
        QCOMPARE(d.type->typeId(), QStringLiteral("body.box"));
        QCOMPARE(d.name, QStringLiteral("Crate"));
        QVERIFY(std::abs(d.type->pose(d).angle() - kPi / 2) < 1e-12);
        QCOMPARE(d.d(d.type->schema().indexOf("width")), 2.0);
        const int g = r.scene.world.type->schema().indexOf("gravity");
        QCOMPARE(r.scene.world.v(g), Vec2(0, -3.7));
    }

    void rejectsNewerAndInvalid()
    {
        QVERIFY(!sceneFromBytes(R"({"format":"kinetic-scene","schemaVersion":99,"items":[]})").ok);
        QVERIFY(!sceneFromBytes("not json").ok);
        QVERIFY(!sceneFromBytes(R"({"format":"something-else","schemaVersion":2})").ok);
        QVERIFY(!sceneFromBytes(R"([1,2,3])").ok);
    }

    void repairsBadItems()
    {
        const SceneLoadResult r = sceneFromBytes(R"({
            "format": "kinetic-scene", "schemaVersion": 2,
            "items": [
              { "id": 1, "type": "body.box", "props": { "width": "wide", "height": 1e9, "bogus": 3 } },
              { "id": 1, "type": "body.circle" },
              { "id": 2, "type": "no.such.type" },
              { "id": 3, "type": "body.circle", "parent": 999 }
            ]})");
        QVERIFY(r.ok);
        QCOMPARE(int(r.scene.items.size()), 2);
        const ItemDef& box = r.scene.items[0];
        const auto& sch = box.type->schema();
        QCOMPARE(box.d(sch.indexOf("width")), 1.0);                // invalid -> default
        QCOMPARE(box.d(sch.indexOf("height")), sch[sch.indexOf("height")].max); // clamped
        QCOMPARE(r.scene.items[1].parent, ItemId(0));             // dangling parent dropped
        QVERIFY(r.warnings.size() >= 4);
    }
};

QTEST_GUILESS_MAIN(TestScene)
#include "test_scene.moc"
