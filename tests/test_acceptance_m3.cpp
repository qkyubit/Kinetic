// M3 acceptance: a frictionless pendulum drifts less than 0.5 % in energy over
// 60 s, scrubbing to any frame matches a live run exactly, a saved replay file
// plays back bit-identically, 10 minutes of 500 bodies record in under 50 MB,
// and trigger zones log enter/leave times.
#include "core/Replay.h"
#include "core/SceneBuilder.h"

#include <QFile>
#include <QTest>

#include <random>

using namespace kn;

namespace {

// Rigid pendulum: a steel bob on a massless rod pinned to the world.
SceneBuilder pendulumScene(const char* connector, double angle0)
{
    SceneBuilder sb;
    const Vec2 pivot{0, 5};
    const double L = 2.0;
    const Vec2 bob = pivot + Vec2{std::sin(angle0), -std::cos(angle0)} * L;
    const ItemId b = sb.add("body.circle", bob, {{"radius", 0.1}, {"material", int64_t(1)}});
    if (QByteArray(connector) == "connector.rod")
        sb.connect(connector, b, bob, 0, pivot);
    else
        sb.connect(connector, b, pivot, 0, pivot); // hinge at the pivot, bob offset on the body
    return sb;
}

} // namespace

class TestAcceptanceM3 : public QObject {
    Q_OBJECT
private slots:
    void pendulumEnergyDrift_data()
    {
        QTest::addColumn<QByteArray>("connector");
        QTest::addColumn<double>("jointHertz");
        QTest::newRow("rod, stiff joints") << QByteArray("connector.rod") << 240.0;
        QTest::newRow("rod, default joints") << QByteArray("connector.rod") << 60.0;
        QTest::newRow("hinge, stiff joints") << QByteArray("connector.hinge") << 240.0;
    }

    void pendulumEnergyDrift()
    {
        QFETCH(QByteArray, connector);
        QFETCH(double, jointHertz);
        SceneBuilder sb = pendulumScene(connector.constData(), 1.0);
        sb.setWorld("jointHertz", jointHertz);
        SimCore core;
        core.start(sb.start());
        Snapshot s;
        core.writeSnapshot(s);
        const double e0 = s.energy.total;
        // Energy scale: the swing's kinetic energy at the bottom (PE relative to the lowest point).
        const double swing = s.energy.kinetic + (s.energy.gravity - (-s.bodies[0].mass * 9.81 * (5.0 - 2.0)));
        double worst = 0;
        for (int i = 1; i <= 120 * 60; ++i) {
            core.advance();
            if (i % 12) continue;
            core.writeSnapshot(s);
            worst = std::max(worst, std::abs(s.energy.total - e0) / swing);
        }
        qInfo("%s pendulum (%g Hz joints): max energy deviation over 60 s = %.4f %% of the swing energy (%.3f J)", connector.constData(),
              jointHertz, worst * 100, swing);
        QVERIFY(worst < 0.005);
    }

