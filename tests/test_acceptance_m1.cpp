// M1 acceptance: build a box stack by dropping toolbox items into the viewport,
// run it, edit friction mid-run and observe the change, undo every step back to
// an empty scene, then save, reload and get bit-identical results.
#include "app/Document.h"
#include "app/SimController.h"
#include "app/ToolboxPanel.h"
#include "app/Viewport.h"
#include "core/Sim.h"

#include <QElapsedTimer>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

using namespace kn;

namespace {

void dropItem(Viewport& vp, const QString& typeId, Vec2 world)
{
    QMimeData mime;
    mime.setData(QLatin1String(ToolboxPanel::mimeType()), typeId.toUtf8());
    const QPointF pos = vp.toScreen(world);
    QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&vp, &enter);
    QDropEvent drop(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&vp, &drop);
}

std::vector<BodyState> runHeadless(const SceneData& scene, int steps)
{
    SimStart st;
    for (const ItemDef& d : scene.items) st.items.push_back(std::make_shared<ItemDef>(d));
    st.world = std::make_shared<ItemDef>(scene.world);
    st.recording = true;
    SimCore core;
    core.start(st);
    for (int i = 0; i < steps; ++i) core.advance();
    RestoreState rs;
    core.world()->capture(rs);
    std::vector<BodyState> out;
    for (const auto& b : rs.bodies) out.push_back(b.second);
    return out;
}

bool waitFor(const std::function<bool()>& cond, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) QTest::qWait(10);
    return cond();
}

} // namespace

class TestAcceptanceM1 : public QObject {
    Q_OBJECT
private slots:
    void boxStackWorkflow()
    {
        Document doc;
        SimController sim(&doc);
        Viewport vp(&doc, &sim);
        vp.resize(1000, 700);
        vp.setCamera({0, 3}, 50);
        const QString groundType = QStringLiteral("body.ground");
        const QString boxType = QStringLiteral("body.box");

        // Build by dropping items from the toolbox (grid snapping is on).
        dropItem(vp, groundType, {0, -0.5});
        for (int i = 0; i < 3; ++i) dropItem(vp, boxType, {0, 0.5 + i});
        dropItem(vp, boxType, {-6, 0.5}); // a box that will slide
        QCOMPARE(int(doc.scene().items.size()), 5);
        const ItemId slider = doc.scene().items.back().id;
        const ItemDef* sd = doc.item(slider);
        QCOMPARE(sd->type->pose(*sd).p, Vec2(-6, 0.5));

        // Give the sliding box an initial velocity (an undoable property edit).
        const auto& sch = sd->type->schema();
        const int vel = sch.indexOf(QStringLiteral("velocity"));
        const int muS = sch.indexOf(QStringLiteral("staticFriction"));
        const int muK = sch.indexOf(QStringLiteral("kineticFriction"));
        doc.setProperty({slider}, vel, Vec2{6, 0});

        // Run.
        sim.play();
        QVERIFY(waitFor([&] { return sim.live() && sim.snapshot().time > 0.25; }));
        auto sliderVx = [&] {
            for (const BodySnap& b : sim.snapshot().bodies)
                if (b.id == slider) return double(b.vx);
            return -1.0;
        };
        // With wood friction the box decelerates.
        const double v1 = sliderVx();
        QVERIFY(waitFor([&] { return sliderVx() < v1 - 0.5; }));

        // Edit friction mid-run: frictionless ground and box -> speed stays constant.
        QList<ItemId> all;
        for (const ItemDef& d : doc.scene().items) all << d.id;
        doc.setProperty(all, muS, 0.0);
        doc.setProperty(all, muK, 0.0);
        QTest::qWait(150);
        const double v2 = sliderVx();
        QVERIFY(v2 > 0.5);
        QTest::qWait(300);
        const double v3 = sliderVx();
        QVERIFY2(std::abs(v3 - v2) < 0.01, qPrintable(QStringLiteral("v2=%1 v3=%2").arg(v2).arg(v3)));
        sim.pause();

        // Undo every step back to an empty scene (also applied to the live simulation).
        QVERIFY(doc.undoStack()->count() >= 8);
        while (doc.undoStack()->canUndo()) doc.undoStack()->undo();
        QVERIFY(doc.scene().items.empty());
        QVERIFY(waitFor([&] { return sim.live() && sim.snapshot().bodies.empty(); }));

        // Redo everything, then save, reload and compare runs bit for bit.
        while (doc.undoStack()->canRedo()) doc.undoStack()->redo();
        QCOMPARE(int(doc.scene().items.size()), 5);
        sim.reset();
        const QByteArray saved = sceneToBytes(doc.scene());
        const SceneLoadResult loaded = sceneFromBytes(saved);
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        QVERIFY(loaded.warnings.isEmpty());
        QCOMPARE(sceneToBytes(loaded.scene), saved); // exact round trip

        const auto a = runHeadless(doc.scene(), 900);
        const auto b = runHeadless(loaded.scene, 900);
        QCOMPARE(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i) QVERIFY(a[i] == b[i]);
    }
};

QTEST_MAIN(TestAcceptanceM1)
#include "test_acceptance_m1.moc"
