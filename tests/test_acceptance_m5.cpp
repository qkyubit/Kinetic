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
        // Each stage must happen, in order: hammer -> ball -> block -> dominoes -> green ball -> finish.
        const SceneData scene = loadExample(QStringLiteral("16_rube_goldberg_chain.json"));
        ItemId finish = 0, ball = 0, block = 0, green = 0;
        std::vector<ItemId> dominoes;
        for (const ItemDef& d : scene.items) {
            if (d.type->typeId() == QLatin1String("tool.trigger")) finish = d.id;
            if (d.name == QLatin1String("Ball")) ball = d.id;
            if (d.name == QLatin1String("Pusher block")) block = d.id;
            if (d.name == QLatin1String("Green ball")) green = d.id;
            if (d.name == QLatin1String("Domino")) dominoes.push_back(d.id);
        }
        QVERIFY(finish && ball && block && green && dominoes.size() == 9);
        SimCore core;
        core.start(startFrom(scene));
        const double blockX0 = [&] {
            for (const ItemDef& d : scene.items)
                if (d.id == block) return d.v(d.type->positionIndex()).x;
            return 0.0;
        }();
        double reached = -1, maxBallX = -1e9, maxBlockTilt = 0;
        std::vector<double> fell(dominoes.size(), -1.0);
        double greenMoved = -1;
        Snapshot s;
        for (int i = 0; i < 120 * 20 && reached < 0; ++i) {
            core.advance();
            for (const TriggerEvent& e : core.events())
                if (e.trigger == finish && e.body == green && e.enter) reached = e.time;
            core.clearEvents();
            core.writeSnapshot(s);
            maxBallX = std::max(maxBallX, double(body(s, ball)->x));
            maxBlockTilt = std::max(maxBlockTilt, std::abs(std::atan2(double(body(s, block)->s), double(body(s, block)->c))));
            for (size_t k = 0; k < dominoes.size(); ++k) {
                const BodySnap* d = body(s, dominoes[k]);
                if (fell[k] < 0 && std::abs(std::atan2(double(d->s), double(d->c))) > 0.8) fell[k] = s.time;
            }
            if (greenMoved < 0 && std::hypot(body(s, green)->vx, body(s, green)->vy) > 0.05) greenMoved = s.time;
        }
        QString order;
        for (double t : fell) order += QStringLiteral(" %1").arg(t, 0, 'f', 2);
        qInfo("Rube Goldberg: dominoes fall at%s s; green ball moves at %.2f s; finish at %.2f s", qPrintable(order),
              greenMoved, reached);
        qInfo("  ball max x %.2f (block started at %.2f), block max tilt %.1f°", maxBallX, blockX0, maxBlockTilt * kDegPerRad);
        QVERIFY(maxBallX < blockX0);        // the ball never gets past the block
        QVERIFY(maxBlockTilt < 0.35);       // the block slides; it does not tumble
        for (size_t k = 0; k < fell.size(); ++k) {
            QVERIFY2(fell[k] > 0, qPrintable(QStringLiteral("domino %1 never fell").arg(k + 1)));
            if (k > 0) QVERIFY2(fell[k] >= fell[k - 1], "dominoes fell out of order");
        }
        QVERIFY(greenMoved >= fell.back() - 0.1); // pushed by the last domino
        QVERIFY(reached > greenMoved);
    }
};

QTEST_GUILESS_MAIN(TestAcceptanceM5)
#include "test_acceptance_m5.moc"
