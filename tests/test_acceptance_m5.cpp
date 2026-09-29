// M5 acceptance: every bundled example loads cleanly and runs without
// non-finite state; the Rube Goldberg chain reaches its finish gate.
#include "core/Scene.h"
#include "core/SceneBuilder.h"

#include <QDir>
#include <QFile>
#include <QTest>

using namespace kn;

namespace {

SimStart startFrom(const SceneData& scene)
{
    SimStart st;
    for (const ItemDef& d : scene.items) st.items.push_back(std::make_shared<ItemDef>(d));
    st.world = std::make_shared<ItemDef>(scene.world);
    return st;
}

SceneData loadExample(const QString& file)
{
    QFile f(QStringLiteral(KINETIC_SOURCE_DIR "/resources/examples/") + file);
    if (!f.open(QIODevice::ReadOnly)) qFatal("cannot open %s", qPrintable(file));
    SceneLoadResult r = sceneFromBytes(f.readAll());
    if (!r.ok) qFatal("%s: %s", qPrintable(file), qPrintable(r.error));
    if (!r.warnings.isEmpty()) qFatal("%s: %s", qPrintable(file), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    return std::move(r.scene);
}

} // namespace

class TestAcceptanceM5 : public QObject {
    Q_OBJECT
private slots:
    void examplesRun_data()
    {
        QTest::addColumn<QString>("file");
        const QStringList files = QDir(QStringLiteral(KINETIC_SOURCE_DIR "/resources/examples"))
                                      .entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        QVERIFY(files.size() >= 11);
        for (const QString& f : files)
            if (!f.startsWith(QLatin1String("99_"))) QTest::newRow(qPrintable(f)) << f;
    }

    void examplesRun()
    {
        QFETCH(QString, file);
        const SceneData scene = loadExample(file);
        SimCore core;
        core.start(startFrom(scene));
        for (int i = 0; i < 360; ++i) core.advance();
        Snapshot s;
        core.writeSnapshot(s);
        for (const BodySnap& b : s.bodies) {
            QVERIFY2(std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.vx) && std::isfinite(b.vy),
                     qPrintable(QStringLiteral("body %1").arg(b.id)));
            QVERIFY2(std::hypot(b.vx, b.vy) < 1000, qPrintable(QStringLiteral("body %1 exploded").arg(b.id)));
        }
        QVERIFY(std::isfinite(s.energy.total));
        // Equation items flag non-finite values in their snapshots.
        for (const ItemDef& d : scene.items) {
            if (d.type->kind() != ItemKind::Equation) continue;
            const ItemSnap* is = itemSnap(s, d.id);
            QVERIFY(is);
            for (int i = 0; i < 8; ++i) QVERIFY(std::isfinite(is->v[i]));
        }
    }

    void rubeGoldbergFinishes()
    {
        const SceneData scene = loadExample(QStringLiteral("16_rube_goldberg_chain.json"));
        ItemId finish = 0, ball = 0;
        for (const ItemDef& d : scene.items) {
            if (d.type->typeId() == QLatin1String("tool.trigger")) finish = d.id;
            if (d.name == QLatin1String("Green ball")) ball = d.id;
        }
        QVERIFY(finish && ball);
        SimCore core;
        core.start(startFrom(scene));
        double reached = -1;
        for (int i = 0; i < 120 * 20 && reached < 0; ++i) {
            core.advance();
            for (const TriggerEvent& e : core.events())
                if (e.trigger == finish && e.body == ball && e.enter) reached = e.time;
            core.clearEvents();
        }
        qInfo("Rube Goldberg chain: finish gate reached at t = %.2f s", reached);
        QVERIFY(reached > 0);
    }
};

QTEST_GUILESS_MAIN(TestAcceptanceM5)
#include "test_acceptance_m5.moc"
