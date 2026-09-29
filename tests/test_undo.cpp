// Undo stack: every document edit can be undone and redone exactly.
#include "app/Document.h"

#include <QTest>

using namespace kn;

namespace {
QByteArray snapshotOf(const Document& d) { return sceneToBytes(d.scene()); }
const ItemType* type(const char* id) { return ItemRegistry::instance().find(QString::fromLatin1(id)); }
} // namespace

class TestUndo : public QObject {
    Q_OBJECT
private slots:
    void everyOperationUndoes()
    {
        Document doc;
        QList<QByteArray> states{snapshotOf(doc)};
        auto checkpoint = [&] { states << snapshotOf(doc); };

        doc.addItems({type("body.ground")->makeDefault(doc.allocateId(), {0, -0.5})}, "add ground");
        checkpoint();
        doc.addItems({type("body.box")->makeDefault(doc.allocateId(), {0, 1}),
                      type("body.circle")->makeDefault(doc.allocateId(), {2, 1})},
                     "add two");
        checkpoint();
        const ItemId box = doc.scene().items[1].id, ball = doc.scene().items[2].id;
        const int width = type("body.box")->schema().indexOf("width");
        doc.setProperty({box}, width, 2.5);
        checkpoint();
        doc.rename(ball, "Ball");
        checkpoint();
        doc.setVisible(ball, false);
        checkpoint();
        doc.setLocked(box, true);
        checkpoint();
        const ItemId g = doc.groupItems({box, ball});
        QVERIFY(g != 0);
        QCOMPARE(doc.item(box)->parent, g);
        checkpoint();
        doc.ungroup(g);
        QVERIFY(!doc.item(g));
        checkpoint();
        doc.reparent({ball}, 0, box); // move ball before box
        checkpoint();
        doc.setWorldProperty(doc.world().type->schema().indexOf("gravity"), Vec2{0, -1.62});
        checkpoint();
        doc.deleteItems({box});
        checkpoint();

        // Undo walks back through every state, redo walks forward again.
        for (int i = int(states.size()) - 2; i >= 0; --i) {
            doc.undoStack()->undo();
            QCOMPARE(snapshotOf(doc), states[i]);
        }
        QVERIFY(!doc.undoStack()->canUndo());
        for (int i = 1; i < int(states.size()); ++i) {
            doc.undoStack()->redo();
            QCOMPARE(snapshotOf(doc), states[i]);
        }
    }

    void continuousEditsMerge()
    {
        Document doc;
        doc.addItems({type("body.box")->makeDefault(doc.allocateId(), {0, 0})}, "add");
        const ItemId id = doc.scene().items[0].id;
        const int width = type("body.box")->schema().indexOf("width");
        const int before = doc.undoStack()->count();
        for (double w = 1.1; w < 3.0; w += 0.1) doc.setProperty({id}, width, w, true); // slider drag
        doc.endContinuousEdit();
        QCOMPARE(doc.undoStack()->count(), before + 1);
        doc.setProperty({id}, width, 4.0, true); // a new drag starts a new step
        doc.endContinuousEdit();
        QCOMPARE(doc.undoStack()->count(), before + 2);
        doc.undoStack()->undo();
        doc.undoStack()->undo();
        QCOMPARE(doc.item(id)->d(width), 1.0);
    }

    void deleteCascadesToDependentsAndChildren()
    {
        Document doc;
        doc.addItems({type("body.box")->makeDefault(doc.allocateId(), {0, 0}),
                      type("body.box")->makeDefault(doc.allocateId(), {3, 0})},
                     "add");
        const ItemId a = doc.scene().items[0].id, b = doc.scene().items[1].id;
        const ItemId g = doc.groupItems({a, b});
        const QByteArray full = snapshotOf(doc);
        doc.deleteItems({g});
        QVERIFY(doc.scene().items.empty());
        doc.undoStack()->undo();
        QCOMPARE(snapshotOf(doc), full);
    }

    void pasteRemapsIds()
    {
        Document doc;
        doc.addItems({type("body.box")->makeDefault(doc.allocateId(), {0, 0})}, "add");
        const QByteArray clip = doc.serializeItems({doc.scene().items[0].id});
        auto pasted = doc.deserializeItems(clip, {1, 0}, nullptr);
        QCOMPARE(int(pasted.size()), 1);
        QVERIFY(pasted[0].id != doc.scene().items[0].id);
        QCOMPARE(pasted[0].type->pose(pasted[0]).p, Vec2(1, 0));
        doc.addItems(pasted, "paste");
        QCOMPARE(int(doc.scene().items.size()), 2);
        doc.undoStack()->undo();
        QCOMPARE(int(doc.scene().items.size()), 1);
    }
};

QTEST_GUILESS_MAIN(TestUndo)
#include "test_undo.moc"
