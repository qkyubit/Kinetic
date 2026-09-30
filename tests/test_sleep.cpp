// Sleep delay ("physics timeout"), field wake-ups and the world limit.
#include "core/Scene.h"
#include "core/SceneBuilder.h"

#include <QDir>
#include <QFile>
#include <random>

#include <QTest>

using namespace kn;

namespace {

Snapshot snap(const SimCore& core)
{
    Snapshot s;
    core.writeSnapshot(s);
    return s;
}

void run(SimCore& core, double seconds)
{
    for (int i = 0; i < int(seconds * 120); ++i) core.advance();
}

} // namespace

class TestSleep : public QObject {
    Q_OBJECT
private slots:
    void weakFieldKeepsMoving_data()
    {
        QTest::addColumn<double>("fieldDelay");
        QTest::addColumn<bool>("keepsMoving");
        QTest::newRow("Box2D's own 0.5 s: falls asleep") << 0.5 << false;
        QTest::newRow("5 s field delay: keeps moving") << 5.0 << true;
    }

    void weakFieldKeepsMoving()
    {
        // A box at rest in a gentle vortex starts very slowly (0.02 m/s²); with
        // only Box2D's 0.5 s rule it goes to sleep before it gets going.
        QFETCH(double, fieldDelay);
        QFETCH(bool, keepsMoving);
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        sb.setWorld("fieldSleepDelay", fieldDelay);
        const ItemId box = sb.add("body.box", {2, 0}, {{"width", 0.5}, {"height", 0.5}});
        sb.add("field.vortex", {0, 0}, {{"radius", 6.0}, {"strength", -0.03}});
        SimCore core;
        core.start(sb.start());
        run(core, 6.0);
        const Snapshot s = snap(core);
        const BodySnap* b = body(s, box);
        qInfo("field delay %.1f s: box at (%.3f, %.3f), awake %d", fieldDelay, double(b->x), double(b->y), int(b->awake));
        QCOMPARE(b->awake != 0, keepsMoving);
        const double moved = std::hypot(b->x - 2.0, b->y);
        if (keepsMoving) QVERIFY(moved > 0.2);
        else QVERIFY(moved < 0.01);
    }

    void staticBodyInWindSleepsAfterDelay()
    {
        // A heavy block the wind cannot move sleeps only after the field delay.
        SceneBuilder sb;
        sb.setWorld("fieldSleepDelay", 3.0);
        sb.add("body.ground", {0, -0.5}, {{"width", 40.0}});
        const ItemId block = sb.add("body.box", {0, 0.5}, {{"width", 1.0}, {"height", 1.0}, {"density", 5000.0}});
        sb.add("field.wind", {0, 1}, {{"speed", 5.0}, {"width", 6.0}, {"height", 4.0}});
        SimCore core;
        core.start(sb.start(true));
        run(core, 2.5);
        QCOMPARE(body(snap(core), block)->awake, uint8_t(1));
        run(core, 1.5);
        QCOMPARE(body(snap(core), block)->awake, uint8_t(0));
        // The delay survives keyframe rebuilds: scrubbing reproduces it exactly.
        RestoreState live, again;
        core.world()->capture(live);
        core.scrubTo(100);
        core.scrubTo(core.recorder().head);
        core.world()->capture(again);
        QVERIFY(again == live);
    }

    void bodyEnteringWakesRegion()
    {
        SceneBuilder sb;
        sb.setWorld("fieldSleepDelay", 1.0);
        sb.add("body.ground", {0, -0.5}, {{"width", 40.0}});
        const ItemId block = sb.add("body.box", {0, 0.5}, {{"width", 1.0}, {"height", 1.0}, {"density", 5000.0}});
        sb.add("field.wind", {0, 1}, {{"speed", 5.0}, {"width", 6.0}, {"height", 4.0}});
        const ItemId ball = sb.add("body.circle", {-8, 0.3}, {{"radius", 0.3}, {"velocity", Vec2{6, 0}}});
        SimCore core;
        core.start(sb.start());
        // The block falls asleep before the ball arrives...
        bool slept = false, wokeWhenEntered = false;
        for (int i = 0; i < 120 * 3; ++i) {
            core.advance();
            const Snapshot s = snap(core);
            if (!body(s, block)->awake) slept = true;
            if (slept && body(s, ball)->x > -3.05 && body(s, block)->awake) wokeWhenEntered = true;
        }
        QVERIFY(slept);
        QVERIFY(wokeWhenEntered);
    }