    void scrubMatchesLiveRun()
    {
        // A scene with stateful items (piston direction, gear angle), contacts,
        // sleeping, and edits in the middle of the run.
        SceneBuilder sb;
        sb.add("body.ground", {0, -0.5}, {{"width", 40.0}});
        for (int i = 0; i < 40; ++i) {
            const Vec2 at{-6.0 + (i % 8) * 0.7, 0.5 + (i / 8) * 0.8};
            if (i % 3)
                sb.add("body.box", at, {{"width", 0.5}, {"height", 0.5}});
            else
                sb.add("body.circle", at, {{"radius", 0.3}});
        }
        const ItemId a = sb.add("body.circle", {6, 4});
        sb.connect("connector.spring", 0, {6, 7}, a, {6, 4}, {{"stiffness", 300.0}});
        const ItemId p = sb.add("body.box", {10, 1});
        sb.connect("mechanism.piston", 0, {10, -0.2}, p, {10, 1}, {{"stroke", 1.5}, {"speed", 2.0}});
        const ItemId g1 = sb.add("body.circle", {14, 3}, {{"mask", int64_t(0)}});
        const ItemId g2 = sb.add("body.circle", {16, 3}, {{"mask", int64_t(0)}});
        sb.connect("mechanism.motor", g1, {14, 3}, 0, {14, 3}, {{"speed", 3.0}});
        sb.connect("connector.hinge", g2, {16, 3}, 0, {16, 3});
        sb.connect("connector.gear", g1, {14, 3}, g2, {16, 3});
        sb.setWorld("keyframeInterval", int64_t(50));

        SimCore core;
        core.start(sb.start(true));
        const int steps = 1000;
        std::vector<RestoreState> live(steps + 1);
        core.world()->capture(live[0]);
        const ItemDef boxDef = sb.scene.items[3];
        for (int s = 1; s <= steps; ++s) {
            if (s == 300) { // live friction edit
                ItemDef d = boxDef;
                const int mu = d.type->schema().indexOf("staticFriction");
                d.set(mu, 0.05);
                LoggedCommand c;
                c.type = LoggedCommand::Type::Update;
                c.def = std::make_shared<ItemDef>(d);
                c.mask = propBit(mu);
                core.edit(c);
            }
            if (s == 500) { // add a body mid-run
                LoggedCommand c;
                c.type = LoggedCommand::Type::Add;
                c.def = std::make_shared<ItemDef>(ItemRegistry::instance().find("body.box")->makeDefault(900, {-3, 6}));
                core.edit(c);
            }
            if (s == 700) { // remove one
                LoggedCommand c;
                c.type = LoggedCommand::Type::Remove;
                c.id = sb.scene.items[5].id;
                core.edit(c);
            }
            core.advance();
            core.world()->capture(live[size_t(s)]);
        }
        QCOMPARE(core.recorder().head, uint64_t(steps));

        // Scrub to frames in random order, including keyframe and edit boundaries.
        std::mt19937 rng(1234);
        std::vector<int> targets = {0, 1, 49, 50, 51, 299, 300, 301, 499, 500, 501, 699, 700, 701, 999, 1000};
        for (int i = 0; i < 40; ++i) targets.push_back(int(rng() % (steps + 1)));
        std::shuffle(targets.begin(), targets.end(), rng);
        for (int t : targets) {
            core.scrubTo(uint64_t(t));
            RestoreState rs;
            core.world()->capture(rs);
            QVERIFY2(rs == live[size_t(t)], qPrintable(QStringLiteral("frame %1 differs").arg(t)));
            // Playing forward from a scrubbed frame follows the recording too.
            const int k = std::min(steps - t, 37);
            for (int j = 0; j < k; ++j) core.advance();
            core.world()->capture(rs);
            QVERIFY2(rs == live[size_t(t + k)], qPrintable(QStringLiteral("frame %1 after replaying from %2").arg(t + k).arg(t)));
        }

        // The same run saved to a replay file and loaded into a fresh core.
        ReplayFile rf;
        QString err;
        QVERIFY2(replayFromBytes(replayToBytes(core.exportReplay(), {}), rf, &err), qPrintable(err));
        QCOMPARE(rf.replay.log.size(), size_t(3));
        SimStart st;
        st.items = rf.replay.items;
        st.world = rf.replay.world;
        st.log = rf.replay.log;
        st.headStep = rf.replay.headStep;
        SimCore replay;
        replay.start(st);
        for (int s = 1; s <= steps; ++s) {
            replay.advance();
            if (s % 100) continue;
            RestoreState rs;
            replay.world()->capture(rs);
            QVERIFY2(rs == live[size_t(s)], qPrintable(QStringLiteral("replay file differs at frame %1").arg(s)));
        }
    }

    void liveDragMovesBodyNotDocument()
    {
        // Dragging a body while simulating sends SetState: the body continues
        // from the dragged pose at rest; scrubbing and replay files reproduce it.
        SceneBuilder sb;
        const ItemId b = sb.add("body.circle", {0, 10}, {{"radius", 0.2}});
        SimCore core;
        core.start(sb.start(true));
        for (int i = 0; i < 60; ++i) core.advance(); // falling
        LoggedCommand c;
        c.type = LoggedCommand::Type::SetState;
        c.id = b;
        c.pose = Pose::fromAngle({5, 20}, 0.5);
        core.edit(c);
        Snapshot s;
        core.writeSnapshot(s);
        QCOMPARE(body(s, b)->x, 5.0f);
        QCOMPARE(body(s, b)->y, 20.0f);
        QCOMPARE(body(s, b)->vy, 0.0f);
        for (int i = 0; i < 60; ++i) core.advance();
        core.writeSnapshot(s);
        const double y = body(s, b)->y;
        // Free fall from rest for 0.5 s (semi-implicit Euler, within one step).
        QVERIFY(std::abs(y - (20 - 0.5 * 9.81 * 0.25)) < 9.81 * 0.5 / 120);
        RestoreState live;
        core.world()->capture(live);
        // The initial conditions are untouched.
        QCOMPARE(sb.item(b).v(sb.item(b).type->positionIndex()), (Vec2{0, 10}));
        // Scrub across the drag.
        core.scrubTo(30);
        core.scrubTo(120);
        RestoreState again;
        core.world()->capture(again);
        QVERIFY(again == live);
        // Replay file.
        ReplayFile rf;
        QVERIFY(replayFromBytes(replayToBytes(core.exportReplay(), {}), rf, nullptr));
        SimStart st;
        st.items = rf.replay.items;
        st.world = rf.replay.world;
        st.log = rf.replay.log;
        st.headStep = rf.replay.headStep;
        SimCore replay;
        replay.start(st);
        for (int i = 0; i < 120; ++i) replay.advance();
        replay.world()->capture(again);
        QVERIFY(again == live);
    }

