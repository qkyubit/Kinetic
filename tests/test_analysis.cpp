// Graph sampling and CSV export, and the trigger log, driven through the real
// physics thread.
#include "app/GraphPanel.h"
#include "app/TriggerLogPanel.h"
#include "app/Viewport.h"
#include "core/SceneBuilder.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace kn;

class TestAnalysis : public QObject {
    Q_OBJECT
private slots:
    void graphAndTriggerLogCsv()
    {
        SceneBuilder sb;
        const ItemId ball = sb.add("body.circle", {0, 6}, {{"radius", 0.1}});
        sb.add("tool.trigger", {0, 3.5}, {{"width", 2.0}, {"height", 1.0}});
        Document doc;
        doc.reset(sb.scene);
        SimController sim(&doc);
        GraphPanel graph(&doc, &sim);
        graph.addSeries({Quantity::Kind::Body, ball, Quantity::Y, 0});
        graph.addSeries({Quantity::Kind::Body, ball, Quantity::Vy, 0});
        TriggerLogPanel log(&doc, &sim);

        sim.step(120); // 1 s
        QTRY_VERIFY_WITH_TIMEOUT(sim.live() && sim.snapshot().step == 120, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(log.rowCount(), 2, 5000);
        QVERIFY(graph.rowCount() >= 1);

        QTemporaryDir dir;
        const QString gpath = dir.filePath(QStringLiteral("graph.csv"));
        QVERIFY(graph.exportCsv(gpath));
        QFile gf(gpath);
        QVERIFY(gf.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(gf.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), graph.rowCount() + 1);
        QVERIFY(lines[0].startsWith(QLatin1String("time (s),")));
        QVERIFY(lines[0].contains(QLatin1String("(m)")));
        // Last row: free fall after 1 s (semi-implicit Euler, within one step).
        const QStringList last = lines.back().split(QLatin1Char(','));
        QCOMPARE(last.size(), 3);
        const double t = last[0].toDouble(), y = last[1].toDouble(), vy = last[2].toDouble();
        QVERIFY(std::abs(t - 1.0) < 1e-6); // time sums float steps
        QVERIFY(std::abs(vy + 9.81 * t) < 1e-3);
        QVERIFY(std::abs(y - (6 - 0.5 * 9.81 * t * t)) < 9.81 * t / 120.0);

        const QString tpath = dir.filePath(QStringLiteral("triggers.csv"));
        QVERIFY(log.exportCsv(tpath));
        QFile tf(tpath);
        QVERIFY(tf.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList rows = QString::fromUtf8(tf.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0], QStringLiteral("time_s,step,trigger,body,event"));
        QVERIFY(rows[1].endsWith(QLatin1String(",enter")));
        QVERIFY(rows[2].endsWith(QLatin1String(",leave")));

        // Scrubbing back and re-simulating does not duplicate log entries.
        sim.scrubTo(30);
        QTRY_VERIFY_WITH_TIMEOUT(sim.snapshot().step == 30, 5000);
        sim.step(90);
        QTRY_VERIFY_WITH_TIMEOUT(sim.snapshot().step == 120, 5000);
        QTest::qWait(50);
        QCOMPARE(log.rowCount(), 2);
        sim.reset();
    }

    void dragLiveBodyMovesFromLivePose()
    {
        // Regression: dragging while simulating started from the initial pose.
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, -2.0});
        const ItemId ball = sb.add("body.circle", {0, 5}, {{"radius", 0.3}});
        Document doc;
        doc.reset(sb.scene);
        SimController sim(&doc);
        Viewport view(&doc, &sim);
        view.resize(800, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setCamera({0, 4}, 50);
        sim.step(120); // falls 1 m
        QTRY_VERIFY_WITH_TIMEOUT(sim.live() && sim.snapshot().step == 120, 5000);
        const Vec2 before{sim.bodySnap(ball)->x, sim.bodySnap(ball)->y};
        QVERIFY(before.y < 4.1);
        const QPoint p0 = view.toScreen(before).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, p0);
        for (int i = 1; i <= 10; ++i) {
            QMouseEvent move(QEvent::MouseMove, QPointF(p0 + QPoint(10 * i, 0)), view.mapToGlobal(QPointF(p0 + QPoint(10 * i, 0))),
                             Qt::NoButton, Qt::LeftButton, {});
            QApplication::sendEvent(&view, &move);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, {}, p0 + QPoint(100, 0));
        // 100 px at 50 px/m = 2 m to the right of the live position, not of the initial one.
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(sim.bodySnap(ball)->x - (before.x + 2.0)) < 0.05, 5000);
        QVERIFY(std::abs(sim.bodySnap(ball)->y - before.y) < 0.05);
        // The document still holds the initial conditions.
        const ItemDef* d = doc.item(ball);
        QCOMPARE(d->v(d->type->positionIndex()), (Vec2{0, 5}));
        sim.reset();
    }
};

QTEST_MAIN(TestAnalysis)
#include "test_analysis.moc"