    void worldLimitFreezesInsteadOfCrashing()
    {
        // Box2D asserts at 100 km; a body falling forever is frozen at the limit.
        SceneBuilder sb;
        sb.setWorld("worldLimit", 2000.0);
        const ItemId ball = sb.add("body.circle", {0, 0}, {{"radius", 0.2}});
        SimCore core;
        core.start(sb.start(true));
        run(core, 30.0); // free fall passes 2 km after about 20 s
        const Snapshot s = snap(core);
        const BodySnap* b = body(s, ball);
        qInfo("frozen at y = %.1f m, v = %.2f", double(b->y), double(b->vy));
        QVERIFY(b->y < -2000.0 && b->y > -2100.0);
        QCOMPARE(b->vy, 0.0f);
        QVERIFY(std::isfinite(s.energy.total));
        RestoreState live, again;
        core.world()->capture(live);
        core.scrubTo(2500); // before the freeze
        core.scrubTo(core.recorder().head);
        core.world()->capture(again);
        QVERIFY(again == live);
        // Dragging it back brings it to life.
        LoggedCommand c;
        c.type = LoggedCommand::Type::SetState;
        c.id = ball;
        c.pose = Pose::fromAngle({0, 5}, 0);
        core.edit(c);
        run(core, 0.5);
        QVERIFY(body(snap(core), ball)->vy < -1.0);
    }

    void grabFuzzAcrossExamples()
    {
        // Violent grabbing, dragging and throwing in every example must never
        // produce non-finite state (run with Box2D assertions in CI builds).
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        const QDir dir(QStringLiteral(KINETIC_SOURCE_DIR "/resources/examples"));
        for (const QString& f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            if (f.startsWith(QLatin1String("99_"))) continue;
            QFile file(dir.filePath(f));
            QVERIFY(file.open(QIODevice::ReadOnly));
            SceneLoadResult r = sceneFromBytes(file.readAll());
            QVERIFY(r.ok);
            SimStart st;
            for (const ItemDef& d : r.scene.items) st.items.push_back(std::make_shared<ItemDef>(d));
            st.world = std::make_shared<ItemDef>(r.scene.world);
            SimCore core;
            core.start(st);
            std::vector<ItemId> dynamic;
            {
                Snapshot s;
                core.writeSnapshot(s);
                for (const BodySnap& b : s.bodies)
                    if (b.bodyType == 2) dynamic.push_back(b.id);
            }
            if (dynamic.empty()) continue;
            Vec2 target;
            for (int i = 0; i < 1500; ++i) {
                LoggedCommand c;
                if (i % 150 == 0) {
                    Snapshot s;
                    core.writeSnapshot(s);
                    const BodySnap* b = body(s, dynamic[rng() % dynamic.size()]);
                    c.type = LoggedCommand::Type::Grab;
                    c.id = b->id;
                    c.v = {0.2 * u(rng), 0.2 * u(rng)};
                    target = {b->x, b->y};
                    c.pose.p = target;
                    core.edit(c);
                } else if (i % 150 == 120) {
                    c.type = LoggedCommand::Type::Release;
                    core.edit(c);
                } else {
                    target += Vec2{u(rng), u(rng)} * (i % 7 == 0 ? 40.0 : 0.5); // occasional wild jumps
                    c.type = LoggedCommand::Type::GrabMove;
                    c.pose.p = target;
                    core.edit(c);
                }
                core.advance();
            }
            Snapshot s;
            core.writeSnapshot(s);
            for (const BodySnap& b : s.bodies)
                QVERIFY2(std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.c) && std::isfinite(b.s), qPrintable(f));
        }
    }
};

QTEST_GUILESS_MAIN(TestSleep)
#include "test_sleep.moc"