    void grabToolDragsSpinsPushesAndThrows()
    {
        SceneBuilder sb;
        sb.setWorld("gravity", Vec2{0, 0});
        sb.setWorld("sleep", false);
        const ItemId held = sb.add("body.box", {0, 0}, {{"width", 1.0}, {"height", 0.4}});
        const ItemId other = sb.add("body.box", {4, 0}, {{"width", 0.5}, {"height", 0.5}});
        SimCore core;
        core.start(sb.start(true));
        auto cmd = [&](LoggedCommand::Type t, Vec2 target) {
            LoggedCommand c;
            c.type = t;
            c.id = held;
            c.v = {0.45, 0.0}; // grip near the right end: pulling sideways spins it
            c.pose.p = target;
            core.edit(c);
        };
        Snapshot s;
        cmd(LoggedCommand::Type::Grab, {0.45, 0});
        double maxSpin = 0;
        // Sweep the cursor up and then right at 6 m/s for 1.5 s.
        for (int i = 1; i <= 180; ++i) {
            const double t = i / 120.0;
            cmd(LoggedCommand::Type::GrabMove, t < 0.5 ? Vec2{0.45, 6 * t} : Vec2{0.45 + 6 * (t - 0.5), 3});
            core.advance();
            core.writeSnapshot(s);
            maxSpin = std::max(maxSpin, std::abs(double(body(s, held)->w)));
        }
        const BodySnap* h = body(s, held);
        double fy = 0;
        qInfo("grab: held box at (%.2f, %.2f), v = (%.2f, %.2f), max spin %.2f rad/s, hand force %.0f N", double(h->x), double(h->y),
              double(h->vx), double(h->vy), maxSpin, forceOf(*h, ForceSource::Hand, &fy));
        QVERIFY(maxSpin > 1.0);                   // off-centre pull spins it
        QVERIFY(forceOf(*h, ForceSource::Hand) > 0); // tagged in the force breakdown
        QVERIFY(h->vx > 3.0);                     // following the cursor
        // Release while moving: momentum is kept (no gravity, nothing in the way above).
        const Vec2 v0{h->vx, h->vy};
        cmd(LoggedCommand::Type::Release, {});
        for (int i = 0; i < 60; ++i) core.advance();
        core.writeSnapshot(s);
        qInfo("thrown: v = (%.2f, %.2f) 0.5 s after release (was (%.2f, %.2f))", double(body(s, held)->vx), double(body(s, held)->vy),
              v0.x, v0.y);
        QVERIFY(std::abs(body(s, held)->vx - v0.x) < 1e-3 && std::abs(body(s, held)->vy - v0.y) < 1e-3);
        QCOMPARE(forceOf(*body(s, held), ForceSource::Hand), 0.0);

        // Scrub and replay reproduce the whole interaction.
        RestoreState live, again;
        core.world()->capture(live);
        core.scrubTo(45);
        core.scrubTo(core.recorder().head);
        core.world()->capture(again);
        QVERIFY(again == live);
        ReplayFile rf;
        QVERIFY(replayFromBytes(replayToBytes(core.exportReplay(), {}), rf, nullptr));
        SimStart st;
        st.items = rf.replay.items;
        st.world = rf.replay.world;
        st.log = rf.replay.log;
        st.headStep = rf.replay.headStep;
        SimCore replay;
        replay.start(st);
        while (replay.step() < rf.replay.headStep) replay.advance();
        replay.world()->capture(again);
        QVERIFY(again == live);
        Q_UNUSED(other);
    }

    void grabToolPushesAndHolds()
    {
        // Dragging a held box into another pushes it; holding still under
        // gravity the hand carries the weight.
        SceneBuilder sb;
        sb.setWorld("sleep", false);
        sb.add("body.ground", {0, -0.5}, {{"width", 40.0}});
        const ItemId held = sb.add("body.box", {0, 0.25}, {{"width", 0.5}, {"height", 0.5}});
        const ItemId other = sb.add("body.box", {2, 0.25}, {{"width", 0.5}, {"height", 0.5}});
        SimCore core;
        core.start(sb.start());
        auto cmd = [&](LoggedCommand::Type t, Vec2 target) {
            LoggedCommand c;
            c.type = t;
            c.id = held;
            c.v = {0, 0};
            c.pose.p = target;
            core.edit(c);
        };
        cmd(LoggedCommand::Type::Grab, {0, 0.25});
        for (int i = 1; i <= 240; ++i) {
            cmd(LoggedCommand::Type::GrabMove, {std::min(4.0, 3.0 * i / 120.0), 0.25});
            core.advance();
        }
        Snapshot s;
        core.writeSnapshot(s);
        qInfo("push: other box moved to x = %.2f", double(body(s, other)->x));
        QVERIFY(body(s, other)->x > 4.0);
        // Lift and hold still for 3 s.
        cmd(LoggedCommand::Type::GrabMove, {0, 3});
        for (int i = 0; i < 360; ++i) core.advance();
        core.writeSnapshot(s);
        const BodySnap* h = body(s, held);
        double fy = 0;
        forceOf(*h, ForceSource::Hand, &fy);
        const double mg = h->mass * 9.81;
        qInfo("hold: hand force %.2f N vs weight %.2f N", fy, mg);
        QVERIFY(std::abs(fy - mg) / mg < 0.02);
    }

    void replayMemory()
    {
        // 500 bodies for 10 minutes at 120 Hz, recording on.
        SceneBuilder sb;
        sb.add("body.ground", {0, -0.5}, {{"width", 60.0}});
        for (int i = 0; i < 500; ++i) {
            const Vec2 at{-20.0 + (i % 50) * 0.8, 0.5 + (i / 50) * 0.8};
            if (i % 2)
                sb.add("body.box", at, {{"width", 0.5}, {"height", 0.5}});
            else
                sb.add("body.circle", at, {{"radius", 0.3}});
        }
        // Keep everything awake so every keyframe holds full body state.
        sb.setWorld("sleep", false);
        SimCore core;
        core.start(sb.start(true));
        const int steps = 120 * 60 * 10;
        for (int s = 0; s < steps; ++s) core.advance();
        const double mb = double(core.recorder().memoryBytes()) / (1024.0 * 1024.0);
        qInfo("replay of %d steps with 500 bodies: %zu keyframes, %.2f MB", steps, core.recorder().keyframes.size(), mb);
        QCOMPARE(core.recorder().head, uint64_t(steps));
        QVERIFY(mb < 50.0);
    }

    void triggerZone()
    {
        // A ball dropped through a 1 m tall zone: enter when its bottom reaches
        // the top edge, leave when its top clears the bottom edge.
        SceneBuilder sb;
        const double r = 0.1, y0 = 6.0, top = 4.0, bottom = 3.0;
        const ItemId ball = sb.add("body.circle", {0, y0}, {{"radius", r}});
        const ItemId zone = sb.add("tool.trigger", {0, 0.5 * (top + bottom)}, {{"width", 2.0}, {"height", top - bottom}});
        sb.setWorld("keyframeInterval", int64_t(10)); // rebuilds while inside must not re-report
        SimCore core;
        core.start(sb.start(true));
        for (int i = 0; i < 120; ++i) core.advance();
        const auto events = core.events();
        QCOMPARE(events.size(), size_t(2));
        const double g = 9.81;
        const double tEnter = std::sqrt(2 * (y0 - top - r) / g), tLeave = std::sqrt(2 * (y0 - bottom + r) / g);
        qInfo("trigger: enter %.4f s (analytic %.4f), leave %.4f s (analytic %.4f)", events[0].time, tEnter,
              events[1].time, tLeave);
        const double dt = 1.0 / 120.0;
        QVERIFY(events[0].enter && !events[1].enter);
        QCOMPARE(events[0].trigger, zone);
        QCOMPARE(events[0].body, ball);
        QVERIFY(std::abs(events[0].time - tEnter) < 2 * dt);
        QVERIFY(std::abs(events[1].time - tLeave) < 2 * dt);
        QVERIFY(std::abs(events[0].time - double(events[0].step) * dt) < 1e-6); // time sums float steps
        Snapshot s;
        core.writeSnapshot(s);
        QCOMPARE(itemSnap(s, zone)->v[4], 0.0f);
        QCOMPARE(itemSnap(s, zone)->v[5], 1.0f);

        // Scrubbing re-simulates silently.
        core.clearEvents();
        core.scrubTo(80);
        core.scrubTo(10);
        QVERIFY(core.events().empty());
        // Playing on from a scrubbed frame reports the same events again.
        for (int i = 10; i < 120; ++i) core.advance();
        QCOMPARE(core.events().size(), size_t(2));
        QCOMPARE(core.events()[0].step, events[0].step);
        QCOMPARE(core.events()[1].step, events[1].step);
    }
};

QTEST_GUILESS_MAIN(TestAcceptanceM3)
#include "test_acceptance_m3.moc"
